#include "NativeProgramSetLists.h"

#include <BECore/ProgramListTypes.h>
#include <BECore/ProgramRowTypes.h>
#include <BECore/Values/Programs/Lists/ListImage.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <NativePrograms/NativeProgramSet.h>

namespace BECore::Tests {

    using namespace ProgramListData;
    using ProgramRowData::ProgramRowOperands;

    namespace {

        constexpr uint8_t kSuitBytes = sizeof(ProgramListSuit);

        template <typename Object, typename Field>
        uint32_t OffsetOf(const Object& object, const Field& field) {
            return static_cast<uint32_t>(reinterpret_cast<const std::byte*>(&field) - reinterpret_cast<const std::byte*>(&object));
        }

        /// The offsets of the fields are the same in every object of a type, so a default one tells them
        template <typename T>
        const T& Probe() {
            static const T probe{};
            return probe;
        }

        /// The descriptor of the list field @p field of the probe object @p object; a list that does not describe is a hole in the set, so the error is kept
        template <typename Object, typename List>
        std::expected<ListDescriptor, String> DescriptorOf(const Object& object, const List& field) {
            return MakeListDescriptor(OffsetOf(object, field), DescribeType<List>());
        }

        /// Assembles one program: the registers are taken in order and never given back, apart from the temporaries of a loop body
        class ListAssembler {
        public:
            explicit ListAssembler(String name) : _name(std::move(name)) {}

            [[nodiscard]] uint8_t Acquire() {
                return _builder.AcquireRegister().value_or(0);
            }

            [[nodiscard]] ProgramBuilder& Builder() {
                return _builder;
            }

            /// A register loaded from the `i32` of the operands at `kOperandsLevelA`: a threshold or an index that changes from case to case
            [[nodiscard]] uint8_t LoadCaseInt() {
                const uint8_t reg = Acquire();
                _builder.EmitLoadSlot(reg, ProgramType::Int32, SlotRef{kOperandsLevelA, OffsetOf(Probe<ProgramRowOperands>(), Probe<ProgramRowOperands>().i32)});
                return reg;
            }

            [[nodiscard]] uint8_t ConstInt(int32_t value) {
                const uint8_t reg = Acquire();
                _builder.EmitLoadConst(reg, value);
                return reg;
            }

            [[nodiscard]] uint8_t LoadElem(ProgramType type, uint8_t element, uint32_t offset, uint8_t enumBytes = 0) {
                const uint8_t reg = Acquire();
                _builder.EmitLoadElem(reg, type, element, offset, enumBytes);
                return reg;
            }

            void Release(uint8_t reg) {
                _builder.ReleaseRegister(reg);
            }

            /// Interns the program with its result in @p resultRegister and appends it to @p programs unless it is there already
            std::expected<void, String> Finish(ProgramType resultType, uint8_t resultRegister, Vector<ProgramId>& programs) {
                auto program = _builder.Finish(resultType, resultRegister, PoolString::Intern(_name));
                if (!program.has_value()) {
                    return std::unexpected(Format("{}: {}", _name, program.error().message));
                }
                const Optional<ProgramId> id = ProgramPool::Intern(std::move(*program));
                if (!id.has_value()) {
                    return std::unexpected(Format("{}: the program pool is full", _name));
                }
                if (std::find(programs.begin(), programs.end(), *id) == programs.end()) {
                    programs.push_back(*id);
                }
                return {};
            }

        private:
            String _name;
            ProgramBuilder _builder;
        };

        /// The first error of a run of steps
        class Steps {
        public:
            void Add(std::expected<void, String> result) {
                if (!result.has_value() && _error.empty()) {
                    _error = std::move(result.error());
                }
            }

            [[nodiscard]] std::expected<void, String> Result() {
                if (!_error.empty()) {
                    return std::unexpected(std::move(_error));
                }
                return {};
            }

        private:
            String _error;
        };

