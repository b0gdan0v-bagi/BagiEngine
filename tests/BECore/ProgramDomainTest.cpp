// The registry of a game's value types and operations (Values/Programs/Domain/ProgramDomain.h). The generated code of the reflected `DomainBoard` is included here only.

#include <BECore/ProgramDomainTypes.h>
#include <BECore/Values/Programs/Domain/ProgramDomain.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Generated/ProgramDomainTypes.gen.hpp>
#include <atomic>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ProgramDomainData;

    namespace {

        [[nodiscard]] int32_t WithoutOverload(int32_t value) {
            return value;
        }

        [[nodiscard]] float WithoutOverload(float value) {
            return value;
        }

        [[nodiscard]] int64_t IntegerScaleWiderResult(int32_t value) {
            return value;
        }

        [[nodiscard]] DomainBits256 PatternMask(uint64_t seed) {
            DomainBits256 mask;
            for (size_t i = 0; i < mask.words.size(); ++i) {
                mask.words[i] = (seed + i) * 0x9E3779B97F4A7C15ULL;
            }
            return mask;
        }

        /// Fills every register with a pattern, so a write that is not the whole result shows
        void FillRegisters(Array<RegisterImage, kMaxProgramRegisters>& registers) {
            for (RegisterImage& image : registers) {
                image.bytes.fill(0xEE);
            }
        }

        template <typename T>
        void Put(RegisterImage& image, const T& value) {
            image = RegisterImage{};
            std::memcpy(image.bytes.data(), &value, sizeof(T));
        }

        template <typename T>
        [[nodiscard]] T Get(const RegisterImage& image) {
            T value{};
            std::memcpy(&value, image.bytes.data(), sizeof(T));
            return value;
        }

        /// Calls the raw function of an operation as the interpreter does: arguments in `a`, `b`, `c`, the result in `dst`
        void Call(DomainOperationId id, Array<RegisterImage, kMaxProgramRegisters>& registers, const Context& context, uint8_t a = 1, uint8_t b = 2, uint8_t c = 3) {
            const Instruction ins{Opcode::Invalid, 0, a, b, c, id};
            ProgramDomain::Operation(id).function(registers.data(), ins, context);
        }

        /// A context with a board at level 0 whose walls are @p walls
        struct BoardContext {
            ContextDecl declaration;
            DomainBoard board;
            Context context;

            explicit BoardContext(const DomainBits256& walls) {
                EXPECT_TRUE(declaration.Add<DomainBoard>("board").has_value());
                board.walls = walls;
                context = Context(declaration);
                context.Set(0, board);
            }
        };

        std::atomic<int32_t> g_probeCounter{0};

        /// A name no earlier case of this process used: the registry never forgets
        [[nodiscard]] String FreshOperationName(StringView stem) {
            return Format("{}_{}", stem, g_probeCounter.fetch_add(1));
        }

    }  // namespace

    TEST(ProgramDomainTest, CompileTimeChecks) {
        static_assert(DomainValueClass<DomainBits256> && DomainValueClass<DomainPair>);
        static_assert(!DomainValueClass<String> && !DomainValueClass<Vec3> && !DomainValueClass<DomainDirection>, "an engine type, an enum or a type that owns memory is no game type");
        static_assert(DomainFunction<&BitsAnd> && DomainFunction<&BoardWalls> && DomainFunction<&BitsShift>);
        static_assert(DomainFunction<&UnregisteredParameter>, "an unregistered type is refused when the function is added, not by the compiler");
    }

    TEST(ProgramDomainTest, ATypeIsFoundByNameAndByDescriptor) {
        const DomainToys& toys = RegisterDomainToys();

        EXPECT_EQ(ProgramDomain::FindType("mask"), toys.mask);
        EXPECT_EQ(ProgramDomain::FindTypeByDescriptor(DescribeType<DomainBits256>()), toys.mask);
        EXPECT_EQ(ProgramDomain::FindTypeByDescriptor(DescribeType<DomainPair>()), toys.pair);
        EXPECT_FALSE(ProgramDomain::FindType("no_such_type").has_value());
        EXPECT_FALSE(ProgramDomain::FindTypeByDescriptor(DescribeType<DomainUnregistered>()).has_value());
        EXPECT_FALSE(ProgramDomain::FindTypeByDescriptor(DescribeType<Vec3>()).has_value()) << "an engine type is not a registered one";

        const DomainType& mask = ProgramDomain::Type(toys.mask);
        EXPECT_EQ(mask.name, "mask"_intern);
        EXPECT_EQ(mask.size, sizeof(DomainBits256));
        EXPECT_TRUE(SameType(*mask.descriptor, DescribeType<DomainBits256>()));
        EXPECT_EQ(ProgramDomain::Type(toys.pair).size, sizeof(DomainPair));
    }

    TEST(ProgramDomainTest, EqualityDefaultsFromTheObjectRepresentation) {
        const DomainToys& toys = RegisterDomainToys();

        EXPECT_EQ(ProgramDomain::Type(toys.mask).equality, DomainEquality::Bitwise);
        EXPECT_EQ(ProgramDomain::Type(toys.pair).equality, DomainEquality::Bitwise);
        EXPECT_EQ(ProgramDomain::Type(toys.weighted).equality, DomainEquality::None) << "a float member breaks bitwise equality";
    }

    TEST(ProgramDomainTest, ATypeRegistrationIsRefusedWithItsReason) {
        const DomainToys& toys = RegisterDomainToys();
        static_cast<void>(toys);
        const auto refusal = [](const std::expected<DomainTypeId, String>& result) {
            EXPECT_FALSE(result.has_value());
            return result.has_value() ? String{} : result.error();
        };
        const size_t typesBefore = ProgramDomain::TypeCount();

        EXPECT_NE(refusal(ProgramDomain::RegisterType<DomainUnregistered>("mask")).find("taken"), String::npos) << "a duplicate name";
        EXPECT_NE(refusal(ProgramDomain::RegisterType<DomainBits256>("mask_again")).find("registered already as 'mask'"), String::npos) << "the same C++ type under another name";
        EXPECT_NE(refusal(ProgramDomain::RegisterType<DomainHuge>("huge")).find("40 bytes"), String::npos) << "over a register";
        EXPECT_NE(refusal(ProgramDomain::RegisterType<DomainUnregistered>("vec3")).find("engine type"), String::npos);
        EXPECT_NE(refusal(ProgramDomain::RegisterType<DomainUnregistered>("int")).find("engine type"), String::npos);
        EXPECT_NE(refusal(ProgramDomain::RegisterType<DomainUnregistered>("two words")).find("identifier"), String::npos);
        EXPECT_NE(refusal(ProgramDomain::RegisterType<DomainUnregistered>("")).find("identifier"), String::npos);
        EXPECT_NE(refusal(ProgramDomain::RegisterType<DomainUnregistered>("1st")).find("identifier"), String::npos);
        EXPECT_NE(refusal(ProgramDomain::RegisterType<DomainWeighted>("weighted_bits", DomainTypeOptions{DomainEquality::Bitwise})).find("compares"), String::npos)
            << "bitwise equality of a type with a float";

        EXPECT_EQ(ProgramDomain::TypeCount(), typesBefore) << "a refused registration changes nothing";
    }

    TEST(ProgramDomainTest, AnOperationRecordsItsSignature) {
        const DomainToys& toys = RegisterDomainToys();

        const DomainOperation& shift = ProgramDomain::Operation(toys.shift);
        EXPECT_EQ(shift.name, "shift"_intern);
        ASSERT_EQ(shift.parameterCount, 2);
        EXPECT_EQ(shift.parameters[0].domain, toys.mask);
        EXPECT_EQ(shift.parameters[1].type, ProgramType::Enum);
        ASSERT_NE(shift.parameters[1].enumType, nullptr);
        EXPECT_TRUE(SameType(*shift.parameters[1].enumType, DescribeType<DomainDirection>()));
        EXPECT_EQ(shift.result.domain, toys.mask);
        EXPECT_FALSE(shift.takesContext);

        const DomainOperation& popcount = ProgramDomain::Operation(toys.popcount);
        EXPECT_EQ(popcount.result.type, ProgramType::Int32);
        EXPECT_FALSE(popcount.result.IsDomain());

        const DomainOperation& walls = ProgramDomain::Operation(toys.wallsOfRow);
        EXPECT_TRUE(walls.takesContext);
        EXPECT_EQ(walls.parameterCount, 1) << "the context is not a parameter of the formula";
        EXPECT_EQ(ProgramDomain::Operation(toys.walls).parameterCount, 0);
        EXPECT_EQ(ProgramDomain::Operation(toys.majority).parameterCount, 3);
    }

    TEST(ProgramDomainTest, AnOperationRegistrationIsRefusedWithItsReason) {
        const DomainToys& toys = RegisterDomainToys();
        static_cast<void>(toys);
        const auto refusal = [](const std::expected<DomainOperationId, String>& result) {
            EXPECT_FALSE(result.has_value());
            return result.has_value() ? String{} : result.error();
        };
        const size_t operationsBefore = ProgramDomain::OperationCount();

        EXPECT_NE(refusal(ProgramDomain::AddOperation<&BitsPopcount>("min")).find("function of the formula language"), String::npos) << "an engine function";
        EXPECT_NE(refusal(ProgramDomain::AddOperation<&BitsPopcount>("at")).find("function of the formula language"), String::npos);
        EXPECT_NE(refusal(ProgramDomain::AddOperation<&BitsPopcount>("ByLevel")).find("function of the formula language"), String::npos);
        EXPECT_NE(refusal(ProgramDomain::AddOperation<&BitsPopcount>("a+b")).find("identifier"), String::npos);
        EXPECT_NE(refusal(ProgramDomain::AddOperation<&FourOf>("four_of")).find("takes 4 parameters"), String::npos) << "more than an instruction has registers for";
        EXPECT_NE(refusal(ProgramDomain::AddOperation<&UnregisteredParameter>("unregistered_parameter")).find("parameter 1"), String::npos);
        EXPECT_NE(refusal(ProgramDomain::AddOperation<&UnregisteredResult>("unregistered_result")).find("result type"), String::npos);
        EXPECT_NE(refusal(ProgramDomain::AddOperation<&BitsAnd>("and_mask")).find("registered already"), String::npos) << "the same name and parameters";
        EXPECT_NE(refusal(ProgramDomain::AddOperation<&BitsOr>("and_mask")).find("registered already"), String::npos) << "another function with the same parameters";

        EXPECT_EQ(ProgramDomain::OperationCount(), operationsBefore) << "a refused registration changes nothing";
    }

    TEST(ProgramDomainTest, OverloadsDifferInTheirParameters) {
        static const auto ids = [] {
            const auto integer = Require(ProgramDomain::AddOperation<static_cast<int32_t (*)(int32_t)>(&WithoutOverload)>("overloaded"));
            const auto real = Require(ProgramDomain::AddOperation<static_cast<float (*)(float)>(&WithoutOverload)>("overloaded"));
            return Array<DomainOperationId, 2>{integer, real};
        }();

        const Vector<DomainOperationId> found = ProgramDomain::FindOperations("overloaded");
        ASSERT_EQ(found.size(), 2U);
        EXPECT_EQ(found[0], ids[0]);
        EXPECT_EQ(found[1], ids[1]) << "in registration order";
        EXPECT_EQ(ProgramDomain::Operation(ids[0]).parameters[0].type, ProgramType::Int32);
        EXPECT_EQ(ProgramDomain::Operation(ids[1]).parameters[0].type, ProgramType::Float);

        const auto sameParameters = ProgramDomain::AddOperation<&IntegerScaleWiderResult>("overloaded");
        EXPECT_FALSE(sameParameters.has_value()) << "a result alone does not make an overload";
        EXPECT_TRUE(ProgramDomain::FindOperations("no_such_operation").empty());
    }

    TEST(ProgramDomainTest, TheRawFunctionOfEachOperationIsTheCppFunction) {
        const DomainToys& toys = RegisterDomainToys();
        const DomainBits256 a = PatternMask(1);
        const DomainBits256 b = PatternMask(7);
        const DomainBits256 c = PatternMask(13);
        BoardContext board(a);
        Array<RegisterImage, kMaxProgramRegisters> registers;
        FillRegisters(registers);

        Put(registers[1], a);
        Put(registers[2], b);
        Call(toys.bitsAnd, registers, board.context);
        EXPECT_EQ(Get<DomainBits256>(registers[0]), BitsAnd(a, b));
        Call(toys.bitsOr, registers, board.context);
        EXPECT_EQ(Get<DomainBits256>(registers[0]), BitsOr(a, b));

        Put(registers[3], c);
        Call(toys.majority, registers, board.context);
        EXPECT_EQ(Get<DomainBits256>(registers[0]), BitsMajority(a, b, c));

        Call(toys.popcount, registers, board.context);
        EXPECT_EQ(Get<int32_t>(registers[0]), BitsPopcount(a));

        Call(toys.any, registers, board.context);
        EXPECT_EQ(Get<bool>(registers[0]), BitsAny(a));

        for (const DomainDirection direction : EnumUtils<DomainDirection>::Values()) {
            Put(registers[1], a);
            Put(registers[2], ProgramEnum{static_cast<uint32_t>(direction)});
            Call(toys.shift, registers, board.context);
            EXPECT_EQ(Get<DomainBits256>(registers[0]), BitsShift(a, direction)) << EnumUtils<DomainDirection>::ToString(direction).data();
        }

        Put(registers[1], int32_t{37});
        Call(toys.bit, registers, board.context);
        EXPECT_EQ(Get<DomainBits256>(registers[0]), BitsBit(37));

        Put(registers[1], int32_t{5});
        Put(registers[2], int32_t{9});
        Call(toys.cell, registers, board.context);
        EXPECT_EQ(Get<DomainBits256>(registers[0]), BitsCell(5, 9));

        Call(toys.walls, registers, board.context);
        EXPECT_EQ(Get<DomainBits256>(registers[0]), a) << "an operation without parameters reads the context";

        Put(registers[1], int32_t{2});
        Call(toys.wallsOfRow, registers, board.context);
        EXPECT_EQ(Get<DomainBits256>(registers[0]), BoardWallsOfRow(board.context, 2)) << "the context and one argument";
    }

    TEST(ProgramDomainTest, AResultOfARegisteredTypeZeroesTheRestOfTheRegister) {
        const DomainToys& toys = RegisterDomainToys();
        BoardContext board({});
        Array<RegisterImage, kMaxProgramRegisters> registers;
        FillRegisters(registers);

        Put(registers[1], int64_t{-3});
        Put(registers[2], int64_t{4});
        Call(toys.pairOf, registers, board.context);

        EXPECT_EQ(Get<DomainPair>(registers[0]), PairOf(-3, 4));
        for (size_t i = sizeof(DomainPair); i < kProgramRegisterBytes; ++i) {
            EXPECT_EQ(registers[0].bytes[i], 0) << "byte " << i << ": equal values must be equal images";
        }
    }

    TEST(ProgramDomainTest, TheFingerprintFollowsWhatIsRegistered) {
        static_cast<void>(RegisterDomainToys());
        const uint64_t before = ProgramDomain::Fingerprint();
        EXPECT_EQ(ProgramDomain::Fingerprint(), before) << "reading changes nothing";

        const String name = FreshOperationName("fingerprint_probe");
        static_cast<void>(Require(ProgramDomain::AddOperation<&BitsAny>(name)));

        EXPECT_NE(ProgramDomain::Fingerprint(), before);
    }

    TEST(ProgramDomainTest, AnEntryDoesNotMoveWhileAnotherThreadRegisters) {
        if constexpr (!Thread::kAvailable) {
            GTEST_SKIP() << "no threads in this build";
        }
        const DomainToys& toys = RegisterDomainToys();
        const DomainOperation* const popcount = &ProgramDomain::Operation(toys.popcount);
        const DomainType* const mask = &ProgramDomain::Type(toys.mask);
        // More than a chunk of the table holds, so a new chunk is published while the reader looks
        constexpr int32_t kRegistrations = 150;
        const String stem = FreshOperationName("concurrent");

        std::atomic<bool> done{false};
        std::atomic<int32_t> moved{0};
        std::atomic<int64_t> lookups{0};
        Thread reader("DomainReader", [&] {
            while (!done.load()) {
                if (&ProgramDomain::Operation(toys.popcount) != popcount || &ProgramDomain::Type(toys.mask) != mask) {
                    moved.fetch_add(1);
                }
                const size_t count = ProgramDomain::OperationCount();
                if (count > 0 && ProgramDomain::Operation(static_cast<DomainOperationId>(count - 1)).function == nullptr) {
                    moved.fetch_add(1);
                }
                lookups.fetch_add(1);
            }
        });
        for (int32_t i = 0; i < kRegistrations; ++i) {
            static_cast<void>(Require(ProgramDomain::AddOperation<&BitsAny>(Format("{}_{}", stem, i))));
        }
        done.store(true);
        reader.join();

        EXPECT_EQ(moved.load(), 0) << "an address of an earlier entry changed, or an entry was seen half written";
        EXPECT_GT(lookups.load(), 0);
        EXPECT_EQ(ProgramDomain::FindOperations(Format("{}_{}", stem, kRegistrations - 1)).size(), 1U);
    }

}  // namespace BECore::Tests
