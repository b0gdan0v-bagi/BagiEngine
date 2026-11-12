#include "UISegmented.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIChoice.h>
#include <Generated/UISegmented.gen.hpp>

namespace BECore {

    eastl::string_view UISegmented::GetSelectedValue() const {
        if (bind.Empty()) {
            return {};
        }
        return CoreManager::GetUIVars().Get(bind);
    }

    void UISegmented::SyncChildren() {
        const eastl::string_view chosen = GetSelectedValue();
        for (const auto& child : children) {
            if (!UIChoice::IsOption(*child)) {
                continue;
            }
            child->selected = !chosen.empty() && UIChoice::GetValue(*child).ToStringView() == chosen;
            child->groupDisabled = disabled;
        }
    }

    void UISegmented::DrawSelf(IRenderer& renderer) {
        UIElement::DrawSelf(renderer);
        SyncChildren();
    }

}  // namespace BECore