        /// `acc = identity; for element in list: body(element, acc)`, the result being `acc`; the body is a function of the assembler, the element and the accumulator that emits its code
        template <typename T, typename Body>
        std::expected<void, String> Fold(StringView name, const std::expected<ListDescriptor, String>& list, T identity, Body&& body, Vector<ProgramId>& programs) {
            if (!list.has_value()) {
                return std::unexpected(Format("{}: {}", name, list.error()));
            }
            ListAssembler assembler{String(name)};
            const uint8_t listReg = assembler.Acquire();
            const uint8_t element = assembler.Acquire();
            const uint8_t acc = assembler.Acquire();
            assembler.Builder().EmitListSlot(listReg, kListsLevel, *list);
            assembler.Builder().EmitLoadConst(acc, identity);
            const size_t begin = assembler.Builder().EmitLoopBegin(element, listReg);
            body(assembler, element, acc);
            assembler.Builder().EmitLoopEnd(begin);
            return assembler.Finish(kProgramTypeOf<T>, acc, programs);
        }

        /// `acc = op(acc, field)` over a field of type @p type at @p offset; @p breakOn leaves the loop when the accumulator (a bool) is that
        auto FieldBody(ProgramOperation op, ProgramType type, uint32_t offset, Optional<bool> breakOn = std::nullopt, uint8_t enumBytes = 0) {
            return [=](ListAssembler& assembler, uint8_t element, uint8_t acc) {
                const uint8_t value = assembler.LoadElem(type, element, offset, enumBytes);
                assembler.Builder().Emit(OpcodeOf(op, type), acc, acc, value);
                if (breakOn.has_value()) {
                    assembler.Builder().EmitBreakIf(acc, *breakOn);
                }
                assembler.Release(value);
            };
        }

        /// `acc += (field of type @p type at @p offset  op  a constant or the case integer) ? 1 : 0`, the comparison being @p compare
        template <typename Operand>
        auto CountBody(ProgramOperation compare, ProgramType type, uint32_t offset, Operand operand, uint8_t enumBytes = 0) {
            return [=](ListAssembler& assembler, uint8_t element, uint8_t acc) {
                const uint8_t field = assembler.LoadElem(type, element, offset, enumBytes);
                const uint8_t wanted = operand(assembler);
                const uint8_t hit = assembler.Acquire();
                assembler.Builder().Emit(OpcodeOf(compare, type), hit, field, wanted);
                const uint8_t one = assembler.ConstInt(1);
                const uint8_t zero = assembler.ConstInt(0);
                const uint8_t chosen = assembler.Acquire();
                assembler.Builder().Emit(OpcodeOf(ProgramOperation::Select, ProgramType::Int32), chosen, hit, one, zero);
                assembler.Builder().Emit(OpcodeOf(ProgramOperation::Add, ProgramType::Int32), acc, acc, chosen);
                for (const uint8_t reg : {chosen, zero, one, hit, wanted, field}) {
                    assembler.Release(reg);
                }
            };
        }

        std::expected<void, String> AssembleCounts(Vector<ProgramId>& programs) {
            const ProgramListHand& hand = Probe<ProgramListHand>();
            const ProgramListTile& tile = Probe<ProgramListTile>();
            const ProgramListSet& set = Probe<ProgramListSet>();
            const auto tiles = DescriptorOf(hand, hand.tiles);
            const auto sets = DescriptorOf(hand, hand.sets);
            const auto caseInt = [](ListAssembler& assembler) { return assembler.LoadCaseInt(); };
            Steps steps;
            steps.Add(Fold("ListCountTerminal", tiles, int32_t{0},
                           CountBody(ProgramOperation::Eq, ProgramType::Bool, OffsetOf(tile, tile.terminal),
                                     [](ListAssembler& assembler) {
                                         const uint8_t reg = assembler.Acquire();
                                         assembler.Builder().EmitLoadConst(reg, true);
                                         return reg;
                                     }),
                           programs));
            steps.Add(Fold("ListCountRankAboveCase", tiles, int32_t{0}, CountBody(ProgramOperation::Gt, ProgramType::Int32, OffsetOf(tile, tile.rank), caseInt), programs));
            steps.Add(Fold("ListCountWinds", tiles, int32_t{0},
                           CountBody(
                               ProgramOperation::Eq, ProgramType::Enum, OffsetOf(tile, tile.suit),
                               [](ListAssembler& assembler) {
                                   const uint8_t reg = assembler.Acquire();
                                   assembler.Builder().EmitLoadConst(reg, ProgramListSuit::Winds);
                                   return reg;
                               },
                               kSuitBytes),
                           programs));
            // The text to find is a field of the case: a `PoolString` constant is an address that is another one in the next process, and such a program is never bound
            steps.Add(Fold("ListCountNameOfCase", tiles, int32_t{0},
                           CountBody(ProgramOperation::Eq, ProgramType::String, OffsetOf(tile, tile.name),
                                     [](ListAssembler& assembler) {
                                         const uint8_t reg = assembler.Acquire();
                                         assembler.Builder().EmitLoadSlot(reg, ProgramType::String, SlotRef{kOperandsLevelA, OffsetOf(Probe<ProgramRowOperands>(), Probe<ProgramRowOperands>().text)});
                                         return reg;
                                     }),
                           programs));
            steps.Add(Fold("ListCountPungs", sets, int32_t{0},
                           CountBody(ProgramOperation::Eq, ProgramType::Bool, OffsetOf(set, set.pung),
                                     [](ListAssembler& assembler) {
                                         const uint8_t reg = assembler.Acquire();
                                         assembler.Builder().EmitLoadConst(reg, true);
                                         return reg;
                                     }),
                           programs));
            return steps.Result();
        }

