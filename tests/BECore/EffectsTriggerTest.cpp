// A trigger from content to host (Values/Effects/CLAUDE.md): the script of a document loaded from XML runs over many cells, a host loop dispatches the commands it emitted, and every
// cell and every dispatch equals a C++ reference.

#include <BECore/EffectsTriggerTypes.h>
#include <BECore/Random/Rng.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Values/Effects/WriteTargets.h>
#include <BECore/Values/ValueLinkScope.h>
#include <Common/AssertBridge.h>
#include <Common/Printers.h>
#include <Generated/EffectsTriggerTypes.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace EffectsData;
    using ProgramDomainData::BitsAnd;
    using ProgramDomainData::DomainBits256;

    namespace {

        constexpr size_t kCells = 512;
        constexpr size_t kRounds = 6;
        constexpr size_t kBufferCapacity = 4;
        constexpr uint32_t kMaxTimer = 5;
        constexpr uint32_t kEveryNthSharesTheMask = 3;

        /// What a host did with the commands: the index of each cell it was told to open and to mask, in the order of the commands
        struct HostLog {
            Vector<int32_t> opened;
            Vector<int32_t> masked;

            [[nodiscard]] bool operator==(const HostLog&) const = default;
        };

        /// The host loop: the one command is `Action(kind, index)`, told apart by its first argument
        void Dispatch(const EmitBuffer& buffer, CommandId action, HostLog& log) {
            for (const EmittedCommand& command : buffer.Commands()) {
                ASSERT_EQ(command.id, action);
                ASSERT_EQ(command.count, 2U);
                const int32_t index = command.Argument<int32_t>(1);
                switch (command.Argument<TriggerAction>(0)) {
                    case TriggerAction::Open:
                        log.opened.push_back(index);
                        break;
                    case TriggerAction::Mask:
                        log.masked.push_back(index);
                        break;
                }
            }
        }

        /// The script as C++
        void Reference(TriggerCell& cell, const TriggerBoard& board, HostLog& log) {
            if (cell.timer > 0) {
                cell.timer -= 1;
                if (cell.timer == 0) {
                    cell.open = !cell.open;
                    log.opened.push_back(cell.index);
                }
            } else if (board.mask != cell.mask) {
                cell.mask = BitsAnd(cell.mask, board.mask);
                log.masked.push_back(cell.index);
            }
        }

        DomainBits256 RandomMask(Rng& rng) {
            DomainBits256 mask;
            for (uint64_t& word : mask.words) {
                word = rng.Next64();
            }
            return mask;
        }

        bool SameCell(const TriggerCell& a, const TriggerCell& b) {
            return a.index == b.index && a.timer == b.timer && a.open == b.open && a.tone == b.tone && a.mask == b.mask;
        }

        TriggerBoard MakeBoard(Rng& rng) {
            TriggerBoard board;
            board.mask = RandomMask(rng);
            return board;
        }

        Vector<TriggerCell> MakeCells(Rng& rng, const TriggerBoard& board) {
            Vector<TriggerCell> cells(kCells);
            for (size_t i = 0; i < kCells; ++i) {
                TriggerCell& cell = cells[i];
                cell.index = static_cast<int32_t>(i);
                // A negative timer is a cell the trigger skips as a spent one
                cell.timer = static_cast<int32_t>(rng.Next32() % (kMaxTimer + 3)) - 2;
                cell.open = (rng.Next32() & 1U) != 0;
                cell.tone = static_cast<TriggerTone>(rng.Next32() % EnumUtils<TriggerTone>::Count());
                // Some cells already hold the board's mask: the `!=` of the script is false for them
                cell.mask = i % kEveryNthSharesTheMask == 0 ? board.mask : RandomMask(rng);
            }
            return cells;
        }

        String LoadDocument(TriggerDocument& document, const WriteTargets& targets) {
            ValueLinkScope scope("cells/trigger.xml");
            scope.UseWriteTargets(targets);
            XmlDeserializer reader;
            const String xml = Format("<root><State>{}<onTick script=\"{}\"/></State></root>", kTriggerContext, kTriggerScript);
            EXPECT_TRUE(reader.LoadFromString(xml));
            EXPECT_TRUE(reader.BeginObject("State"));
            document.Deserialize(reader);
            reader.EndObject();
            String reports;
            for (const DeserializeError& report : reader.GetErrors()) {
                AppendFormat(reports, "{}: {}\n", report.path, report.errorMessage);
            }
            return reports;
        }

    }  // namespace

    TEST(EffectsTriggerTest, ATriggerLoadedFromContentGivesTheCellsAndTheCommandsOfTheCppReference) {
        const CommandId action = RegisterTriggerAction();
        WriteTargets targets;
        ASSERT_TRUE(targets.Allow("cell"));
        TriggerDocument document;
        const String reports = LoadDocument(document, targets);
        ASSERT_TRUE(reports.empty()) << reports.c_str();
        ASSERT_FALSE(document.onTick.Empty());
        EXPECT_EQ(document.onTick.Writes(), LevelBit(0));
        EXPECT_EQ(document.onTick.Reads(), LevelBit(0) | LevelBit(1));

        Rng rng(0x7219);
        const TriggerBoard board = MakeBoard(rng);
        Vector<TriggerCell> cells = MakeCells(rng, board);
        Vector<TriggerCell> expected = cells;
        Vector<uint64_t> versions(kCells, 0);

        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<TriggerCell>("cell").has_value());
        ASSERT_TRUE(declaration.Add<TriggerBoard>("board").has_value());

        HostLog hostLog;
        HostLog referenceLog;
        EmitBufferStorage<kBufferCapacity> storage;
        EmitBuffer& buffer = storage.Buffer();
        size_t opened = 0;
        size_t masked = 0;
        for (size_t round = 0; round < kRounds; ++round) {
            for (size_t i = 0; i < kCells; ++i) {
                const TriggerCell before = expected[i];
                const uint64_t versionBefore = versions[i];
                Context context(declaration);
                context.SetMutable(0, cells[i], &versions[i]);
                context.Set(1, board);

                buffer.Clear();
                const EffectResult result = document.onTick.Run(context, buffer);
                Reference(expected[i], board, referenceLog);
                Dispatch(buffer, action, hostLog);

                ASSERT_TRUE(SameCell(cells[i], expected[i])) << "round " << round << " cell " << i;
                ASSERT_EQ(result.refused, 0U);
                ASSERT_EQ(result.dropped, 0U);
                // The counter moves exactly when a byte of the cell did
                EXPECT_EQ(versions[i] != versionBefore, !SameCell(before, expected[i])) << "round " << round << " cell " << i;
            }
            ASSERT_EQ(hostLog, referenceLog) << "round " << round;
            opened = hostLog.opened.size();
            masked = hostLog.masked.size();
        }
        // The scenario reaches every branch: a cell opens, a cell takes the mask and the host heard of both
        EXPECT_GT(opened, 0U);
        EXPECT_GT(masked, 0U);
    }

    TEST(EffectsTriggerTest, ACellThatIsNotWritableLosesTheStoresAndTheHostSeesTheCommandsOnly) {
        static_cast<void>(RegisterTriggerAction());
        WriteTargets targets;
        ASSERT_TRUE(targets.Allow("cell"));
        TriggerDocument document;
        ASSERT_TRUE(LoadDocument(document, targets).empty());

        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<TriggerCell>("cell").has_value());
        ASSERT_TRUE(declaration.Add<TriggerBoard>("board").has_value());
        TriggerCell cell;
        cell.timer = 1;
        TriggerBoard board;
        Context context(declaration);
        // The host forgot `SetMutable`: a store is refused and counted, the memory stays as it was
        context.Set(0, cell);
        context.Set(1, board);
        EmitBufferStorage<kBufferCapacity> storage;
        EffectResult result;
        int asserts = 0;
        {
            const AssertBridge::Capture capture([&asserts](const AssertEvent&) { ++asserts; });
            result = document.onTick.Run(context, storage.Buffer());
        }
        EXPECT_EQ(cell.timer, 1);
        EXPECT_GT(result.refused, 0U);
        EXPECT_GT(asserts, 0);
    }

}  // namespace BECore::Tests
