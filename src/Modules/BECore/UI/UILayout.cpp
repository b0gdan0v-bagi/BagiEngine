#include "UILayout.h"

namespace BECore {

    namespace {

        constexpr float kEpsilon = 1e-3f;  // a line is not broken for rounding noise

        float ClampExtent(float extent, float parentExtent, const std::optional<UILength>& minimum, const std::optional<UILength>& maximum) {
            if (maximum) {
                extent = std::min(extent, maximum->Resolve(parentExtent));
            }
            if (minimum) {
                extent = std::max(extent, minimum->Resolve(parentExtent));
            }
            return std::max(extent, 0.0f);
        }

        void ApplyAspect(float& width, float& height, const UIRectSpec& spec) {
            if (spec.aspectRatio <= 0.0f || width <= 0.0f || height <= 0.0f) {
                return;
            }
            const bool boxIsWider = width / height > spec.aspectRatio;
            const bool narrowTheWidth = (spec.aspectMode == UIAspectMode::Fit) == boxIsWider;
            if (narrowTheWidth) {
                width = height * spec.aspectRatio;
            } else {
                height = width / spec.aspectRatio;
            }
        }

        struct ChildSize {
            float main = 0.0f;   // the size along the stack's direction
            float cross = 0.0f;  // the size across it
            float flex = 0.0f;
            std::optional<UILength> minMain, maxMain, minCross, maxCross;
        };

        struct Line {
            size_t first = 0;
            size_t count = 0;
        };

    }  // namespace

    Rect UILayout::Inset(const Rect& rect, const UIEdges& edges) {
        return {rect.x + edges.left, rect.y + edges.top, std::max(rect.w - edges.left - edges.right, 0.0f), std::max(rect.h - edges.top - edges.bottom, 0.0f)};
    }

    Rect UILayout::ResolveRect(const Rect& parent, const UIRectSpec& spec) {
        const bool stretch = spec.anchor == UIAnchor::Stretch;
        const Rect area = stretch ? Inset(parent, spec.margin) : parent;

        float width = stretch ? area.w : spec.width.Resolve(parent.w);
        float height = stretch ? area.h : spec.height.Resolve(parent.h);
        width = ClampExtent(width, parent.w, spec.minWidth, spec.maxWidth);
        height = ClampExtent(height, parent.h, spec.minHeight, spec.maxHeight);
        ApplyAspect(width, height, spec);

        if (stretch) {
            return {area.x + (area.w - width) * 0.5f, area.y + (area.h - height) * 0.5f, width, height};
        }
        const auto [fractionX, fractionY] = AnchorFraction(spec.anchor);
        return {parent.x + (parent.w - width) * fractionX + spec.x.Resolve(parent.w), parent.y + (parent.h - height) * fractionY + spec.y.Resolve(parent.h), width, height};
    }

    void UILayout::LayoutStack(const Rect& parent, const UIStackSpec& stack, eastl::span<const UIRectSpec> children, eastl::vector<Rect>& rects) {
        rects.assign(children.size(), Rect{});
        if (children.empty()) {
            return;
        }

        const Rect content = Inset(parent, stack.padding);
        const bool vertical = stack.direction == UIStackDirection::Vertical;
        const float mainExtent = vertical ? content.h : content.w;
        const float crossExtent = vertical ? content.w : content.h;

        // The sizes of the children as their specs give them, before flex and stretch
        eastl::vector<ChildSize> sizes(children.size());
        for (size_t i = 0; i < children.size(); ++i) {
            const UIRectSpec& spec = children[i];
            ChildSize& size = sizes[i];
            const UILength mainLength = vertical ? spec.height : spec.width;
            const UILength crossLength = vertical ? spec.width : spec.height;
            size.minMain = vertical ? spec.minHeight : spec.minWidth;
            size.maxMain = vertical ? spec.maxHeight : spec.maxWidth;
            size.minCross = vertical ? spec.minWidth : spec.minHeight;
            size.maxCross = vertical ? spec.maxWidth : spec.maxHeight;
            size.flex = std::max(spec.flex, 0.0f);
            size.main = ClampExtent(mainLength.Resolve(mainExtent), mainExtent, size.minMain, size.maxMain);
            size.cross = ClampExtent(crossLength.Resolve(crossExtent), crossExtent, size.minCross, size.maxCross);
        }

        // Lines: one, or as many as the main axis needs when the stack wraps
        eastl::vector<Line> lines;
        lines.push_back({0, 0});
        float lineMain = 0.0f;
        for (size_t i = 0; i < sizes.size(); ++i) {
            Line& line = lines.back();
            const float needed = line.count == 0 ? sizes[i].main : lineMain + stack.spacing + sizes[i].main;
            if (stack.wrap && line.count > 0 && needed > mainExtent + kEpsilon) {
                lines.push_back({i, 0});
                lineMain = 0.0f;
            }
            Line& target = lines.back();
            lineMain = target.count == 0 ? sizes[i].main : lineMain + stack.spacing + sizes[i].main;
            ++target.count;
        }

        float crossCursor = 0.0f;
        for (const Line& line : lines) {
            // Flex children share what is left of the line
            float fixedMain = stack.spacing * static_cast<float>(line.count - 1);
            float totalFlex = 0.0f;
            for (size_t i = line.first; i < line.first + line.count; ++i) {
                if (sizes[i].flex > 0.0f) {
                    totalFlex += sizes[i].flex;
                } else {
                    fixedMain += sizes[i].main;
                }
            }
            if (totalFlex > 0.0f) {
                const float freeMain = std::max(mainExtent - fixedMain, 0.0f);
                for (size_t i = line.first; i < line.first + line.count; ++i) {
                    if (sizes[i].flex > 0.0f) {
                        sizes[i].main = ClampExtent(freeMain * sizes[i].flex / totalFlex, mainExtent, sizes[i].minMain, sizes[i].maxMain);
                    }
                }
            }

            // Without wrap the line is the whole content on the cross axis; a wrapped line is as wide as its widest child
            float lineCross = crossExtent;
            if (stack.wrap) {
                lineCross = 0.0f;
                for (size_t i = line.first; i < line.first + line.count; ++i) {
                    lineCross = std::max(lineCross, sizes[i].cross);
                }
            }

            float mainCursor = 0.0f;
            for (size_t i = line.first; i < line.first + line.count; ++i) {
                float cross = sizes[i].cross;
                if (stack.crossAlign == UIStackAlign::Stretch) {
                    cross = ClampExtent(lineCross, crossExtent, sizes[i].minCross, sizes[i].maxCross);
                }
                float crossOffset = 0.0f;
                if (stack.crossAlign == UIStackAlign::Center) {
                    crossOffset = (lineCross - cross) * 0.5f;
                } else if (stack.crossAlign == UIStackAlign::End) {
                    crossOffset = lineCross - cross;
                }

                const float main = sizes[i].main;
                rects[i] =
                    vertical ? Rect{content.x + crossCursor + crossOffset, content.y + mainCursor, cross, main} : Rect{content.x + mainCursor, content.y + crossCursor + crossOffset, main, cross};
                mainCursor += main + stack.spacing;
            }
            crossCursor += lineCross + stack.spacing;
        }
    }

}  // namespace BECore
