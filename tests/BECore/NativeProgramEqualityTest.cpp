// The generated functions of `BagiEngineTests` (tests/NativePrograms, written by the build) against the interpreter: for every program of the set the two paths
// write the same bytes. NaN, the infinities and the integer extremes are in the objects, so a kernel that differs on them shows here.

#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <Common/Require.h>
#include <NativePrograms/NativeProgramSet.h>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr size_t kImageBytes = 16;

        struct Pair {
            ValueUnitStats caster;
            ValueCastInfo cast;
        };

        /// Objects of the two levels of the set: ordinary numbers, negative ones, the extremes and the values a float computation can go wrong on
        Vector<Pair> MakePairs() {
            Vector<Pair> pairs(6);
            pairs[1].caster.attack = -37;
            pairs[1].caster.alive = false;
            pairs[1].caster.score = -5000000000;
            pairs[1].caster.speed = -2.75f;
            pairs[1].caster.precise = -0.125;
            pairs[1].caster.velocity = Vec3{-4.0f, 0.5f, 9.25f};
            pairs[1].cast.level = 0;
            pairs[1].cast.power = -1.0f;
            pairs[2].caster.attack = std::numeric_limits<int32_t>::max();
            pairs[2].caster.score = std::numeric_limits<int64_t>::max();
            pairs[2].cast.level = std::numeric_limits<int32_t>::max();
            pairs[3].caster.attack = std::numeric_limits<int32_t>::min() + 1;
            pairs[3].caster.score = std::numeric_limits<int64_t>::min() + 1;
            pairs[3].cast.level = std::numeric_limits<int32_t>::min() + 1;
            pairs[4].caster.speed = std::numeric_limits<float>::quiet_NaN();
            pairs[4].caster.precise = std::numeric_limits<double>::infinity();
            pairs[4].caster.velocity = Vec3{std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
            pairs[4].cast.power = std::numeric_limits<float>::infinity();
            pairs[5].caster.speed = std::numeric_limits<float>::denorm_min();
            pairs[5].caster.precise = std::numeric_limits<double>::denorm_min();
            pairs[5].caster.velocity = Vec3{std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
            pairs[5].cast.power = std::numeric_limits<float>::max();
            return pairs;
        }

        String Describe(const Program& program) {
            return String(program.Source().ToStringView());
        }

        struct Image {
            alignas(kImageBytes) std::byte bytes[kImageBytes]{};
        };

    }  // namespace

    TEST(NativeProgramEqualityTest, EveryProgramOfTheSetRunsItsGeneratedFunctionAndAgreesWithTheInterpreter) {
        const auto compiled = CompileNativeProgramSet();
        ASSERT_TRUE(compiled.has_value()) << compiled.error().c_str();
        ASSERT_FALSE(compiled->empty());
        EXPECT_GE(NativePrograms::Counts().bound, compiled->size());

        const Vector<Pair> pairs = MakePairs();
        for (const ProgramId id : *compiled) {
            const Program& program = ProgramPool::Get(id);
            const NativeProgramFn function = ProgramPool::Native(id);
            ASSERT_NE(function, nullptr) << Describe(program).c_str() << " is not bound: the generated file is missing or stale";
            for (const Pair& pair : pairs) {
                Context context(NativeProgramSetDeclaration());
                context.Set(0, pair.caster);
                context.Set(1, pair.cast);
                Image generated;
                Image interpreted;
                function(context, generated.bytes);
                BECore::Run(program, context, interpreted.bytes);
                EXPECT_EQ(std::memcmp(generated.bytes, interpreted.bytes, ProgramTypeSize(program.ResultType())), 0) << Describe(program).c_str();
            }
        }
    }

    TEST(NativeProgramEqualityTest, EvaluateRunsTheGeneratedFunctionAndGivesTheInterpretersValue) {
        const auto value = CompileValue<int32_t>("caster.attack * 3 + 7", NativeProgramSetDeclaration());
        ASSERT_TRUE(value.has_value());
        ASSERT_EQ(value->Kind(), ValueKind::Program);
        ASSERT_NE(ProgramPool::Native(value->Id()), nullptr);

        for (const Pair& pair : MakePairs()) {
            Context context(NativeProgramSetDeclaration());
            context.Set(0, pair.caster);
            context.Set(1, pair.cast);
            EXPECT_EQ(value->Evaluate(context), Run<int32_t>(ProgramPool::Get(value->Id()), context));
        }
    }

}  // namespace BECore::Tests