        std::expected<void, String> AssembleFolds(Vector<ProgramId>& programs) {
            const ProgramListHand& hand = Probe<ProgramListHand>();
            const ProgramListTile& tile = Probe<ProgramListTile>();
            constexpr float kInf = std::numeric_limits<float>::infinity();
            const auto tiles = DescriptorOf(hand, hand.tiles);
            const auto counts = DescriptorOf(hand, hand.counts);
            const auto weights = DescriptorOf(hand, hand.weights);
            const auto flags = DescriptorOf(hand, hand.flags);
            Steps steps;
            steps.Add(Fold("ListSumRanks", tiles, int32_t{0}, FieldBody(ProgramOperation::Add, ProgramType::Int32, OffsetOf(tile, tile.rank)), programs));
            steps.Add(Fold("ListSumWeights", tiles, 0.0f, FieldBody(ProgramOperation::Add, ProgramType::Float, OffsetOf(tile, tile.weight)), programs));
            steps.Add(Fold("ListSumPositions", tiles, Vec3{}, FieldBody(ProgramOperation::Add, ProgramType::Vec3, OffsetOf(tile, tile.position)), programs));
            steps.Add(Fold("ListSumCounts", counts, int32_t{0}, FieldBody(ProgramOperation::Add, ProgramType::Int32, 0), programs));
            // The elements hold NaN: a minimum skips it, and over a list of NaN alone it is the identity
            steps.Add(Fold("ListMinTileWeight", tiles, kInf, FieldBody(ProgramOperation::Min, ProgramType::Float, OffsetOf(tile, tile.weight)), programs));
            steps.Add(Fold("ListMaxTileWeight", tiles, -kInf, FieldBody(ProgramOperation::Max, ProgramType::Float, OffsetOf(tile, tile.weight)), programs));
            steps.Add(Fold("ListMinWeights", weights, kInf, FieldBody(ProgramOperation::Min, ProgramType::Float, 0), programs));
            steps.Add(Fold("ListMaxWeights", weights, -kInf, FieldBody(ProgramOperation::Max, ProgramType::Float, 0), programs));
            steps.Add(Fold("ListMinRank", tiles, std::numeric_limits<int32_t>::max(), FieldBody(ProgramOperation::Min, ProgramType::Int32, OffsetOf(tile, tile.rank)), programs));
            steps.Add(Fold("ListAnyFlag", flags, false, FieldBody(ProgramOperation::Or, ProgramType::Bool, 0, true), programs));
            steps.Add(Fold("ListAllFlag", flags, true, FieldBody(ProgramOperation::And, ProgramType::Bool, 0, false), programs));
            steps.Add(Fold("ListAnyTerminal", tiles, false, FieldBody(ProgramOperation::Or, ProgramType::Bool, OffsetOf(tile, tile.terminal), true), programs));
            steps.Add(Fold("ListAllTerminalNoBreak", tiles, true, FieldBody(ProgramOperation::And, ProgramType::Bool, OffsetOf(tile, tile.terminal)), programs));
            return steps.Result();
        }

