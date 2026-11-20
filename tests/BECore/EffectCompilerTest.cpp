// The text of a script to an `EffectScript` (Values/Effects/EffectCompiler.h): what each statement stores and emits, the type rules it shares with formulas, every load error and its
// column, and that a refused text leaves the pool of programs alone.

#include <BECore/EffectsToys.h>
#include <BECore/ProgramDomainTypes.h>
#include <BECore/Random/Rng.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Effects/EffectCompiler.h>
#include <Common/AssertBridge.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TestText.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;
    using ProgramDomainData::BitsAnd;
    using ProgramDomainData::BitsOr;
    using ProgramDomainData::BitsShift;
    using ProgramDomainData::DomainBits256;
    using ProgramDomainData::DomainBoard;
    using ProgramDomainData::DomainDirection;

    namespace {

        constexpr size_t kRoomyBuffer = 32;
        constexpr uint32_t kGeneratedContexts = 300;

        /// A context whose caster, board and enum widths a script may write, and whose cast it may only read
        struct Fixture {
            ContextDecl declaration;
            ValueUnitStats caster;
            ValueCastInfo cast;
            DomainBoard board;
            ValueEnumWidths widths;
            uint64_t casterVersion = 0;
            Context context{declaration};
            WriteTargets targets;
            EffectOptions options;
            EmitBufferStorage<kRoomyBuffer> storage;

            Fixture() {
                (void)EffectsData::RegisterEffectToys();
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(declaration.Add<ValueCastInfo>("cast").has_value());
                EXPECT_TRUE(declaration.Add<DomainBoard>("board").has_value());
                EXPECT_TRUE(declaration.Add<ValueEnumWidths>("widths").has_value());
                context.SetMutable(0, caster, &casterVersion);
                context.Set(1, cast);
                context.SetMutable(2, board);
                context.SetMutable(3, widths);
                EXPECT_TRUE(targets.Allow("caster"));
                EXPECT_TRUE(targets.Allow("board"));
                EXPECT_TRUE(targets.Allow("widths"));
                options.targets = &targets;
            }

            Fixture(const Fixture&) = delete;
            Fixture& operator=(const Fixture&) = delete;

            [[nodiscard]] std::expected<EffectScript, ProgramError> Compile(StringView text) const {
                return CompileEffects(text, declaration, options);
            }

            [[nodiscard]] EffectScript Must(StringView text) const {
                auto script = Compile(text);
                if (!script.has_value()) {
                    ADD_FAILURE() << String(text).c_str() << "\n" << FormatProgramError(text, script.error()).c_str();
                    return {};
                }
                return *script;
            }

            EffectResult Run(const EffectScript& script) {
                return script.Run(context, storage.Buffer());
            }

            EffectResult RunText(StringView text) {
                return Run(Must(text));
            }

            EmitBuffer& Buffer() {
                return storage.Buffer();
            }
        };

        /// The refusal of @p text, the empty error when it compiles
        ProgramError Refusal(const Fixture& fixture, StringView text) {
            const auto script = fixture.Compile(text);
            EXPECT_FALSE(script.has_value()) << String(text).c_str();
            return script.has_value() ? ProgramError{} : script.error();
        }

        /// The 1-based column of the first @p needle in @p text
        uint32_t ColumnOf(StringView text, StringView needle) {
            const size_t at = text.find(needle);
            EXPECT_NE(at, StringView::npos) << String(needle).c_str();
            return static_cast<uint32_t>(at) + 1;
        }

        void ExpectRefusal(const Fixture& fixture, StringView text, StringView at, StringView message) {
            const ProgramError error = Refusal(fixture, text);
            EXPECT_EQ(error.column, ColumnOf(text, at)) << String(text).c_str() << "\n" << FormatProgramError(text, error).c_str();
            EXPECT_TRUE(Contains(error.message, message)) << String(text).c_str() << "\n" << error.message.c_str();
        }

        template <typename T>
        T ArgumentOf(const EmittedCommand& command, size_t index) {
            return command.Argument<T>(index);
        }

    }  // namespace

    TEST(EffectCompilerTest, AnAssignmentStoresEveryTypeAFieldCanHave) {
        Fixture f;
        const EffectResult result = f.RunText("caster.alive = false; caster.attack = -5; caster.score = 77; caster.speed = 0.75; caster.precise = -2.5; caster.velocity = vec3(4, 5, 6); "
                                              "caster.element = Fire; widths.narrow = Low; widths.wide16 = Mid; widths.wide32 = Low; board.turn = 12; board.walls = board.walls;");
        EXPECT_EQ(result.stores, 12U);
        EXPECT_FALSE(f.caster.alive);
        EXPECT_EQ(f.caster.attack, -5);
        EXPECT_EQ(f.caster.score, 77);
        EXPECT_EQ(f.caster.speed, 0.75f);
        EXPECT_EQ(f.caster.precise, -2.5);
        EXPECT_EQ(f.caster.velocity, (Vec3{4.0f, 5.0f, 6.0f}));
        EXPECT_EQ(f.caster.element, ValueElement::Fire);
        EXPECT_EQ(f.widths.narrow, ValueNarrowEnum::Low);
        EXPECT_EQ(f.widths.wide16, ValueShortEnum::Mid);
        EXPECT_EQ(f.widths.wide32, ValueWideEnum::Low);
        EXPECT_EQ(f.board.turn, 12);
    }

    TEST(EffectCompilerTest, ACompoundStatementIsTheAssignmentOfItsExpression) {
        Fixture f;
        f.caster.attack = 10;
        f.caster.speed = 1.5f;
        f.caster.precise = 0.5;
        f.caster.velocity = Vec3{1.0f, 2.0f, 3.0f};
        f.caster.score = 100;
        f.RunText("caster.attack += 5; caster.speed -= 0.5; caster.precise += 0.25; caster.velocity += vec3(1, 1, 1); caster.score -= int64(caster.attack);");
        EXPECT_EQ(f.caster.attack, 15);
        EXPECT_EQ(f.caster.speed, 1.0f);
        EXPECT_EQ(f.caster.precise, 0.75);
        EXPECT_EQ(f.caster.velocity, (Vec3{2.0f, 3.0f, 4.0f}));
        EXPECT_EQ(f.caster.score, 85) << "the right side reads what the statements before it wrote";

        f.RunText("caster.attack++; caster.attack++; caster.speed--; caster.score++; caster.precise--;");
        EXPECT_EQ(f.caster.attack, 17);
        EXPECT_EQ(f.caster.speed, 0.0f);
        EXPECT_EQ(f.caster.score, 86);
        EXPECT_EQ(f.caster.precise, -0.25);
    }

    TEST(EffectCompilerTest, TheCompoundRightSideIsOneOperandWhateverItsPrecedence) {
        Fixture f;
        f.caster.attack = 20;
        f.RunText("caster.attack -= 2 + 3 * 4;");
        EXPECT_EQ(f.caster.attack, 6) << "x - (2 + 12), not x - 2 + 12";
    }

    TEST(EffectCompilerTest, AnIntegerAddWrapsAsAFormulaDoes) {
        Fixture f;
        f.caster.attack = std::numeric_limits<int32_t>::max();
        f.RunText("caster.attack++;");
        EXPECT_EQ(f.caster.attack, std::numeric_limits<int32_t>::min());
    }

    TEST(EffectCompilerTest, AToggleFlipsABoolAndOnlyABool) {
        Fixture f;
        f.RunText("toggle caster.alive;");
        EXPECT_FALSE(f.caster.alive);
        f.RunText("toggle caster.alive;");
        EXPECT_TRUE(f.caster.alive);
        const ProgramError error = Refusal(f, "toggle caster.attack;");
        EXPECT_TRUE(Contains(error.message, "'toggle' of 'caster.attack'")) << error.message.c_str();
        EXPECT_EQ(error.column, 1U) << "an error of the written part is at the keyword";
    }

    TEST(EffectCompilerTest, AnIfRunsOneBranchAndAnElseIfChainTheFirstTrueOne) {
        Fixture f;
        const EffectScript script =
            f.Must("if (caster.attack > 10) { board.turn = 1; } else if (caster.attack > 5) { board.turn = 2; } else if (caster.attack > 0) { board.turn = 3; } else { board.turn = 4; }");
        for (const auto& [attack, count] : {std::pair{20, 1}, std::pair{8, 2}, std::pair{3, 3}, std::pair{-1, 4}, std::pair{11, 1}, std::pair{10, 2}}) {
            f.caster.attack = attack;
            f.board.turn = 0;
            f.Run(script);
            EXPECT_EQ(f.board.turn, count) << attack;
        }
    }

    TEST(EffectCompilerTest, ABlockSeesWhatAnEarlierStatementWrote) {
        Fixture f;
        f.caster.attack = 0;
        f.board.turn = 0;
        f.RunText("caster.attack = 7; if (caster.attack == 7) { caster.attack += 1; if (caster.attack == 8) { board.turn = 5; } } if (board.turn == 5) { board.turn++; }");
        EXPECT_EQ(f.caster.attack, 8);
        EXPECT_EQ(f.board.turn, 6);
    }

    TEST(EffectCompilerTest, AConditionThatFoldsToAConstantIsNotRemoved) {
        Fixture f;
        const EffectScript script = f.Must("if (1 < 2) { board.turn = 11; } if (2 < 1) { board.turn = 12; }");
        ASSERT_NE(script.Program().Get(), nullptr);
        size_t jumps = 0;
        for (const EffectStep& step : script.Program()->Steps()) {
            jumps += step.op == EffectOp::JumpIfNot ? 1 : 0;
        }
        EXPECT_EQ(jumps, 2U) << script.Program()->Disassemble().c_str();
        f.Run(script);
        EXPECT_EQ(f.board.turn, 11);
    }

    TEST(EffectCompilerTest, AnEmptyScriptRunsNothing) {
        Fixture f;
        const EffectScript script = f.Must("");
        EXPECT_TRUE(script.Empty());
        const EffectResult result = f.Run(script);
        EXPECT_EQ(result.stores + result.emitted, 0U);
    }

    TEST(EffectCompilerTest, AnEmitTakesEveryKindOfArgument) {
        Fixture f;
        f.caster.attack = 4;
        f.board.walls.words[1] = 0xF0;
        f.RunText("emit FxPing(); emit FxCount(caster.attack + 1); emit FxFace(East); emit FxMask(or_mask(board.walls, shift(board.walls, South))); emit FxMixed(3, select(caster.element == Fire, "
                  "West, North), board.walls);");
        const auto commands = f.Buffer().Commands();
        ASSERT_EQ(commands.size(), 5U);
        const EffectsData::EffectToys& toys = EffectsData::RegisterEffectToys();
        EXPECT_EQ(commands[0].id, toys.ping);
        EXPECT_EQ(commands[0].count, 0U);
        EXPECT_EQ(commands[1].id, toys.count);
        EXPECT_EQ(ArgumentOf<int32_t>(commands[1], 0), 5);
        EXPECT_EQ(ArgumentOf<DomainDirection>(commands[2], 0), DomainDirection::East);
        EXPECT_EQ(ArgumentOf<DomainBits256>(commands[3], 0), BitsOr(f.board.walls, BitsShift(f.board.walls, DomainDirection::South)));
        EXPECT_EQ(ArgumentOf<int32_t>(commands[4], 0), 3);
        EXPECT_EQ(ArgumentOf<DomainDirection>(commands[4], 1), DomainDirection::North);
        EXPECT_EQ(ArgumentOf<DomainBits256>(commands[4], 2), f.board.walls);
    }

    TEST(EffectCompilerTest, AnOperationAGameRegisteredWorksInsideAScript) {
        Fixture f;
        f.board.walls.words = {0xFF00, 0x0F, 0, 0};
        const DomainBits256 before = f.board.walls;
        f.RunText("board.walls = and_mask(board.walls, shift(board.walls, East)); board.turn = popcount(board.walls);");
        const DomainBits256 expected = BitsAnd(before, BitsShift(before, DomainDirection::East));
        EXPECT_EQ(f.board.walls, expected);
        EXPECT_EQ(f.board.turn, ProgramDomainData::BitsPopcount(expected));
    }

    TEST(EffectCompilerTest, ARunStoresOnlyThroughAWritableLevel) {
        Fixture f;
        f.context.Set(0, f.caster);
        const EffectScript script = f.Must("caster.attack = 99;");
        EffectResult result;
        {
            const AssertBridge::Capture capture([](const AssertEvent&) {});
            result = f.Run(script);
        }
        EXPECT_EQ(result.refused, 1U);
        EXPECT_NE(f.caster.attack, 99);
    }

    TEST(EffectCompilerTest, TheWriteTargetsOfTheHostDecideWhatLoads) {
        Fixture f;
        ExpectRefusal(f, "cast.level = 1;", "cast.level", "not a path the host allows");
        const ProgramError listed = Refusal(f, "cast.level = 1;");
        EXPECT_TRUE(Contains(listed.message, "caster, board, widths")) << listed.message.c_str();

        WriteTargets narrow;
        ASSERT_TRUE(narrow.Allow("caster.attack"));
        ASSERT_TRUE(narrow.Allow("board"));
        f.options.targets = &narrow;
        EXPECT_TRUE(f.Compile("caster.attack = 1; board.turn = 2;").has_value());
        ExpectRefusal(f, "caster.speed = 1.0;", "caster.speed", "not a path the host allows");

        f.options.targets = nullptr;
        ExpectRefusal(f, "caster.attack = 1;", "caster.attack", "no write targets");
        const WriteTargets none;
        f.options.targets = &none;
        ExpectRefusal(f, "caster.attack = 1;", "caster.attack", "no write targets");
        EXPECT_TRUE(f.Compile("if (caster.alive) {}").has_value()) << "a script that writes nothing needs no target";
        EXPECT_TRUE(f.Compile("emit FxPing();").has_value());
    }

    TEST(EffectCompilerTest, ATargetThatIsNoWritableFieldIsALoadErrorThatSaysWhy) {
        Fixture f;
        ExpectRefusal(f, "caster.attak = 1;", "caster.attak", "did you mean 'attack'");
        ExpectRefusal(f, "casters.attack = 1;", "casters.attack", "unknown level");
        ExpectRefusal(f, "caster = 1;", "caster =", "is a level");
        ExpectRefusal(f, "caster.velocity.x = 1.0;", "caster.velocity.x", "component");
        ExpectRefusal(f, "caster.tint.r = 1;", "caster.tint.r", "a script writes a whole field");
        ExpectRefusal(f, "caster.bounds = 1;", "caster.bounds", "cannot be written");
        ExpectRefusal(f, "caster.span = 1;", "caster.span", "cannot be written");
        ExpectRefusal(f, "caster.buffs = 1;", "caster.buffs", "cannot be written");
        ExpectRefusal(f, "caster.buffs[0] = 1;", "[", "'['");
        EXPECT_TRUE(f.Compile("caster.span.low = 4;").has_value()) << "a field of a sub-object is a whole field";
    }

    TEST(EffectCompilerTest, TheTypeRulesAreThoseOfFormulas) {
        Fixture f;
        ExpectRefusal(f, "caster.alive += 1;", "+=", "'+=' on 'caster.alive'");
        ExpectRefusal(f, "caster.alive++;", "++", "'++' on 'caster.alive'");
        ExpectRefusal(f, "caster.attack = 1.5;", "1.5", "assignment to 'caster.attack'");
        ExpectRefusal(f, "caster.attack = caster.speed;", "caster.speed", "");
        ExpectRefusal(f, "caster.attack += 1.5;", "1.5", "'+=' on 'caster.attack'");
        ExpectRefusal(f, "caster.element += 1;", "+=", "");
        ExpectRefusal(f, "caster.element = Frost;", "Frost", "");
        ExpectRefusal(f, "caster.alive = 1;", "1", "");
        ExpectRefusal(f, "caster.attack = 3 / 0;", "0;", "division by a literal zero");
        ExpectRefusal(f, "if (caster.attack) { }", "caster.attack", "'if' condition");
        ExpectRefusal(f, "caster.attack = nope(1);", "nope", "assignment to 'caster.attack'");
    }

    TEST(EffectCompilerTest, ACommandIsLookedUpAndItsArgumentsCheckedAgainstItsSignature) {
        Fixture f;
        ExpectRefusal(f, "emit FxPign();", "FxPign", "did you mean 'FxPing'");
        const ProgramError unknown = Refusal(f, "emit Nothing();");
        EXPECT_TRUE(Contains(unknown.message, "the commands are: ")) << unknown.message.c_str();
        EXPECT_TRUE(Contains(unknown.message, "FxMixed")) << unknown.message.c_str();
        ExpectRefusal(f, "emit FxCount();", "FxCount", "takes 1 argument(s)");
        ExpectRefusal(f, "emit FxPing(1);", "FxPing", "takes 0 argument(s)");
        ExpectRefusal(f, "emit FxMixed(1, North);", "FxMixed", "takes 3 argument(s) (int, enum {North, East, South, West}, mask)");
        ExpectRefusal(f, "emit FxCount(true);", "true", "'emit FxCount' argument 1");
        ExpectRefusal(f, "emit FxFace(Up);", "Up", "'emit FxFace' argument 1");
        ExpectRefusal(f, "emit FxMixed(1, North, caster.attack);", "caster.attack", "argument 3");
        ExpectRefusal(f, "emit FxCount(1.5);", "1.5", "argument 1");
    }

    TEST(EffectCompilerTest, AnErrorInsideTheUsersTextIsAtItsOwnByteEvenWhenTheCompilerWrappedIt) {
        Fixture f;
        // The column is in the whole script, not in the part the formula compiler saw
        ExpectRefusal(f, "board.turn = 1; caster.attack += caster.atack;", "caster.atack", "did you mean");
        ExpectRefusal(f, "board.turn = 1; caster.attack -= 1 + * 2;", "* 2", "'-=' on 'caster.attack'");
        ExpectRefusal(f, "  if (caster.alive) {\n    caster.attack = caster.atack + 1;\n  }", "caster.atack", "did you mean");
        ExpectRefusal(f, "emit FxMixed(1, North, board.wals);", "board.wals", "did you mean");
    }

    TEST(EffectCompilerTest, AnUnbalancedParenthesisInACompoundIsNotHiddenByTheWrapper) {
        Fixture f;
        // `caster.attack + (1) + (2)` would compile if the compiler only looked at the whole
        const ProgramError error = Refusal(f, "caster.attack += 1) + (2;");
        EXPECT_GE(error.column, ColumnOf("caster.attack += 1) + (2;", "1)"));
        EXPECT_LE(error.column, static_cast<uint32_t>(StringView("caster.attack += 1) + (2;").size()));
        ExpectRefusal(f, "caster.attack -= (1;", ";", "'-=' on 'caster.attack'");
    }

    TEST(EffectCompilerTest, ATextTheScriptParserRefusesKeepsItsColumn) {
        Fixture f;
        const StringView missing = "caster.attack = 1";
        const ProgramError error = Refusal(f, missing);
        EXPECT_EQ(error.column, missing.size() + 1) << "a missing ';' is at the end of the text";
        EXPECT_TRUE(Contains(error.message, "';'")) << error.message.c_str();
        ExpectRefusal(f, "else { }", "else", "else");
    }

    TEST(EffectCompilerTest, ARefusedTextAddsNothingToThePool) {
        Fixture f;
        const size_t before = ProgramPool::Count();
        // The first statements are programs nobody has compiled; the last statement is the error
        const auto refused = f.Compile("caster.attack = caster.attack * 7919 + 13; if (caster.speed * 31.5 > 2.25) { board.turn = board.turn * 4093; } caster.attack = 1.5;");
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(ProgramPool::Count(), before);
        const auto toggled = f.Compile("caster.attack = caster.attack * 7919 + 13; toggle caster.attack;");
        ASSERT_FALSE(toggled.has_value());
        EXPECT_EQ(ProgramPool::Count(), before);
    }

    TEST(EffectCompilerTest, TheSameTextCompiledTwiceBehavesTheSameAndAddsNoProgram) {
        Fixture f;
        const StringView text = "if (caster.attack * 6151 > 100) { caster.attack = caster.attack * 6151 - 7; } else { board.turn += 6151; }";
        const EffectScript first = f.Must(text);
        const size_t afterFirst = ProgramPool::Count();
        const EffectScript second = f.Must(text);
        EXPECT_EQ(ProgramPool::Count(), afterFirst);
        EXPECT_NE(first.Program().Get(), second.Program().Get());
        EXPECT_TRUE(first == second);
        EXPECT_EQ(first.Source(), text);
        EXPECT_FALSE(first == f.Must("board.turn = 1;"));
        EXPECT_FALSE(first == EffectScript());
        EXPECT_TRUE(EffectScript() == EffectScript());

        for (const int32_t attack : {-4, 0, 17, 1000}) {
            f.caster.attack = attack;
            f.board.turn = 1;
            f.Run(first);
            const int32_t attackAfterFirst = f.caster.attack;
            const uint32_t countAfterFirst = f.board.turn;
            f.caster.attack = attack;
            f.board.turn = 1;
            f.Run(second);
            EXPECT_EQ(f.caster.attack, attackAfterFirst);
            EXPECT_EQ(f.board.turn, countAfterFirst);
        }
    }

    TEST(EffectCompilerTest, TheReadsAndWritesAreTheLevelsTheScriptTouches) {
        Fixture f;
        const EffectScript script = f.Must("if (cast.level > 1) { board.turn = caster.attack; }");
        EXPECT_EQ(script.Reads(), LevelBit(0) | LevelBit(1));
        EXPECT_EQ(script.Writes(), LevelBit(2));
        EXPECT_EQ(EffectScript().Reads(), 0U);
    }

    TEST(EffectCompilerTest, TheOptionsReachTheFormulas) {
        Fixture f;
        ValueTableSet tables;
        tables.Add("Steps", {10.0, 20.0, 30.0});
        EXPECT_FALSE(f.Compile("caster.attack = int(ByLevel('Steps', 1));").has_value()) << "no tables: the formula's own error";
        f.options.tables = &tables;
        f.options.fuse = false;
        f.RunText("caster.attack = int(ByLevel('Steps', 1));");
        EXPECT_EQ(f.caster.attack, 20);
    }

    TEST(EffectCompilerTest, ACompiledScriptDoesWhatTheEquivalentCppDoes) {
        Fixture f;
        const EffectsData::EffectToys& toys = EffectsData::RegisterEffectToys();
        const EffectScript script = f.Must("if (caster.attack > 10) { caster.attack -= 3; if (caster.alive) { caster.score += int64(caster.attack); } else { toggle caster.alive; } } "
                                           "else if (caster.speed > 1.5) { caster.speed = caster.speed * 2.0; emit FxCount(caster.attack); } "
                                           "else { caster.attack++; emit FxMask(or_mask(board.walls, shift(board.walls, North))); } "
                                           "board.turn += 1; emit FxFace(East);");

        Rng rng(0xEFFEC7);
        for (uint32_t i = 0; i < kGeneratedContexts; ++i) {
            f.caster.attack = static_cast<int32_t>(rng.Next32() % 61U) - 20;
            f.caster.speed = static_cast<float>(rng.Next32() % 17U) * 0.25f;
            f.caster.alive = (rng.Next32() & 1U) != 0;
            f.caster.score = static_cast<int64_t>(rng.Next32() % 1000U);
            f.board.turn = static_cast<int32_t>(rng.Next32() % 100U);
            for (uint64_t& word : f.board.walls.words) {
                word = rng.Next64();
            }

            // The reference: the script written as C++ over copies
            ValueUnitStats caster = f.caster;
            DomainBoard board = f.board;
            bool emittedCount = false;
            bool emittedMask = false;
            if (caster.attack > 10) {
                caster.attack = static_cast<int32_t>(static_cast<uint32_t>(caster.attack) - 3U);
                if (caster.alive) {
                    caster.score += static_cast<int64_t>(caster.attack);
                } else {
                    caster.alive = !caster.alive;
                }
            } else if (caster.speed > 1.5f) {
                caster.speed = caster.speed * 2.0f;
                emittedCount = true;
            } else {
                caster.attack = static_cast<int32_t>(static_cast<uint32_t>(caster.attack) + 1U);
                emittedMask = true;
            }
            const int32_t countArgument = caster.attack;
            const DomainBits256 maskArgument = BitsOr(board.walls, BitsShift(board.walls, DomainDirection::North));
            board.turn += 1;

            f.Buffer().Clear();
            f.Run(script);
            EXPECT_EQ(f.caster.attack, caster.attack) << i;
            EXPECT_EQ(f.caster.speed, caster.speed) << i;
            EXPECT_EQ(f.caster.alive, caster.alive) << i;
            EXPECT_EQ(f.caster.score, caster.score) << i;
            EXPECT_EQ(f.board.turn, board.turn) << i;

            const auto commands = f.Buffer().Commands();
            size_t at = 0;
            if (emittedCount) {
                ASSERT_LT(at, commands.size()) << i;
                EXPECT_EQ(commands[at].id, toys.count);
                EXPECT_EQ(commands[at].Argument<int32_t>(0), countArgument) << i;
                ++at;
            }
            if (emittedMask) {
                ASSERT_LT(at, commands.size()) << i;
                EXPECT_EQ(commands[at].id, toys.mask);
                EXPECT_EQ(commands[at].Argument<DomainBits256>(0), maskArgument) << i;
                ++at;
            }
            ASSERT_EQ(commands.size(), at + 1) << i;
            EXPECT_EQ(commands[at].id, toys.face);
            EXPECT_EQ(commands[at].Argument<DomainDirection>(0), DomainDirection::East);
        }
    }

}  // namespace BECore::Tests
