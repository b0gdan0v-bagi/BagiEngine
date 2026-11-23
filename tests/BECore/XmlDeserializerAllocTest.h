#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::Tests {

    /// A factory base whose derived names are over the 23 bytes a small string keeps inside itself, as the type names of components are
    class LongNamedThing : public RefCounted {
        BE_CLASS(LongNamedThing, FACTORY_BASE, ELEMENT = thing)
    public:
        LongNamedThing() = default;
        ~LongNamedThing() override = default;

        BE_REFLECT_FIELD int32_t value = 0;
    };

    class AThingWithAnExtraLongTypeNameForTheFactory : public LongNamedThing {
        BE_CLASS(AThingWithAnExtraLongTypeNameForTheFactory)
    public:
        AThingWithAnExtraLongTypeNameForTheFactory() = default;
        ~AThingWithAnExtraLongTypeNameForTheFactory() override = default;
    };

    struct LongNamedThingList : RefCounted {
        BE_CLASS(LongNamedThingList)

        BE_REFLECT_FIELD Vector<IntrusivePtr<LongNamedThing>> things;
    };

}  // namespace BECore::Tests