        /// `any(tiles, rank > case)` and `all(tiles, rank < case)`, each with the early exit, over a threshold that changes from case to case
        std::expected<void, String> AssembleBreaks(Vector<ProgramId>& programs) {
            const ProgramListHand& hand = Probe<ProgramListHand>();
            const ProgramListTile& tile = Probe<ProgramListTile>();
            const auto tiles = DescriptorOf(hand, hand.tiles);
            if (!tiles.has_value()) {
                return std::unexpected(tiles.error());
            }
            Steps steps;
            for (const bool all : {false, true}) {
                ListAssembler assembler{all ? "ListAllRankBelowCase" : "ListAnyRankAboveCase"};
                const uint8_t limit = assembler.LoadCaseInt();
                const uint8_t listReg = assembler.Acquire();
                const uint8_t element = assembler.Acquire();
                const uint8_t answer = assembler.Acquire();
                assembler.Builder().EmitListSlot(listReg, kListsLevel, *tiles);
                assembler.Builder().EmitLoadConst(answer, all);
                const size_t begin = assembler.Builder().EmitLoopBegin(element, listReg);
                const uint8_t rank = assembler.LoadElem(ProgramType::Int32, element, OffsetOf(tile, tile.rank));
                const uint8_t holds = assembler.Acquire();
                assembler.Builder().Emit(OpcodeOf(all ? ProgramOperation::Lt : ProgramOperation::Gt, ProgramType::Int32), holds, rank, limit);
                assembler.Builder().Emit(OpcodeOf(all ? ProgramOperation::And : ProgramOperation::Or, ProgramType::Bool), answer, answer, holds);
                assembler.Builder().EmitBreakIf(answer, !all);
                assembler.Release(holds);
                assembler.Release(rank);
                assembler.Builder().EmitLoopEnd(begin);
                steps.Add(assembler.Finish(ProgramType::Bool, answer, programs));
            }
            return steps.Result();
        }

        /// A loop in a loop: the tiles of every set summed, and the tiles of the set at a case index (a set that is not there has an empty list)
        std::expected<void, String> AssembleNested(Vector<ProgramId>& programs) {
            const ProgramListHand& hand = Probe<ProgramListHand>();
            const ProgramListTile& tile = Probe<ProgramListTile>();
            const ProgramListSet& set = Probe<ProgramListSet>();
            const auto sets = DescriptorOf(hand, hand.sets);
            const auto setTiles = DescriptorOf(set, set.tiles);
            if (!sets.has_value() || !setTiles.has_value()) {
                return std::unexpected(sets.has_value() ? setTiles.error() : sets.error());
            }
            Steps steps;
            {
                ListAssembler assembler{"ListSumRanksOfSets"};
                const uint8_t outerList = assembler.Acquire();
                const uint8_t setElement = assembler.Acquire();
                const uint8_t acc = assembler.Acquire();
                const uint8_t innerList = assembler.Acquire();
                const uint8_t tileElement = assembler.Acquire();
                assembler.Builder().EmitListSlot(outerList, kListsLevel, *sets);
                assembler.Builder().EmitLoadConst(acc, int32_t{0});
                const size_t outer = assembler.Builder().EmitLoopBegin(setElement, outerList);
                assembler.Builder().EmitListElem(innerList, setElement, *setTiles);
                const size_t inner = assembler.Builder().EmitLoopBegin(tileElement, innerList);
                const uint8_t rank = assembler.LoadElem(ProgramType::Int32, tileElement, OffsetOf(tile, tile.rank));
                assembler.Builder().Emit(OpcodeOf(ProgramOperation::Add, ProgramType::Int32), acc, acc, rank);
                assembler.Release(rank);
                assembler.Builder().EmitLoopEnd(inner);
                assembler.Builder().EmitLoopEnd(outer);
                steps.Add(assembler.Finish(ProgramType::Int32, acc, programs));
            }
            {
                ListAssembler assembler{"ListSumRanksOfSetAtCase"};
                const uint8_t index = assembler.LoadCaseInt();
                const uint8_t setsList = assembler.Acquire();
                const uint8_t setElement = assembler.Acquire();
                const uint8_t acc = assembler.Acquire();
                const uint8_t tilesList = assembler.Acquire();
                const uint8_t tileElement = assembler.Acquire();
                assembler.Builder().EmitListSlot(setsList, kListsLevel, *sets);
                assembler.Builder().EmitListAt(setElement, setsList, index);
                assembler.Builder().EmitLoadConst(acc, int32_t{0});
                assembler.Builder().EmitListElem(tilesList, setElement, *setTiles);
                const size_t loop = assembler.Builder().EmitLoopBegin(tileElement, tilesList);
                const uint8_t rank = assembler.LoadElem(ProgramType::Int32, tileElement, OffsetOf(tile, tile.rank));
                assembler.Builder().Emit(OpcodeOf(ProgramOperation::Add, ProgramType::Int32), acc, acc, rank);
                assembler.Release(rank);
                assembler.Builder().EmitLoopEnd(loop);
                steps.Add(assembler.Finish(ProgramType::Int32, acc, programs));
            }
            return steps.Result();
        }

