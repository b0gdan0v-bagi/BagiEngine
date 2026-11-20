// `CallDomain` and the register type `Domain` in the interpreter (Values/Programs/Domain/CLAUDE.md): programs assembled by hand over the toy mask of `ProgramDomainTypes.h`.

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/ProgramDomainTypes.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ProgramDomainData;

    namespace {

        template <typename T>
        [[nodiscard]] ProgramDomainValue ImageOf(const T& value) {
            static_assert(sizeof(T) <= kProgramRegisterBytes);
            ProgramDomainValue image;
            std::memcpy(image.bytes.data(), &value, sizeof(T));
            return image;
        }

        [[nodiscard]] DomainBits256 PatternMask(uint64_t seed) {
            DomainBits256 mask;
            for (size_t i = 0; i < mask.words.size(); ++i) {
                mask.words[i] = (seed + i) * 0x9E3779B97F4A7C15ULL;
            }
            return mask;
        }

        /// A context with a `DomainBoard` at level 0
        struct BoardWorld {
            ContextDecl declaration;
            DomainBoard board;
            Context context;

            BoardWorld() {
                EXPECT_TRUE(declaration.Add<DomainBoard>("board").has_value());
                context = Context(declaration);
                context.Set(0, board);
            }

            [[nodiscard]] SlotRef Link(StringView path, const TypeDesc& type) const {
                const auto slot = LinkSlot(declaration, path, type);
                EXPECT_TRUE(slot.has_value()) << String(path).c_str();
                return slot.has_value() ? *slot : SlotRef{};
            }
        };

        /// A program that calls @p operation on constants of the given images and returns its result
        struct CallBuilder {
            ProgramBuilder builder;

            uint8_t Constant(const ProgramDomainValue& image) {
                const uint8_t reg = Require(builder.AcquireRegister());
                builder.EmitLoadConst(reg, image);
                return reg;
            }

            template <typename T>
            uint8_t Scalar(T value) {
                const uint8_t reg = Require(builder.AcquireRegister());
                builder.EmitLoadConst(reg, value);
                return reg;
            }
        };

        /// Registered once per process: a context function that declares the level it reads
        [[nodiscard]] DomainOperationId WallsThatDeclareTheirLevel() {
            static const DomainOperationId id = [] {
                static_cast<void>(RegisterDomainToys());
                return Require(ProgramDomain::AddOperation<&BoardWalls>("walls_declared", DomainOperationOptions{LevelBit(0)}));
            }();
            return id;
        }

        template <typename T>
        [[nodiscard]] T RunOf(const Program& program, const Context& context) {
            return Run<T>(program, context);
        }

        [[nodiscard]] size_t CountOf(const Program& program, Opcode op) {
            return static_cast<size_t>(std::count_if(program.Code().begin(), program.Code().end(), [op](const Instruction& ins) { return ins.op == op; }));
        }

    }  // namespace

    TEST(ProgramDomainRunTest, AFormulaOverTheBoardRunsAsTheCppDoes) {
        const DomainToys& toys = RegisterDomainToys();
        BoardWorld world;
        world.board.walls = PatternMask(3);
        const SlotRef walls = world.Link("board.walls", DescribeType<DomainBits256>());

        // popcount(and_mask(walls, shift(walls, East)))
        ProgramBuilder builder;
        const uint8_t first = Require(builder.AcquireRegister());
        const uint8_t second = Require(builder.AcquireRegister());
        const uint8_t direction = Require(builder.AcquireRegister());
        builder.EmitLoadSlot(first, ProgramType::Domain, walls, sizeof(DomainBits256));
        builder.EmitLoadSlot(second, ProgramType::Domain, walls, sizeof(DomainBits256));
        builder.EmitLoadConst(direction, ProgramEnum{static_cast<uint32_t>(DomainDirection::East)});
        builder.EmitCallDomain(second, toys.shift, second, direction);
        builder.EmitCallDomain(first, toys.bitsAnd, first, second);
        builder.EmitCallDomain(first, toys.popcount, first);
        const Program program = Require(builder.Finish(ProgramType::Int32, first));

        EXPECT_EQ(RunOf<int32_t>(program, world.context), BitsPopcount(BitsAnd(world.board.walls, BitsShift(world.board.walls, DomainDirection::East))));
        world.board.walls = PatternMask(11);
        EXPECT_EQ(RunOf<int32_t>(program, world.context), BitsPopcount(BitsAnd(world.board.walls, BitsShift(world.board.walls, DomainDirection::East))));
    }

    TEST(ProgramDomainRunTest, EveryToyOperationThroughTheInterpreterIsTheCppFunction) {
        const DomainToys& toys = RegisterDomainToys();
        BoardWorld world;
        world.board.walls = PatternMask(5);
        const DomainBits256 a = PatternMask(1);
        const DomainBits256 b = PatternMask(9);
        const DomainBits256 c = PatternMask(21);

        const auto domainResult = [&](DomainOperationId operation, auto... arguments) {
            CallBuilder call;
            Array<uint8_t, 3> registers{};
            size_t next = 0;
            ((registers[next++] = call.Constant(arguments)), ...);
            const uint8_t dst = Require(call.builder.AcquireRegister());
            call.builder.EmitCallDomain(dst, operation, registers[0], registers[1], registers[2]);
            return RunOf<ProgramDomainValue>(Require(call.builder.Finish(ProgramType::Domain, dst)), world.context);
        };

        EXPECT_EQ(domainResult(toys.bitsAnd, ImageOf(a), ImageOf(b)), ImageOf(BitsAnd(a, b)));
        EXPECT_EQ(domainResult(toys.bitsOr, ImageOf(a), ImageOf(b)), ImageOf(BitsOr(a, b)));
        EXPECT_EQ(domainResult(toys.majority, ImageOf(a), ImageOf(b), ImageOf(c)), ImageOf(BitsMajority(a, b, c)));
        for (const DomainDirection direction : EnumUtils<DomainDirection>::Values()) {
            EXPECT_EQ(domainResult(toys.shift, ImageOf(a), ImageOf(ProgramEnum{static_cast<uint32_t>(direction)})), ImageOf(BitsShift(a, direction)));
        }
        EXPECT_EQ(domainResult(toys.walls), ImageOf(world.board.walls)) << "an operation of no parameters reads the context";
        EXPECT_EQ(domainResult(toys.bitsAnd, ImageOf(a), ImageOf(DomainBits256{})), ImageOf(DomainBits256{}));
    }

    TEST(ProgramDomainRunTest, ScalarArgumentsAndResultsPassThroughTheirRegisters) {
        const DomainToys& toys = RegisterDomainToys();
        BoardWorld world;

        CallBuilder bit;
        const uint8_t index = bit.Scalar(int32_t{70});
        const uint8_t bitDst = Require(bit.builder.AcquireRegister());
        bit.builder.EmitCallDomain(bitDst, toys.bit, index);
        EXPECT_EQ(RunOf<ProgramDomainValue>(Require(bit.builder.Finish(ProgramType::Domain, bitDst)), world.context), ImageOf(BitsBit(70)));

        CallBuilder cell;
        const uint8_t x = cell.Scalar(int32_t{4});
        const uint8_t y = cell.Scalar(int32_t{9});
        const uint8_t cellDst = Require(cell.builder.AcquireRegister());
        cell.builder.EmitCallDomain(cellDst, toys.cell, x, y);
        EXPECT_EQ(RunOf<ProgramDomainValue>(Require(cell.builder.Finish(ProgramType::Domain, cellDst)), world.context), ImageOf(BitsCell(4, 9)));

        CallBuilder any;
        const uint8_t mask = any.Constant(ImageOf(BitsBit(3)));
        any.builder.EmitCallDomain(mask, toys.any, mask);
        EXPECT_TRUE(RunOf<bool>(Require(any.builder.Finish(ProgramType::Bool, mask)), world.context));
    }

    TEST(ProgramDomainRunTest, ASlotOfARegisteredTypeLoadsItsBytesAndZeroesTheRestOfTheRegister) {
        BoardWorld world;
        world.board.walls = PatternMask(2);
        world.board.score = DomainPair{-5, 77};
        const SlotRef walls = world.Link("board.walls", DescribeType<DomainBits256>());
        const SlotRef score = world.Link("board.score", DescribeType<DomainPair>());

        Array<RegisterImage, kMaxProgramRegisters> registers;
        for (RegisterImage& image : registers) {
            image.bytes.fill(0xEE);
        }
        Instruction load{Opcode::LoadSlotDomain, 0, walls.level, sizeof(DomainBits256), 0, walls.offset};
        Kernel::Execute<Kernel::LoadSlot, ProgramDomainValue>(registers.data(), load, world.context);
        EXPECT_EQ(std::memcmp(registers[0].bytes.data(), &world.board.walls, sizeof(DomainBits256)), 0) << "the whole register of a 32-byte type";

        load = Instruction{Opcode::LoadSlotDomain, 1, score.level, sizeof(DomainPair), 0, score.offset};
        Kernel::Execute<Kernel::LoadSlot, ProgramDomainValue>(registers.data(), load, world.context);
        EXPECT_EQ(std::memcmp(registers[1].bytes.data(), &world.board.score, sizeof(DomainPair)), 0);
        for (size_t i = sizeof(DomainPair); i < kProgramRegisterBytes; ++i) {
            EXPECT_EQ(registers[1].bytes[i], 0) << "byte " << i << ": a smaller type leaves no leftovers of the register";
        }
    }

    TEST(ProgramDomainRunTest, EqualityAndSelectCompareTheWholeImage) {
        BoardWorld world;
        const auto compare = [&](Opcode op, const ProgramDomainValue& left, const ProgramDomainValue& right) {
            CallBuilder call;
            const uint8_t l = call.Constant(left);
            const uint8_t r = call.Constant(right);
            call.builder.Emit(op, l, l, r);
            return RunOf<bool>(Require(call.builder.Finish(ProgramType::Bool, l)), world.context);
        };
        ProgramDomainValue lowByte = ImageOf(DomainBits256{});
        lowByte.bytes.front() = 1;
        ProgramDomainValue highByte = ImageOf(DomainBits256{});
        highByte.bytes.back() = 1;
        const ProgramDomainValue zero = ImageOf(DomainBits256{});

        EXPECT_TRUE(compare(Opcode::EqDomain, lowByte, lowByte));
        EXPECT_FALSE(compare(Opcode::EqDomain, lowByte, zero));
        EXPECT_FALSE(compare(Opcode::EqDomain, highByte, zero)) << "a difference in the last byte of the register";
        EXPECT_TRUE(compare(Opcode::NeDomain, highByte, lowByte));
        EXPECT_FALSE(compare(Opcode::NeDomain, highByte, highByte));

        for (const bool condition : {true, false}) {
            CallBuilder call;
            const uint8_t cond = call.Scalar(condition);
            const uint8_t whenTrue = call.Constant(lowByte);
            const uint8_t whenFalse = call.Constant(highByte);
            call.builder.Emit(Opcode::SelectDomain, cond, cond, whenTrue, whenFalse);
            EXPECT_EQ(RunOf<ProgramDomainValue>(Require(call.builder.Finish(ProgramType::Domain, cond)), world.context), condition ? lowByte : highByte);
        }
    }

    TEST(ProgramDomainRunTest, TheImageOfAResultEqualsTheImageOfTheSameFieldLoaded) {
        const DomainToys& toys = RegisterDomainToys();
        BoardWorld world;
        world.board.score = DomainPair{3, -4};
        const SlotRef score = world.Link("board.score", DescribeType<DomainPair>());

        // `pair_of(3, -4) == board.score`: the result of an operation and a loaded field are equal images only when both zero the rest of the register
        CallBuilder call;
        const uint8_t first = call.Scalar(int64_t{3});
        const uint8_t second = call.Scalar(int64_t{-4});
        const uint8_t loaded = Require(call.builder.AcquireRegister());
        call.builder.EmitCallDomain(first, toys.pairOf, first, second);
        call.builder.EmitLoadSlot(loaded, ProgramType::Domain, score, sizeof(DomainPair));
        call.builder.Emit(Opcode::EqDomain, first, first, loaded);

        EXPECT_TRUE(RunOf<bool>(Require(call.builder.Finish(ProgramType::Bool, first)), world.context));
    }

    TEST(ProgramDomainRunTest, ADeclaredLevelIsPartOfTheReadsOfAProgramThatCalls) {
        const DomainToys& toys = RegisterDomainToys();

        const auto readsOf = [](DomainOperationId operation) {
            ProgramBuilder builder;
            const uint8_t reg = Require(builder.AcquireRegister());
            builder.EmitCallDomain(reg, operation);
            return Require(builder.Finish(ProgramType::Domain, reg)).Reads();
        };

        EXPECT_EQ(readsOf(WallsThatDeclareTheirLevel()), LevelBit(0));
        EXPECT_EQ(readsOf(toys.walls), 0) << "an operation that declares nothing reads no level, as far as a cache can know";
    }

    TEST(ProgramDomainRunTest, TheBuilderRefusesACallItCannotRun) {
        const DomainToys& toys = RegisterDomainToys();
        const auto refusal = [](auto emit) {
            ProgramBuilder builder;
            const uint8_t reg = Require(builder.AcquireRegister());
            emit(builder, reg);
            const auto program = builder.Finish(ProgramType::Domain, reg);
            EXPECT_FALSE(program.has_value());
            return program.has_value() ? String{} : program.error().message;
        };

        const auto count = static_cast<DomainOperationId>(ProgramDomain::OperationCount());
        EXPECT_NE(refusal([&](ProgramBuilder& builder, uint8_t reg) { builder.EmitCallDomain(reg, count + 5); }).find("registered"), String::npos) << "an operation nobody registered";
        EXPECT_NE(refusal([&](ProgramBuilder& builder, uint8_t reg) { builder.EmitCallDomain(reg, toys.bitsAnd, reg, 9); }).find("never acquired"), String::npos)
            << "an argument register beyond the program's";
        EXPECT_NE(refusal([&](ProgramBuilder& builder, uint8_t reg) { builder.Emit(Opcode::LoadSlotDomain, reg, 0, 0, 0, 0); }).find("registered type of 0 bytes"), String::npos);
        EXPECT_NE(refusal([&](ProgramBuilder& builder, uint8_t reg) { builder.Emit(Opcode::LoadSlotDomain, reg, 0, 33, 0, 0); }).find("registered type of 33 bytes"), String::npos);
    }

    TEST(ProgramDomainRunTest, ACallReadsOnlyTheRegistersOfItsParametersSoFusionStopsAtWhatItReads) {
        const DomainToys& toys = RegisterDomainToys();
        BoardWorld world;
        world.board.turn = 5;
        const SlotRef turn = world.Link("board.turn", DescribeType<int32_t>());

        // `turn * 2 + 3` is one `MulAddSlot` unless the call that follows reads the register of the constant 3 that the fusion would stop writing
        const auto build = [&](bool callReadsTheConstant) {
            ProgramBuilder builder;
            const uint8_t value = Require(builder.AcquireRegister());
            const uint8_t constant = Require(builder.AcquireRegister());
            const uint8_t result = Require(builder.AcquireRegister());
            builder.EmitLoadSlot(value, ProgramType::Int32, turn);
            builder.EmitLoadConst(constant, int32_t{2});
            builder.Emit(Opcode::MulInt32, value, value, constant);
            builder.EmitLoadConst(constant, int32_t{3});
            builder.Emit(Opcode::AddInt32, value, value, constant);
            builder.EmitCallDomain(result, toys.bit, callReadsTheConstant ? constant : value);
            builder.Fuse(result);
            return Require(builder.Finish(ProgramType::Domain, result));
        };

        const Program readsConstant = build(true);
        EXPECT_EQ(CountOf(readsConstant, Opcode::MulAddSlotInt32), 0U) << readsConstant.Disassemble().c_str();
        EXPECT_EQ(RunOf<ProgramDomainValue>(readsConstant, world.context), ImageOf(BitsBit(3)));

        const Program readsValue = build(false);
        EXPECT_EQ(CountOf(readsValue, Opcode::MulAddSlotInt32), 1U) << readsValue.Disassemble().c_str();
        EXPECT_EQ(RunOf<ProgramDomainValue>(readsValue, world.context), ImageOf(BitsBit(13)));
    }

    TEST(ProgramDomainRunTest, ADisassemblyNamesTheOperation) {
        const DomainToys& toys = RegisterDomainToys();
        ProgramBuilder builder;
        const uint8_t reg = Require(builder.AcquireRegister());
        builder.EmitLoadConst(reg, ProgramDomainValue{});
        builder.EmitCallDomain(reg, toys.popcount, reg);

        const String text = Require(builder.Finish(ProgramType::Int32, reg)).Disassemble();

        EXPECT_NE(text.find("popcount"), String::npos) << text.c_str();
    }

    TEST(ProgramDomainRunTest, ARunOfDomainInstructionsAllocatesNothing) {
        const DomainToys& toys = RegisterDomainToys();
        BoardWorld world;
        world.board.walls = PatternMask(7);
        const SlotRef walls = world.Link("board.walls", DescribeType<DomainBits256>());
        ProgramBuilder builder;
        const uint8_t mask = Require(builder.AcquireRegister());
        const uint8_t other = Require(builder.AcquireRegister());
        builder.EmitLoadSlot(mask, ProgramType::Domain, walls, sizeof(DomainBits256));
        builder.EmitCallDomain(other, toys.walls);
        builder.EmitCallDomain(mask, toys.bitsOr, mask, other);
        builder.EmitCallDomain(mask, toys.popcount, mask);
        const Program program = Require(builder.Finish(ProgramType::Int32, mask));
        ASSERT_EQ(RunOf<int32_t>(program, world.context), BitsPopcount(world.board.walls));

        const uint64_t before = AllocTracker::GetThreadCounters().count;
        int32_t sum = 0;
        constexpr int kRuns = 1000;
        for (int i = 0; i < kRuns; ++i) {
            sum += RunOf<int32_t>(program, world.context);
        }
        EXPECT_EQ(sum, kRuns * BitsPopcount(world.board.walls));
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(AllocTracker::GetThreadCounters().count, before);
        }
    }

}  // namespace BECore::Tests
