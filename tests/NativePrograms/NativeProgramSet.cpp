#include "NativeProgramSet.h"

#include <BECore/ProgramDomainTypes.h>
#include <BECore/ProgramListTypes.h>
#include <BECore/ProgramRowTypes.h>
#include <BECore/ValueListMahjongTypes.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/CompileOptions.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <NativePrograms/NativeProgramFormulas.h>
#include <NativePrograms/NativeProgramSetLists.h>
#include <NativePrograms/NativeProgramSetRows.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        /// Compiles @p text fused and unfused; a program is appended to @p programs unless it is there already, a constant or a slot adds nothing
        template <typename T>
        std::expected<void, String> CompileFormula(StringView text, Vector<ProgramId>& programs) {
            for (const bool fuse : {true, false}) {
                const auto compiled = CompileExpression(text, NativeProgramSetDeclaration(), DescribeType<T>(), CompileOptions{.tables = &NativeProgramSetTables(), .fuse = fuse});
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
            static_cast<void>(result.Add<ProgramListData::ProgramListHand>("hand"));
            static_cast<void>(result.Add<ValueListMahjongData::ValueListHand>("mahjong"));
            return result;
        }();
        return declaration;
    }

    const ValueTableSet& NativeProgramSetTables() {
        static const ValueTableSet tables = [] {
            ValueTableSet result;
            result.Add("NativeDamage", {10.0, 20.0, 30.0, 45.0, 60.0});
            return result;
        }();
        return tables;
    }

    std::expected<Vector<ProgramId>, String> CompileNativeProgramSet() {
        // The programs call operations by id: the registry is the tool's and the tests' to share, filled in the same order before the first formula
        static_cast<void>(ProgramDomainData::RegisterDomainToys());
        Vector<ProgramId> programs;
        const std::expected<void, String> results[] = {
            CompileFormulas<int32_t>(kNativeIntFormulas, programs),
            CompileFormulas<int64_t>(kNativeInt64Formulas, programs),
            CompileFormulas<float>(kNativeFloatFormulas, programs),
            CompileFormulas<double>(kNativeDoubleFormulas, programs),
            CompileFormulas<bool>(kNativeBoolFormulas, programs),
            CompileFormulas<int32_t>(kNativeListIntFormulas, programs),
            CompileFormulas<int64_t>(kNativeListInt64Formulas, programs),
            CompileFormulas<float>(kNativeListFloatFormulas, programs),
            CompileFormulas<double>(kNativeListDoubleFormulas, programs),
            CompileFormulas<bool>(kNativeListBoolFormulas, programs),
            CompileFormulas<bool>(kNativeMahjongBoolFormulas, programs),
            CompileFormulas<int32_t>(kNativeMahjongIntFormulas, programs),
            CompileFormulas<Vec2>(kNativeVec2Formulas, programs),
            CompileFormulas<Vec3>(kNativeVec3Formulas, programs),
            CompileFormulas<Vec4>(kNativeVec4Formulas, programs),
            CompileFormulas<IVec2>(kNativeIVec2Formulas, programs),
            CompileFormulas<Quat>(kNativeQuatFormulas, programs),
            CompileFormulas<Color>(kNativeColorFormulas, programs),
            CompileFormulas<ValueNarrowEnum>(kNativeNarrowEnumFormulas, programs),
            CompileFormulas<ValueShortEnum>(kNativeShortEnumFormulas, programs),
            CompileFormulas<ValueWideEnum>(kNativeWideEnumFormulas, programs),
            CompileFormulas<PoolString>(kNativeTextFormulas, programs),
            CompileFormulas<bool>(kNativeDomainBoolFormulas, programs),
            CompileFormulas<int32_t>(kNativeDomainIntFormulas, programs),
            CompileFormulas<ProgramDomainData::DomainBits256>(kNativeDomainMaskFormulas, programs),
            CompileFormulas<ProgramDomainData::DomainPair>(kNativeDomainPairFormulas, programs),
            CompileFormula<int32_t>(NativeDeepFormula(), programs),
        };
        for (const auto& result : results) {
            if (!result.has_value()) {
                return std::unexpected(result.error());
            }
        }
        if (auto rows = AssembleRowPrograms(programs); !rows.has_value()) {
            return std::unexpected(rows.error());
        }
        if (auto lists = AssembleListPrograms(programs); !lists.has_value()) {
            return std::unexpected(lists.error());
        }
        return programs;
    }

}  // namespace BECore::Tests
