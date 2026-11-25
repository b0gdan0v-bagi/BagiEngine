#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UITransitionComponent.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief What a canvas draws, as a flat list: the visible elements in pre-order, then the children of the popups, rebuilt when the tree changes.
     *
     * Rebuild() is pure: it reads the tree and makes the list, with no renderer and no scene. The order is the one the recursive walk drew in: an element, then its subtree; an element with
     * `active.visible` false and its subtree have no entry. The children of an element with `HasPopupChildren()` get none in the main section (the element itself does): each such element is
     * a PopupSection, whose entries (its children's subtrees, in tree order) follow the main section, so the open ones are drawn after the whole tree.
     *
     * What is kept in the list is what only an edit of the tree or of an element's `components` can change (**edit, then `UICanvasComponent::Invalidate()`**): who is visible, the transition
     * component, who owns the pointer state and whose state a descendant is drawn in. An entry holds its element by `IntrusivePtr`, so an edit without `Invalidate()` leaves a stale list
     * that draws a removed element once more, never a dangling one.
     */
    class UIDrawList {
    public:
        struct Entry {
            IntrusivePtr<UIElement> element;
            uint32_t subtreeEnd = 0;                         ///< the index after the last descendant: where the scopes of this element close
            IntrusivePtr<UITransitionComponent> transition;  ///< null for none
            int32_t ownSlot = -1;                            ///< this element `OwnsState()`: its place among the owners, else -1
            int32_t inheritSlot = -1;                        ///< the slot of the nearest ancestor that owns the state, -1 for none; a child of a popup element starts again at -1
        };

        /// The children of one popup element: `owner` is its entry in the main section, `[begin, end)` the entries of their subtrees
        struct PopupSection {
            uint32_t owner = 0;
            uint32_t begin = 0;
            uint32_t end = 0;
        };

        /// Makes the list of the tree under @p root (the root is entry 0) and, for an element that no owner of the state hands anything down to, clears its inherited state once
        void Rebuild(UIElement& root);

        [[nodiscard]] const Vector<Entry>& Entries() const {
            return _entries;
        }
        /// The popup elements of the main section, in tree order (open or not: whether one is open is asked when it is drawn)
        [[nodiscard]] const Vector<PopupSection>& Sections() const {
            return _sections;
        }
        /// The end of the main section: the entries before it are the tree, the ones after it the popup sections
        [[nodiscard]] uint32_t MainEnd() const {
            return _mainEnd;
        }
        /// The deepest nesting of one section (the root is depth 1): the most scopes that can be open at once
        [[nodiscard]] uint32_t MaxDepth() const {
            return _maxDepth;
        }
        /// How many elements own the pointer state: the slots are `[0, OwnerCount())`
        [[nodiscard]] uint32_t OwnerCount() const {
            return _ownerCount;
        }

    private:
        /// Appends @p element and its subtree; @p inSection is false for the main section, where a popup element becomes a PopupSection
        void Append(UIElement& element, int32_t inheritSlot, uint32_t depth, bool inSection);

        Vector<Entry> _entries;
        Vector<PopupSection> _sections;
        uint32_t _mainEnd = 0;
        uint32_t _maxDepth = 0;
        uint32_t _ownerCount = 0;
    };

}  // namespace BECore
