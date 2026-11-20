// The generated functions of `BagiEngineTests` (tests/NativePrograms, written by the build) against the interpreter: for every program of the set the two paths
// write the same bytes, over contexts made of the edge values of each type (NaN of two payloads, the infinities, subnormals, the extremes of the integers).
// Every bit of a result is compared except the payload and the sign of a NaN: the compiler folds an operation with a constant NaN into some NaN (LLVM keeps no payload),
// so which one a lane holds is not part of the meaning of a program, and `Same` of ProgramInterpreterTest makes the same exception.

#include <BECore/ProgramEdgeValues.h>
#include <BECore/ProgramRowTypes.h>
#include <BECore/Random/Rng.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>
#include <BECore/Values/Programs/Native/ProgramCodegen.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <Common/LogRecorder.h>
#include <Common/Require.h>
#include <Common/TestFiles.h>
#include <Generated/ProgramRowTypes.gen.hpp>
#include <NativePrograms/NativeProgramFormulas.h>
#include <NativePrograms/NativeProgramSet.h>
#include <bit>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;
    using ProgramRowData::ProgramRowOperands;

    namespace {

        constexpr size_t kImageBytes = kProgramRegisterBytes;
        constexpr size_t kCaseCount = 2048;
        constexpr uint64_t kSeed = 20260101;

        constexpr uint32_t kFloatNanPayload = 0x7FC12345U;
        constexpr uint32_t kFloatNegativeNan = 0xFFC00000U;
        constexpr uint64_t kDoubleNanPayload = 0x7FF8000000012345ULL;
        constexpr uint64_t kDoubleNegativeNan = 0xFFF8000000000000ULL;

        /// The edges of the instruction tests and the values only a bytewise comparison sees: the zero with a sign, NaN of another payload and of the other sign, a subnormal, the
        /// values a conversion or an addition rounds
        template <typename T>
        Vector<T> BuildInputs() {
            Vector<T> values = Edges<T>();
            constexpr float kPayloadNan = std::bit_cast<float>(kFloatNanPayload);
            constexpr float kNegativeNan = std::bit_cast<float>(kFloatNegativeNan);
            constexpr float kDenorm = std::numeric_limits<float>::denorm_min();
            if constexpr (std::is_same_v<T, float>) {
                for (const float value : {-0.0f, kDenorm, -kDenorm, 8388608.5f, 16777216.0f, 0.1f, 1e-30f, kPayloadNan, kNegativeNan}) {
                    values.push_back(value);
                }
            } else if constexpr (std::is_same_v<T, double>) {
                for (const double value : {-0.0, std::numeric_limits<double>::denorm_min(), 4503599627370496.5, 9007199254740992.0, 0.1, 1e-300, std::bit_cast<double>(kDoubleNanPayload),
                                           std::bit_cast<double>(kDoubleNegativeNan)}) {
                    values.push_back(value);
                }
            } else if constexpr (std::is_same_v<T, int64_t>) {
                for (const int64_t value : {int64_t{4294967295}, -int64_t{4294967296}, int64_t{1} << 53, (int64_t{1} << 53) + 1}) {
                    values.push_back(value);
                }
            } else if constexpr (std::is_same_v<T, Vec2>) {
                values.push_back(Vec2{kPayloadNan, -0.0f});
                values.push_back(Vec2{kDenorm, 0.1f});
            } else if constexpr (std::is_same_v<T, Vec3>) {
                values.push_back(Vec3{kPayloadNan, -0.0f, kDenorm});
                values.push_back(Vec3{0.1f, 1e30f, -1e30f});
            } else if constexpr (std::is_same_v<T, Vec4> || std::is_same_v<T, Quat>) {
                values.push_back(T{kPayloadNan, -0.0f, kDenorm, kNegativeNan});
                values.push_back(T{0.1f, 1e30f, -1e30f, 0.5f});
            }
            return values;
        }

        template <typename T>
        const Vector<T>& Inputs() {
            static const Vector<T> values = BuildInputs<T>();
            return values;
        }

        /// How a field picks its value for case `index`: the first operand walks the list, the second steps once per lap of the first (so two operands meet in every pair
        /// while the list is shorter than the number of cases), the third is drawn
        CORE_ENUM(Pick, uint8_t, Walk, Step, Draw)

        template <typename T>
        T Choose(Pick pick, size_t index, Rng& rng) {
            const Vector<T>& values = Inputs<T>();
            const size_t count = values.size();
            switch (pick) {
                case Pick::Walk:
                    return values[index % count];
                case Pick::Step:
                    return values[(index / count) % count];
                case Pick::Draw:
                    break;
            }
            return values[rng.UniformInt<size_t>(0, count - 1)];
        }

        void Fill(ProgramRowOperands& operands, Pick pick, size_t index, Rng& rng) {
            operands.flag = Choose<bool>(pick, index, rng);
            operands.i32 = Choose<int32_t>(pick, index, rng);
            operands.i32b = Choose<int32_t>(Pick::Draw, index, rng);
            operands.i64 = Choose<int64_t>(pick, index, rng);
            operands.f32 = Choose<float>(pick, index, rng);
            operands.f32b = Choose<float>(Pick::Draw, index, rng);
            operands.f64 = Choose<double>(pick, index, rng);
            operands.v2 = Choose<Vec2>(pick, index, rng);
            operands.v3 = Choose<Vec3>(pick, index, rng);
            operands.v4 = Choose<Vec4>(pick, index, rng);
            operands.iv2 = Choose<IVec2>(pick, index, rng);
            operands.quat = Choose<Quat>(pick, index, rng);
            operands.color = Choose<Color>(pick, index, rng);
            operands.text = Choose<PoolString>(pick, index, rng);
            const ProgramDomainValue image = Choose<ProgramDomainValue>(pick, index, rng);
            static_assert(sizeof(operands.mask) == sizeof(image) && sizeof(operands.pair) <= sizeof(image));
            std::memcpy(&operands.mask, image.bytes.data(), sizeof(operands.mask));
            std::memcpy(&operands.pair, image.bytes.data(), sizeof(operands.pair));
            const uint32_t element = Choose<ProgramEnum>(pick, index, rng).index;
            operands.narrow = static_cast<ValueNarrowEnum>(static_cast<uint8_t>(element));
            operands.wide16 = static_cast<ValueShortEnum>(static_cast<uint16_t>(element));
            operands.wide32 = static_cast<ValueWideEnum>(element);
            operands.enum64 = element;
        }

        /// The objects of every level of `NativeProgramSetDeclaration()`: what a formula reads is a `caster` and a `cast`, what a hand-assembled program reads is `a`, `b` and `c`
        struct InputCase {
            ValueUnitStats caster;
            ValueCastInfo cast;
            ProgramRowOperands a;
            ProgramRowOperands b;
            ProgramRowOperands c;
        };

        void Fill(InputCase& input, size_t index, Rng& rng) {
            input.caster.alive = Choose<bool>(Pick::Walk, index, rng);
            input.caster.attack = Choose<int32_t>(Pick::Walk, index, rng);
            input.caster.score = Choose<int64_t>(Pick::Walk, index, rng);
            input.caster.speed = Choose<float>(Pick::Walk, index, rng);
            input.caster.precise = Choose<double>(Pick::Walk, index, rng);
            input.caster.velocity = Choose<Vec3>(Pick::Walk, index, rng);
            input.cast.level = Choose<int32_t>(Pick::Step, index, rng);
            input.cast.power = Choose<float>(Pick::Step, index, rng);
            input.cast.target = Choose<Vec3>(Pick::Step, index, rng);
            Fill(input.a, Pick::Walk, index, rng);
            Fill(input.b, Pick::Step, index, rng);
            Fill(input.c, Pick::Draw, index, rng);
        }

        Context MakeContext(const InputCase& input) {
            Context context(NativeProgramSetDeclaration());
            context.Set(kCasterLevel, input.caster);
            context.Set(kCastLevel, input.cast);
            context.Set(kOperandsLevelA, input.a);
            context.Set(kOperandsLevelB, input.b);
            context.Set(kOperandsLevelC, input.c);
            return context;
        }

        String Describe(const Program& program) {
            return String(program.Source().ToStringView());
        }

        String DescribeOperands(const ProgramRowOperands& operands) {
            return Format("{{i32={} i64={} f32={} f64={}}}", operands.i32, operands.i64, operands.f32, operands.f64);
        }

        String DescribeInputs(const InputCase& input) {
            return Format("caster{{attack={} score={} speed={} precise={}}} cast{{level={} power={}}} a{} b{} c{}", input.caster.attack, input.caster.score, input.caster.speed, input.caster.precise,
                          input.cast.level, input.cast.power, DescribeOperands(input.a), DescribeOperands(input.b), DescribeOperands(input.c));
        }

        String Hex(const std::byte* bytes, size_t count) {
            String text;
            for (size_t i = 0; i < count; ++i) {
                text += Format("{:02x}", static_cast<unsigned>(bytes[i]));
            }
            return text;
        }

        /// The size of one float lane of a result type, 0 for a type that has none (its bytes are compared as they are)
        size_t FloatLaneBytes(ProgramType type) {
            switch (type) {
                case ProgramType::Float:
                case ProgramType::Vec2:
                case ProgramType::Vec3:
                case ProgramType::Vec4:
                case ProgramType::Quat:
                    return sizeof(float);
                case ProgramType::Double:
                    return sizeof(double);
                default:
                    return 0;
            }
        }

        template <typename F>
        bool IsNan(const std::byte* lane) {
            F value{};
            std::memcpy(&value, lane, sizeof(F));
            return value != value;
        }

        /// The bytes of two results of @p type are the same, a NaN lane being the same as another NaN lane
        bool SameResult(ProgramType type, const std::byte* left, const std::byte* right) {
            const size_t size = ProgramTypeSize(type);
            const size_t lane = FloatLaneBytes(type);
            if (lane == 0) {
                return std::memcmp(left, right, size) == 0;
            }
            for (size_t offset = 0; offset < size; offset += lane) {
                const bool bothNan = lane == sizeof(float) ? IsNan<float>(left + offset) && IsNan<float>(right + offset) : IsNan<double>(left + offset) && IsNan<double>(right + offset);
                if (!bothNan && std::memcmp(left + offset, right + offset, lane) != 0) {
                    return false;
                }
            }
            return true;
        }

        /// The file the build wrote for this executable: the build folder is the working directory of ctest, and the folder above `bin` is it when the executable is started by hand
        Optional<String> ReadGeneratedFile() {
            constexpr char kRelative[] = "Generated/BagiEngineTests.native.cpp";
            Vector<std::filesystem::path> candidates{std::filesystem::path(kRelative)};
            const auto& arguments = testing::internal::GetArgvs();
            if (!arguments.empty()) {
                std::error_code error;
                const std::filesystem::path executable = std::filesystem::weakly_canonical(arguments.front(), error);
                candidates.push_back(executable.parent_path().parent_path() / kRelative);
            }
            for (const std::filesystem::path& path : candidates) {
                if (std::filesystem::exists(path)) {
                    return ReadFile(path);
                }
            }
            return std::nullopt;
        }

        /// The text of a generated file without the source text of each program: an equal program that another test of the process compiled first carries that formula
        String WithoutSources(const String& text) {
            constexpr StringView kEntry = "    {0x";
            constexpr StringView kSourceEnd = "\", &Program_";
            String out;
            size_t begin = 0;
            while (begin < text.size()) {
                size_t end = text.find('\n', begin);
                end = end == String::npos ? text.size() : end + 1;
                const StringView line(text.data() + begin, end - begin);
                const size_t quote = line.find('"');
                const size_t close = line.rfind(kSourceEnd);
                if (line.starts_with(kEntry) && quote != StringView::npos && close != StringView::npos && close > quote) {
                    out.append(line.data(), quote + 1);
                    out.append(line.data() + close, line.size() - close);
                } else {
                    out.append(line.data(), line.size());
                }
                begin = end;
            }
            return out;
        }

        /// Puts the miss warning back to its default (off) when a test that turned it on ends
        struct WarnOnMissGuard {
            WarnOnMissGuard() {
                NativePrograms::SetWarnOnMiss(true);
            }
            WarnOnMissGuard(const WarnOnMissGuard&) = delete;
            WarnOnMissGuard& operator=(const WarnOnMissGuard&) = delete;
            ~WarnOnMissGuard() {
                NativePrograms::SetWarnOnMiss(false);
            }
        };

        struct Image {
            alignas(kImageBytes) std::byte bytes[kImageBytes]{};
        };

    }  // namespace

    TEST(NativeProgramEqualityTest, EveryProgramOfTheSetWritesTheSameBytesOnBothPathsOverTheEdgeValues) {
        const auto compiled = CompileNativeProgramSet();
        ASSERT_TRUE(compiled.has_value()) << compiled.error().c_str();
        ASSERT_FALSE(compiled->empty());
        EXPECT_GE(NativePrograms::Counts().bound, compiled->size());

        Rng rng(kSeed);
        Vector<InputCase> inputs(kCaseCount);
        Vector<Context> contexts;
        for (size_t i = 0; i < inputs.size(); ++i) {
            Fill(inputs[i], i, rng);
        }
        for (const InputCase& input : inputs) {
            contexts.push_back(MakeContext(input));
        }

        for (const ProgramId id : *compiled) {
            const Program& program = ProgramPool::Get(id);
            const NativeProgramFn function = ProgramPool::Native(id);
            ASSERT_NE(function, nullptr) << Describe(program).c_str() << " is not bound: the generated file is missing or stale";
            const size_t size = ProgramTypeSize(program.ResultType());
            for (size_t i = 0; i < contexts.size(); ++i) {
                Image generated;
                Image interpreted;
                function(contexts[i], generated.bytes);
                BECore::Run(program, contexts[i], interpreted.bytes);
                if (!SameResult(program.ResultType(), generated.bytes, interpreted.bytes)) {
                    ADD_FAILURE() << Describe(program).c_str() << " case " << i << ": generated " << Hex(generated.bytes, size).c_str() << ", interpreter " << Hex(interpreted.bytes, size).c_str()
                                  << "\n"
                                  << program.Disassemble().c_str() << DescribeInputs(inputs[i]).c_str();
                    break;
                }
            }
        }
    }

    TEST(NativeProgramEqualityTest, TheSetRunsEveryOpcode) {
        const auto compiled = CompileNativeProgramSet();
        ASSERT_TRUE(compiled.has_value()) << compiled.error().c_str();

        Array<bool, EnumUtils<Opcode>::Count()> used{};
        for (const ProgramId id : *compiled) {
            for (const Instruction& ins : ProgramPool::Get(id).Code()) {
                used[static_cast<size_t>(ins.op)] = true;
            }
        }
        for (const Opcode op : EnumUtils<Opcode>::Values()) {
            // `CallDomain` names an operation of a game's registry, which the tool that writes the set does not register
            if (op != Opcode::Invalid && op != Opcode::CallDomain) {
                EXPECT_TRUE(used[static_cast<size_t>(op)]) << "no program of the set runs " << EnumUtils<Opcode>::ToString(op).data() << ": add it to NativeProgramSetRows.cpp";
            }
        }
    }

    TEST(NativeProgramEqualityTest, TheGeneratedFileIsWhatTheSetGeneratesNow) {
        const auto compiled = CompileNativeProgramSet();
        ASSERT_TRUE(compiled.has_value()) << compiled.error().c_str();
        const Optional<String> built = ReadGeneratedFile();
        ASSERT_TRUE(built.has_value()) << "the file the build generated is not found: run the executable from the build folder";

        const String now = GenerateNativeSource(Span<const ProgramId>(compiled->data(), compiled->size()), NativeCodegenOptions{kNativeProgramSetName});
        const String expected = WithoutSources(now);
        const String actual = WithoutSources(*built);
        if (expected != actual) {
            size_t at = 0;
            while (at < expected.size() && at < actual.size() && expected[at] == actual[at]) {
                ++at;
            }
            ADD_FAILURE() << "the generated file of the build differs from the one the set generates now, first at byte " << at << ": the build did not regenerate it\n"
                          << "build:  " << String(actual.substr(at, 160)).c_str() << "\nnow:    " << String(expected.substr(at, 160)).c_str();
        }
    }

    TEST(NativeProgramEqualityTest, AFormulaWithAnotherConstantThanTheSetIsNotBoundAndRunsTheInterpreter) {
        constexpr StringView kSetFormula = kNativeIntFormulas[1];
        ASSERT_TRUE(kSetFormula.ends_with("+ 7"));
        // Another addend on every run of the case (`--gtest_repeat`): the pool keeps a program for the life of the process and warns about a miss only when it is interned
        static int32_t nextAddend = 70913;
        const int32_t staleAddend = nextAddend++;
        const String stale = Format("{}{}", kSetFormula.substr(0, kSetFormula.size() - 1), staleAddend);

        const WarnOnMissGuard warn;
        LogRecorder recorder;
        const auto value = CompileValue<int32_t>(stale, NativeProgramSetDeclaration());
        LogEvent::Flush();

        ASSERT_TRUE(value.has_value());
        ASSERT_EQ(value->Kind(), ValueKind::Program);
        EXPECT_EQ(ProgramPool::Native(value->Id()), nullptr);
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {StringView(stale), "stale or missing"}), 1U);
        Rng rng(kSeed);
        InputCase input;
        Fill(input, 3, rng);
        const Context context = MakeContext(input);
        EXPECT_EQ(value->Evaluate(context), static_cast<int32_t>(static_cast<uint32_t>(input.caster.attack) * 3U + static_cast<uint32_t>(staleAddend)));
    }

    TEST(NativeProgramEqualityTest, AFieldAtAnotherOffsetIsAnotherProgramThatIsNotBound) {
        constexpr StringView kSetFormula = kNativeIntFormulas[5];
        ASSERT_EQ(kSetFormula, "a.i32 * 3 + 7");
        ContextDecl moved;
        static_cast<void>(moved.Add<ValueUnitStats>("caster"));
        static_cast<void>(moved.Add<ValueCastInfo>("cast"));
        static_cast<void>(moved.Add<ProgramRowData::ProgramRowShifted>("a"));

        const auto inSet = CompileValue<int32_t>(kSetFormula, NativeProgramSetDeclaration());
        const auto shifted = CompileValue<int32_t>(kSetFormula, moved);

        ASSERT_TRUE(inSet.has_value() && shifted.has_value());
        ASSERT_EQ(inSet->Kind(), ValueKind::Program);
        ASSERT_EQ(shifted->Kind(), ValueKind::Program);
        EXPECT_NE(ProgramPool::Get(inSet->Id()).CodeHash(), ProgramPool::Get(shifted->Id()).CodeHash());
        EXPECT_NE(ProgramPool::Native(inSet->Id()), nullptr);
        EXPECT_EQ(ProgramPool::Native(shifted->Id()), nullptr);

        const ValueUnitStats caster;
        const ValueCastInfo cast;
        ProgramRowData::ProgramRowShifted object;
        object.i32 = 5;
        Context context(moved);
        context.Set(kCasterLevel, caster);
        context.Set(kCastLevel, cast);
        context.Set(kOperandsLevelA, object);
        EXPECT_EQ(shifted->Evaluate(context), 22);
    }

    TEST(NativeProgramEqualityTest, EvaluateRunsTheGeneratedFunctionAndGivesTheInterpretersValue) {
        const auto value = CompileValue<int32_t>("caster.attack * 3 + 7", NativeProgramSetDeclaration());
        ASSERT_TRUE(value.has_value());
        ASSERT_EQ(value->Kind(), ValueKind::Program);
        ASSERT_NE(ProgramPool::Native(value->Id()), nullptr);

        Rng rng(kSeed);
        InputCase input;
        for (size_t i = 0; i < Inputs<int32_t>().size(); ++i) {
            Fill(input, i, rng);
            const Context context = MakeContext(input);
            EXPECT_EQ(value->Evaluate(context), Run<int32_t>(ProgramPool::Get(value->Id()), context));
        }
    }

}  // namespace BECore::Tests
