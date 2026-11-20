#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Effects/EffectScript.h>

namespace BECore::EffectsData {

    // A document whose fields are scripts, to load them from content. The .gen.hpp of this header is included in EffectsLoadTest.cpp only; a type is found by its simple name, so the names
    // are unique among all test headers.

    struct EffectsDocument {
        BE_CLASS(EffectsDocument)

        BE_REFLECT_FIELD ContextDecl context;
        BE_REFLECT_FIELD EffectScript onTick;
        BE_REFLECT_FIELD EffectScript onExpire;
    };

}  // namespace BECore::EffectsData