        /// The size of one list, and a field of the element at the case index of the tiles
        std::expected<void, String> AssembleSizesAndElements(Vector<ProgramId>& programs) {
            const ProgramListHand& hand = Probe<ProgramListHand>();
            const ProgramListTile& tile = Probe<ProgramListTile>();
            Steps steps;
            const auto size = [&](StringView name, const std::expected<ListDescriptor, String>& list) {
                if (!list.has_value()) {
                    steps.Add(std::unexpected(Format("{}: {}", name, list.error())));
                    return;
                }
                ListAssembler assembler{String(name)};
                const uint8_t listReg = assembler.Acquire();
                const uint8_t dst = assembler.Acquire();
                assembler.Builder().EmitListSlot(listReg, kListsLevel, *list);
                assembler.Builder().EmitListSize(dst, listReg);
                steps.Add(assembler.Finish(ProgramType::Int32, dst, programs));
            };
            size("ListSizeTiles", DescriptorOf(hand, hand.tiles));
            size("ListSizeSets", DescriptorOf(hand, hand.sets));
            size("ListSizeCounts", DescriptorOf(hand, hand.counts));
            size("ListSizeWeights", DescriptorOf(hand, hand.weights));
            size("ListSizeFlags", DescriptorOf(hand, hand.flags));
            size("ListSizeNone", DescriptorOf(hand, hand.none));

            const auto tiles = DescriptorOf(hand, hand.tiles);
            if (!tiles.has_value()) {
                steps.Add(std::unexpected(tiles.error()));
                return steps.Result();
            }
            const auto at = [&](StringView name, ProgramType type, uint32_t offset, uint8_t enumBytes) {
                ListAssembler assembler{String(name)};
                const uint8_t index = assembler.LoadCaseInt();
                const uint8_t listReg = assembler.Acquire();
                const uint8_t element = assembler.Acquire();
                assembler.Builder().EmitListSlot(listReg, kListsLevel, *tiles);
                assembler.Builder().EmitListAt(element, listReg, index);
                const uint8_t value = assembler.LoadElem(type, element, offset, enumBytes);
                steps.Add(assembler.Finish(type, value, programs));
            };
            at("ListAtRank", ProgramType::Int32, OffsetOf(tile, tile.rank), 0);
            at("ListAtTerminal", ProgramType::Bool, OffsetOf(tile, tile.terminal), 0);
            at("ListAtSuit", ProgramType::Enum, OffsetOf(tile, tile.suit), kSuitBytes);
            at("ListAtWeight", ProgramType::Float, OffsetOf(tile, tile.weight), 0);
            at("ListAtPosition", ProgramType::Vec3, OffsetOf(tile, tile.position), 0);
            at("ListAtName", ProgramType::String, OffsetOf(tile, tile.name), 0);
            return steps.Result();
        }

    }  // namespace

    std::expected<void, String> AssembleListPrograms(Vector<ProgramId>& programs) {
        Steps steps;
        steps.Add(AssembleCounts(programs));
        steps.Add(AssembleFolds(programs));
        steps.Add(AssembleBreaks(programs));
        steps.Add(AssembleNested(programs));
        steps.Add(AssembleSizesAndElements(programs));
        return steps.Result();
    }

}  // namespace BECore::Tests
