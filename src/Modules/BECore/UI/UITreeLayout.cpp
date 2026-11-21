#include "UITreeLayout.h"

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/UI/UILayout.h>

namespace BECore {

    namespace {

        void ClearRects(UIElement& element) {
            element.rect = {};
            for (const auto& child : element.children) {
                ClearRects(*child);
            }
        }

        void LayoutChildren(UIElement& parent, const Rect& contentBounds, const Rect& fullBounds, const UIRegionSet& regions, bool parentIsRoot);

        // The children of a popup element are out of flow: closed they have no rect, open the element places them
        void LayoutPopupChildren(UIElement& parent, const Rect& contentBounds, const Rect& fullBounds, const UIRegionSet& regions) {
            if (!parent.IsPopupOpen()) {
                for (const auto& child : parent.children) {
                    ClearRects(*child);
                }
                return;
            }
            parent.LayoutPopup(contentBounds);
            for (const auto& child : parent.children) {
                if (child->active.visible) {
                    LayoutChildren(*child, contentBounds, fullBounds, regions, false);
                } else {
                    ClearRects(*child);
                }
            }
        }

        void LayoutChildren(UIElement& parent, const Rect& contentBounds, const Rect& fullBounds, const UIRegionSet& regions, bool parentIsRoot) {
            if (parent.HasPopupChildren()) {
                LayoutPopupChildren(parent, contentBounds, fullBounds, regions);
                return;
            }

            // The lists die with this call, and the recursion below nests, so they take the scratch arena and a scope of this call
            const ScratchScope scratch;
            ArenaVector<UIElement*> visible;
            ArenaVector<UIRectSpec> specs;
            visible.reserve(parent.children.size());
            specs.reserve(parent.children.size());
            for (const auto& child : parent.children) {
                if (child->active.visible) {
                    visible.push_back(child.Get());
                    specs.push_back(child->active.rect);
                } else {
                    ClearRects(*child);
                }
            }

            if (parent.layout == UILayoutKind::Stack) {
                ArenaVector<Rect> rects(visible.size());
                UILayout::LayoutStack(parent.rect, parent.active.stack, Span<const UIRectSpec>(specs.data(), specs.size()), Span<Rect>(rects.data(), rects.size()));
                for (size_t i = 0; i < visible.size(); ++i) {
                    visible[i]->rect = rects[i];
                }
            } else {
                for (size_t i = 0; i < visible.size(); ++i) {
                    // A published region is the box of the element; one that is not published leaves it in its parent
                    const Rect* region = regions.Find(specs[i].region);
                    const Rect& area = region ? *region : parentIsRoot && visible[i]->ignoreSafeArea ? fullBounds : parent.rect;
                    visible[i]->rect = UILayout::ResolveRect(area, specs[i]);
                }
            }

            for (UIElement* child : visible) {
                LayoutChildren(*child, contentBounds, fullBounds, regions, false);
            }
        }

    }  // namespace

    void UITreeLayout::Run(UIElement& root, const Rect& contentBounds, const Rect& fullBounds, const UIRegionSet& regions) {
        root.rect = contentBounds;
        LayoutChildren(root, contentBounds, fullBounds, regions, true);
    }

}  // namespace BECore
