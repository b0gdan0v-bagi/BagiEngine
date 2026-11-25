#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UITransitionComponent.h>
#include <BECore/Utils/FunctionRef.h>
#include <cstdint>

namespace BECore {

    class IRenderer;
    class UIThemes;

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
     *
     * Draw() is what the recursive walk did, element by element in list order: the `opacity` of the element multiplies into the renderer's (the product of the nesting, as `ScopedOpacity`),
     * a running transition composes the view transform and multiplies the opacity again, the theme is resolved when its version moved, an element is drawn in the pointer state of the nearest
     * owner above it, then `DrawSelf` and `DrawComponents`. What changes every frame is read at that time and is no key: `opacity`, the progress of a transition, hover and press. The
     * scopes of an element are closed by index when the list reaches the end of its subtree, in reverse order of opening, so the result equals the nested scopes. Draw() allocates nothing.
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

        /// Draws the main section, then the sections of the popups that are open now (`IsPopupOpen()` of the owner, asked here), each with the renderer's own opacity and view transform:
        /// a popup is not drawn inside the scopes of its ancestors. @p now is the game time of the transitions; @p themes is read for its version once
        void Draw(IRenderer& renderer, double now, UIThemes& themes);

        /// Calls @p visit for each popup element that is open now (asked here), in tree order: the elements `UICanvasLayout::ForEachOpenPopup` finds in the tree this list was made of, with no
        /// walk of the tree. Only those that were visible at the Rebuild(). Allocates nothing; @p visit may call `ClosePopup()` on the element it is given
        void ForEachOpenPopupOwner(FunctionRef<void(UIElement&)> visit) const;

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
        /// What an element that changed the renderer gives back when the list reaches @p end; an element that changed nothing opens none
        struct Scope {
            uint32_t end = 0;
            float opacity = 1.0f;  ///< the renderer's before the element, put back as it was and never divided out
            ViewTransform view;    ///< the renderer's before a transition; only put back when `viewChanged`
            bool viewChanged = false;
        };

        /// The pointer state of an owner, rewritten when the list reaches the owner: before every descendant that is drawn in it
        struct OwnerState {
            UIStateLook::State state;
            float duration = 0.0f;
        };

        void DrawRange(IRenderer& renderer, double now, UIThemes& themes, uint64_t themeVersion, uint32_t begin, uint32_t end);
        void CloseScopes(IRenderer& renderer, uint32_t reached);
        /// Appends @p element and its subtree; @p inSection is false for the main section, where a popup element becomes a PopupSection
        void Append(UIElement& element, int32_t inheritSlot, uint32_t depth, bool inSection);

        Vector<Entry> _entries;
        Vector<PopupSection> _sections;
        uint32_t _mainEnd = 0;
        uint32_t _maxDepth = 0;
        uint32_t _ownerCount = 0;
        Vector<Scope> _scopes;       ///< the open scopes of Draw(); the capacity is `_maxDepth`, set by Rebuild()
        Vector<OwnerState> _owners;  ///< one per owner slot
    };

}  // namespace BECore
