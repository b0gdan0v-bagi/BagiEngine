#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <cstdint>

namespace BECore::ProgramFusionData {

    // The reflected context object of ProgramFusionTest; its .gen.hpp is included there. A type is found by its simple name, so the name is unique among all test headers.

    /// Two fields of each number a program computes (and an index for tables), so a formula can read two slots of one type
    struct FusionStats {
        BE_CLASS(FusionStats)

        BE_REFLECT_FIELD int32_t i32 = 0;
        BE_REFLECT_FIELD int32_t i32b = 0;
        BE_REFLECT_FIELD int64_t i64 = 0;
        BE_REFLECT_FIELD int64_t i64b = 0;
        BE_REFLECT_FIELD float f32 = 0.0f;
        BE_REFLECT_FIELD float f32b = 0.0f;
        BE_REFLECT_FIELD double f64 = 0.0;
        BE_REFLECT_FIELD double f64b = 0.0;
    };

    /// Two levels over `FusionStats`, `a` and `b`, so a fused instruction reads a slot of each
    struct FusionWorld {
        ContextDecl declaration;
        FusionStats a;
        FusionStats b;
        Context context;

        FusionWorld() {
            static_cast<void>(declaration.Add<FusionStats>("a"));
            static_cast<void>(declaration.Add<FusionStats>("b"));
            context = Context(declaration);
            context.Set(0, a);
            context.Set(1, b);
        }

        FusionWorld(const FusionWorld&) = delete;
        FusionWorld& operator=(const FusionWorld&) = delete;
    };

}  // namespace BECore::ProgramFusionData
