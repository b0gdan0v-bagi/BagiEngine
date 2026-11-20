#include "NativeProgramSet.h"

#include <BECore/ProgramRowTypes.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/CompileOptions.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <NativePrograms/NativeProgramSetRows.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        // The formulas of ValueBenchTest, then a few of each type a program computes over these two objects
        constexpr StringView kIntFormulas[] = {
            "caster.attack * (100 + cast.level * 10) / 100", "caster.attack * 3 + 7", "clamp(caster.attack, 0, cast.level * 5)", "select(caster.alive, caster.attack, -caster.attack)",
            "abs(caster.attack) % (cast.level + 1) - 3",
        };
        constexpr StringView kInt64Formulas[] = {"caster.score / 3 + 1", "caster.score + int64(cast.level) * 4000000000"};
        constexpr StringView kFloatFormulas[] = {"lerp(caster.speed, cast.power, 0.25)", "clamp(caster.speed * cast.power, -1.5, 6)", "float(caster.attack) / 4 + floor(cast.power)"};
        constexpr StringView kDoubleFormulas[] = {"caster.precise * 2 + sqrt(2.0)", "round(caster.precise) + ceil(double(cast.power))"};
        constexpr StringView kBoolFormulas[] = {"caster.alive && cast.level > 2", "!caster.alive || caster.attack >= 12 && cast.power != 2.5"};
        constexpr StringView kVec3Formulas[] = {"(caster.velocity + cast.target) * cast.power - caster.velocity * 0.5", "caster.velocity * 2 + vec3(1, 2, 3)"};

        /// Compiles @p text fused and unfused; a program is appended to @p programs unless it is there already, a constant or a slot adds nothing
        template <typename T>
        std::expected<void, String> CompileFormula(StringView text, Vector<ProgramId>& programs) {
            for (const bool fuse : {true, false}) {
                const auto compiled = CompileExpression(text, NativeProgramSetDeclaration(), DescribeType<T>(), CompileOptions{.fuse = fuse});
                if (!compiled.has_value()) {
                    return std::unexpected(Format("{}: {}", text, FormatProgramError(text, compiled.error())));
                }
                if (compiled->kind != CompiledKind::Program || std::find(programs.begin(), programs.end(), compiled->program) != programs.end()) {
                    continue;
                }
                programs.push_back(compiled->program);
            }
            return {};
        }

        template <typename T>
        std::expected<void, String> CompileFormulas(Span<const StringView> formulas, Vector<ProgramId>& programs) {
            for (const StringView text : formulas) {
                if (auto result = CompileFormula<T>(text, programs); !result.has_value()) {
                    return result;
                }
            }
            return {};
        }

    }  // namespace

    const ContextDecl& NativeProgramSetDeclaration() {
        static const ContextDecl declaration = [] {
            ContextDecl result;
            // The order is the level numbers the header names
            static_cast<void>(result.Add<ValueUnitStats>("caster"));
            static_cast<void>(result.Add<ValueCastInfo>("cast"));
            for (const StringView name : {"a", "b", "c"}) {
                static_cast<void>(result.Add<ProgramRowData::ProgramRowOperands>(name));
            }
            return result;
        }();
        return declaration;
    }

    std::expected<Vector<ProgramId>, String> CompileNativeProgramSet() {
        Vector<ProgramId> programs;
        const std::expected<void, String> results[] = {
            CompileFormulas<int32_t>(kIntFormulas, programs),   CompileFormulas<int64_t>(kInt64Formulas, programs), CompileFormulas<float>(kFloatFormulas, programs),
            CompileFormulas<double>(kDoubleFormulas, programs), CompileFormulas<bool>(kBoolFormulas, programs),     CompileFormulas<Vec3>(kVec3Formulas, programs),
        };
        for (const auto& result : results) {
            if (!result.has_value()) {
                return std::unexpected(result.error());
            }
        }
        if (auto rows = AssembleRowPrograms(programs); !rows.has_value()) {
            return std::unexpected(rows.error());
        }
        return programs;
    }

}  // namespace BECore::Tests
