#include "UIElement.h"

#include <Generated/UIElement.gen.hpp>

namespace BECore {

    IntrusivePtr<UIElement> UIElement::AddChild(PoolString childName) {
        auto child = New<UIElement>();
        child->name = childName;
        children.push_back(child);
        return child;
    }

    UIElement* UIElement::FindByName(PoolString searched) {
        if (name == searched) {
            return this;
        }
        for (const auto& child : children) {
            if (UIElement* found = child->FindByName(searched)) {
                return found;
            }
        }
        return nullptr;
    }

    UIContentFit UIElement::CheckContent(float /*width*/, float /*height*/, float /*scale*/) const {
        return {};
    }

}  // namespace BECore
