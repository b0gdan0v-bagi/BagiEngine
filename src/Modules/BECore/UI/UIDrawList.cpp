#include "UIDrawList.h"

namespace BECore {

    void UIDrawList::Rebuild(UIElement& root) {
        _entries.clear();
        _sections.clear();
        _mainEnd = 0;
        _maxDepth = 0;
        _ownerCount = 0;

        Append(root, -1, 1, false);
        _mainEnd = static_cast<uint32_t>(_entries.size());
        // The sections are not added to while they are filled: a popup inside a popup is no section (its owner is drawn, its children are not)
        for (PopupSection& section : _sections) {
            UIElement& owner = *_entries[section.owner].element;
            section.begin = static_cast<uint32_t>(_entries.size());
            for (const auto& child : owner.children) {
                Append(*child, -1, 1, true);
            }
            section.end = static_cast<uint32_t>(_entries.size());
        }
    }

    void UIDrawList::Append(UIElement& element, int32_t inheritSlot, uint32_t depth, bool inSection) {
        if (!element.active.visible) {
            return;
        }
        const uint32_t index = static_cast<uint32_t>(_entries.size());
        _maxDepth = std::max(_maxDepth, depth);
        {
            Entry& entry = _entries.emplace_back();
            entry.element = IntrusivePtr<UIElement>(&element);
            entry.transition = IntrusivePtr<UITransitionComponent>(element.GetComponent<UITransitionComponent>());
            entry.inheritSlot = inheritSlot;
            if (element.OwnsState()) {
                entry.ownSlot = static_cast<int32_t>(_ownerCount++);
            }
        }
        const int32_t ownSlot = _entries[index].ownSlot;
        if (inheritSlot < 0) {
            element.SetInheritedState(nullptr, 0.0f);  // nothing is handed down to it, so the per-frame call has nothing to say
        }
        if (element.HasPopupChildren()) {
            if (!inSection) {
                _sections.push_back({index, 0, 0});
            }
        } else {
            const int32_t childSlot = ownSlot >= 0 ? ownSlot : inheritSlot;
            for (const auto& child : element.children) {
                Append(*child, childSlot, depth + 1, inSection);
            }
        }
        _entries[index].subtreeEnd = static_cast<uint32_t>(_entries.size());
    }

}  // namespace BECore
