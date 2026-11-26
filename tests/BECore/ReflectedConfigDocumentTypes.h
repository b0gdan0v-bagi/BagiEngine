#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>

namespace BECore::Tests {

    class ProbeDocumentItem : public RefCountedAtomic {
        BE_CLASS(ProbeDocumentItem, FACTORY_BASE, ELEMENT = item)
    public:
        ~ProbeDocumentItem() override = default;
    };

    class ProbeDocumentLeaf : public ProbeDocumentItem {
        BE_CLASS(ProbeDocumentLeaf)
    public:
        BE_REFLECT_FIELD int32_t weight = 1;
    };

    /// A type with a factory list of its own
    class ProbeDocumentNest : public ProbeDocumentItem {
        BE_CLASS(ProbeDocumentNest)
    public:
        BE_REFLECT_FIELD Vector<IntrusivePtr<ProbeDocumentItem>> inner;
    };

    /// A root with a field of its own beside its factory list
    struct ProbeDocumentRoot {
        BE_CLASS(ProbeDocumentRoot)

        BE_REFLECT_FIELD int32_t limit = 3;
        BE_REFLECT_FIELD Vector<IntrusivePtr<ProbeDocumentItem>> items;
    };

}  // namespace BECore::Tests
