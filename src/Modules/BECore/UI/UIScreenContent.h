#pragma once

#include <BECore/UI/UIVariant.h>

namespace BECore {

    /**
     * @brief What a screen is made of: the elements under the canvas root and the responsive variants.
     *
     * The reflected shape of a `.ui.xml` document (`<ui><elements>...</elements><variants>...</variants></ui>`), and of any other front-end that can
     * drive an IDeserializer: it reads this and gets the same tree the XML front-end gets.
     */
    class UIScreenContent {
        BE_CLASS(UIScreenContent)
    public:
        BE_REFLECT_FIELD eastl::vector<IntrusivePtrNonAtomic<UIElement>> elements;  // not the IntrusivePtr alias: see UIElement::children
        BE_REFLECT_FIELD eastl::vector<UIVariant> variants;
    };

}  // namespace BECore
