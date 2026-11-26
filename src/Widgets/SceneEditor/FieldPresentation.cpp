#include "FieldPresentation.h"

namespace BECore::FieldPresentation {

    Presentation Describe(const FieldAttributes& attributes, NumberKind kind) {
        Presentation result;
        result.kind = kind;
        result.speed = kind == NumberKind::Integer ? kIntegerDragSpeed : kFloatDragSpeed;
        result.tooltip = attributes.tooltip;
        result.disabled = attributes.readOnly;

        if (attributes.hasRange && attributes.rangeMin < attributes.rangeMax) {
            result.widget = NumberWidget::Slider;
            result.rangeMin = attributes.rangeMin;
            result.rangeMax = attributes.rangeMax;
            result.speed = (attributes.rangeMax - attributes.rangeMin) * kRangeDragShare;
        }
        if (attributes.step > 0.0) {
            result.snapStep = attributes.step;
            if (result.widget == NumberWidget::Drag) {
                result.speed = attributes.step;
            }
        }
        return result;
    }

    double SnapToStep(double value, double min, double step) {
        if (!(step > 0.0)) {
            return value;
        }
        return min + std::floor((value - min) / step + 0.5) * step;
    }

    double Constrain(const Presentation& presentation, double value) {
        const bool hasRange = presentation.widget == NumberWidget::Slider;
        const bool isInteger = presentation.kind == NumberKind::Integer;
        const double gridStart = hasRange ? presentation.rangeMin : 0.0;
        double result = SnapToStep(value, gridStart, presentation.snapStep);
        if (isInteger) {
            result = std::round(result);
        }
        if (hasRange) {
            // A whole number inside a range with fractional ends: the first one above the start, the last one below the end
            const double low = isInteger ? std::ceil(presentation.rangeMin) : presentation.rangeMin;
            const double high = isInteger ? std::floor(presentation.rangeMax) : presentation.rangeMax;
            result = low <= high ? std::clamp(result, low, high) : presentation.rangeMin;
        }
        return result;
    }

    double Dragged(const Presentation& presentation, double atPress, double pixels) {
        return Constrain(presentation, atPress + pixels * presentation.speed);
    }

    Axis AxisOf(StringView name) {
        if (name == "x") {
            return Axis::X;
        }
        if (name == "y") {
            return Axis::Y;
        }
        return name == "z" ? Axis::Z : Axis::None;
    }

}  // namespace BECore::FieldPresentation
