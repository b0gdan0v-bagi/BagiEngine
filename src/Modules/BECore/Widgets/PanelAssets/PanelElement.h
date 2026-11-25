#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief One element of a panel asset: the base of the factory, so `<element type="PanelButton" .../>` makes a derived kind
     *
     * The data is untrusted (a file the owner or a game edits, read again at every hot reload), so every text is a `String`: a
     * `PoolString` would never be freed. `visible` is a condition, a binding of a bool field with an optional leading `!`
     * (`PanelBinding.h`); empty is always visible. `elements` holds the children of the container kinds (`PanelSection`, `PanelRow`, `PanelChild`, `PanelPopup`);
     * the loader reports children of any other kind. A loaded element of an unknown `type` becomes a plain `PanelElement`, which the
     * deserializer reports, so the tree is always safe to walk.
     */
    class PanelElement : public RefCounted {
        BE_CLASS(PanelElement, FACTORY_BASE, ELEMENT = element)
    public:
        PanelElement() = default;
        ~PanelElement() override = default;

        BE_REFLECT_FIELD String id;
        BE_REFLECT_FIELD String visible;
        BE_REFLECT_FIELD String tooltip;
        BE_REFLECT_FIELD Vector<IntrusivePtr<PanelElement>> elements;
    };

}  // namespace BECore
