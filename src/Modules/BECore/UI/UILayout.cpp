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

        /// One axis of an anchor layout once its size is known
        struct AxisSize {
            bool stretched = false;
            float spanStart = 0.0f;  // the span between the two anchors minus the margins (a stretched axis)
            float spanExtent = 0.0f;
            float size = 0.0f;
        };

        /// Replaces @p value by @p explicitValue when that is a finite number
        void ReplaceFinite(float& value, float explicitValue) {
            if (std::isfinite(explicitValue)) {
                value = explicitValue;
            }
        }

        void Replace(Vec2& value, const std::optional<Vec2>& explicitValue) {
            if (explicitValue) {
                ReplaceFinite(value.x, explicitValue->x);
                ReplaceFinite(value.y, explicitValue->y);
            }
        }

        AxisSize ResolveAxisSize(float origin, float extent, float anchorMin, float anchorMax, float marginStart, float marginEnd, const UILength& length, const std::optional<UILength>& minimum,
                                 const std::optional<UILength>& maximum) {
            AxisSize axis;
            axis.stretched = anchorMin < anchorMax;
            float size;
            if (axis.stretched) {
                axis.spanStart = origin + extent * anchorMin + marginStart;
                axis.spanExtent = std::max(extent * (anchorMax - anchorMin) - marginStart - marginEnd, 0.0f);
                size = axis.spanExtent;
            } else {
                size = length.Resolve(extent);
            }
            axis.size = ClampExtent(size, extent, minimum, maximum);
            return axis;
        }

        float ResolveAxisPosition(const AxisSize& axis, float origin, float extent, float anchorMin, float pivot, float offset) {
            if (axis.stretched) {
                return axis.spanStart + (axis.spanExtent - axis.size) * pivot;
            }
            // origin + extent * anchorMin - size * pivot, written so that anchorMin == pivot adds exactly 0 to the form every file has always had
            return origin + (extent - axis.size) * pivot + extent * (anchorMin - pivot) + offset;
        }

        /// A side the spec states: a percent (even 0%) or a non-zero number of units; a bare 0 px is "not given"
        bool IsGiven(const UILength& length) {
            return length.unit == UIUnit::Percent || length.value != 0.0f;
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

    UIEffectiveAnchors UILayout::EffectiveAnchors(const UIRectSpec& spec) {
        UIEffectiveAnchors anchors;
        if (spec.anchor == UIAnchor::Stretch) {
            anchors.min = {0.0f, 0.0f};
            anchors.max = {1.0f, 1.0f};
            anchors.pivot = {0.5f, 0.5f};
        } else {
            const auto [fractionX, fractionY] = AnchorFraction(spec.anchor);
            anchors.min = anchors.max = anchors.pivot = {fractionX, fractionY};
        }
        Replace(anchors.min, spec.anchorMin);
        Replace(anchors.max, spec.anchorMax);
        Replace(anchors.pivot, spec.pivot);
        if (anchors.min.x > anchors.max.x) {
            std::swap(anchors.min.x, anchors.max.x);
        }
        if (anchors.min.y > anchors.max.y) {
            std::swap(anchors.min.y, anchors.max.y);
        }
        return anchors;
    }

    Rect UILayout::ResolveRect(const Rect& parent, const UIRectSpec& spec) {
        const UIEffectiveAnchors anchors = EffectiveAnchors(spec);
        AxisSize axisX = ResolveAxisSize(parent.x, parent.w, anchors.min.x, anchors.max.x, spec.margin.left, spec.margin.right, spec.width, spec.minWidth, spec.maxWidth);
        AxisSize axisY = ResolveAxisSize(parent.y, parent.h, anchors.min.y, anchors.max.y, spec.margin.top, spec.margin.bottom, spec.height, spec.minHeight, spec.maxHeight);

        ApplyAspect(axisX.size, axisY.size, spec);

        return {ResolveAxisPosition(axisX, parent.x, parent.w, anchors.min.x, anchors.pivot.x, spec.x.Resolve(parent.w)),
                ResolveAxisPosition(axisY, parent.y, parent.h, anchors.min.y, anchors.pivot.y, spec.y.Resolve(parent.h)), axisX.size, axisY.size};
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

            // A fixed child with an aspect ratio and one side given gets the other (a flex child and a child with both or none are as their spec says)
            if (spec.aspectRatio > 0.0f && size.flex <= 0.0f && IsGiven(spec.width) != IsGiven(spec.height)) {
                if (IsGiven(spec.width)) {
                    const float width = vertical ? size.cross : size.main;
                    const float height = ClampExtent(width / spec.aspectRatio, content.h, spec.minHeight, spec.maxHeight);
                    (vertical ? size.main : size.cross) = height;
                } else {
                    const float height = vertical ? size.main : size.cross;
                    const float width = ClampExtent(height * spec.aspectRatio, content.w, spec.minWidth, spec.maxWidth);
                    (vertical ? size.cross : size.main) = width;
                }
            }
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
