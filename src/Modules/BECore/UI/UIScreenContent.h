#pragma once

#include <BECore/UI/UIVariant.h>

namespace BECore {

    /**
     * @brief What a screen is made of: the elements under the canvas root and the responsive variants.
     *
     * The reflected shape of a `.ui.xml` document, and of any other front-end that can drive an IDeserializer.
     */
    class UIScreenContent {
        BE_CLASS(UIScreenContent)
    public:
        BE_REFLECT_FIELD Vector<IntrusivePtrNonAtomic<UIElement>> elements;  // not the IntrusivePtr alias: see UIElement::children
        BE_REFLECT_FIELD Vector<UIVariant> variants;
    };

}  // namespace BECore
