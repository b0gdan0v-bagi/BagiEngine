#include "UITreeLayout.h"

#include <BECore/UI/UILayout.h>

namespace BECore {

    namespace {

        void ClearRects(UIElement& element) {
            element.rect = {};
            for (const auto& child : element.children) {
                ClearRects(*child);
            }
        }

        void LayoutChildren(UIElement& parent, const Rect& fullBounds, bool parentIsRoot) {
            eastl::vector<UIElement*> visible;
            eastl::vector<UIRectSpec> specs;
            for (const auto& child : parent.children) {
                if (child->active.visible) {
                    visible.push_back(child.Get());
                    specs.push_back(child->active.rect);
                } else {
                    ClearRects(*child);
                }
            }

            if (parent.layout == UILayoutKind::Stack) {
                eastl::vector<Rect> rects;
                UILayout::LayoutStack(parent.rect, parent.active.stack, specs, rects);
                for (size_t i = 0; i < visible.size(); ++i) {
                    visible[i]->rect = rects[i];
                }
            } else {
                for (size_t i = 0; i < visible.size(); ++i) {
                    const Rect& area = parentIsRoot && visible[i]->ignoreSafeArea ? fullBounds : parent.rect;
                    visible[i]->rect = UILayout::ResolveRect(area, specs[i]);
                }
            }

            for (UIElement* child : visible) {
                LayoutChildren(*child, fullBounds, false);
            }
        }

    }  // namespace

    void UITreeLayout::Run(UIElement& root, const Rect& contentBounds, const Rect& fullBounds) {
        root.rect = contentBounds;
        LayoutChildren(root, fullBounds, true);
    }

}  // namespace BECore
