#include "UIRectEdit.h"

#include <BECore/UI/UILayout.h>

namespace BECore {

    namespace {

        constexpr float kAbsoluteTolerance = 1e-3f;  // the tolerance of the layout tests
        constexpr float kRelativeTolerance = 1e-5f;  // float noise of a percent that went through a division

        bool Near(float a, float b) {
            const float tolerance = std::max(kAbsoluteTolerance, kRelativeTolerance * std::max(std::abs(a), std::abs(b)));
            return std::abs(a - b) <= tolerance;
        }

        bool SameRect(const Rect& a, const Rect& b) {
            return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.w, b.w) && Near(a.h, b.h);
        }

        /// The numbers of one axis of a spec, and the parent's and the wanted rect's numbers on that axis
        struct Axis {
            UILength& size;
            UILength& offset;
            float& marginStart;
            float& marginEnd;
            float origin;
            float extent;
            float anchorMin;
            float anchorMax;
            float pivot;
            float wantedStart;
            float wantedSize;

            [[nodiscard]] bool Stretched() const {
                return anchorMin < anchorMax;
            }
        };

        Axis AxisX(UIRectSpec& spec, const Rect& parent, const UIEffectiveAnchors& anchors, const Rect& wanted) {
            return {spec.width, spec.x, spec.margin.left, spec.margin.right, parent.x, parent.w, anchors.min.x, anchors.max.x, anchors.pivot.x, wanted.x, wanted.w};
        }

        Axis AxisY(UIRectSpec& spec, const Rect& parent, const UIEffectiveAnchors& anchors, const Rect& wanted) {
            return {spec.height, spec.y, spec.margin.top, spec.margin.bottom, parent.y, parent.h, anchors.min.y, anchors.max.y, anchors.pivot.y, wanted.y, wanted.h};
        }

        /// @p length with the number that resolves to @p units against @p extent; a percent of nothing keeps what it has
        UILength WithResolved(const UILength& length, float units, float extent) {
            if (length.unit == UIUnit::Pixels) {
                return UILength::Px(units);
            }
            return extent > 0.0f ? UILength::Percent(units / extent * 100.0f) : length;
        }

        /// Margins that make the span of a stretched axis start at @p start and be @p size wide
        void SetMargins(Axis& axis, float start, float size) {
            axis.marginStart = start - axis.origin - axis.extent * axis.anchorMin;
            axis.marginEnd = axis.extent * (axis.anchorMax - axis.anchorMin) - axis.marginStart - size;
        }

        /// The first pass: the size the axis is asked for (the limits and the aspect ratio may change it, which the second pass reads back)
        void SetSize(Axis& axis) {
            if (axis.Stretched()) {
                SetMargins(axis, axis.wantedStart, axis.wantedSize);
            } else {
                axis.size = WithResolved(axis.size, axis.wantedSize, axis.extent);
            }
        }

        /// The second pass: @p size is what the layout really gives the axis, so the position lands where it was wanted
        void SetPosition(Axis& axis, float size) {
            if (axis.Stretched()) {
                SetMargins(axis, axis.wantedStart, size);
                return;
            }
            // The inverse of the position rule of UILayout: origin + (extent - size) * pivot + extent * (min - pivot) + offset
            const float units = axis.wantedStart - (axis.origin + (axis.extent - size) * axis.pivot + axis.extent * (axis.anchorMin - axis.pivot));
            axis.offset = WithResolved(axis.offset, units, axis.extent);
        }

        struct Attempt {
            UIRectSpec spec;
            bool exact = false;
        };

        /// One try; an axis in @p keep is not touched
        Attempt TryRect(const Rect& parent, const UIRectSpec& spec, const Rect& wanted, bool keepX, bool keepY) {
            Attempt attempt;
            attempt.spec = spec;
            const UIEffectiveAnchors anchors = UILayout::EffectiveAnchors(spec);

            Axis x = AxisX(attempt.spec, parent, anchors, wanted);
            Axis y = AxisY(attempt.spec, parent, anchors, wanted);
            if (!keepX) {
                SetSize(x);
            }
            if (!keepY) {
                SetSize(y);
            }
            const Rect sized = UILayout::ResolveRect(parent, attempt.spec);
            if (!keepX) {
                SetPosition(x, sized.w);
            }
            if (!keepY) {
                SetPosition(y, sized.h);
            }
            attempt.exact = SameRect(UILayout::ResolveRect(parent, attempt.spec), wanted);
            return attempt;
        }

    }  // namespace

    UIRectEditResult UIRectEdit::SpecForRect(const Rect& parent, const UIRectSpec& spec, const Rect& wanted) {
        const Rect current = UILayout::ResolveRect(parent, spec);
        const bool keepX = Near(current.x, wanted.x) && Near(current.w, wanted.w);
        const bool keepY = Near(current.y, wanted.y) && Near(current.h, wanted.h);

        if (keepX || keepY) {
            Attempt kept = TryRect(parent, spec, wanted, keepX, keepY);
            if (kept.exact) {
                return {std::move(kept.spec), true};
            }
        }
        Attempt all = TryRect(parent, spec, wanted, false, false);
        return {std::move(all.spec), all.exact};
    }

    Optional<UILength> UIRectEdit::ConvertUnit(const UILength& length, UIUnit unit, float parentExtent) {
        if (length.unit == unit) {
            return length;
        }
        const float units = length.Resolve(parentExtent);
        if (unit == UIUnit::Pixels) {
            return UILength::Px(units);
        }
        if (!(parentExtent > 0.0f)) {
            return std::nullopt;
        }
        return UILength::Percent(units / parentExtent * 100.0f);
    }

    Vector<PoolString> UIRectEdit::RegionChoices(const UIRegionSet& regions, PoolString current) {
        Vector<PoolString> names;
        names.reserve(regions.Size() + 1);
        for (const UIRegionSet::Entry& entry : regions) {
            names.push_back(entry.name);
        }
        if (!current.Empty() && eastl::find(names.begin(), names.end(), current) == names.end()) {
            names.push_back(current);
        }
        return names;
    }

}  // namespace BECore
