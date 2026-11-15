#pragma once

#include <BECore/Reflection/FieldAttributes.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cmath>
#include <limits>

namespace BECore::FieldPresentation {

    /// Integer values are rounded to a whole number after an edit, floating ones are kept
    CORE_ENUM(NumberKind, uint8_t, Integer, Float)

    /// Slider = a field with a `range`, Drag = every other number
    CORE_ENUM(NumberWidget, uint8_t, Drag, Slider)

    /// The speed of a drag with no `step`, per kind (ImGui units per pixel of mouse travel)
    inline constexpr double kIntegerDragSpeed = 1.0;
    inline constexpr double kFloatDragSpeed = 0.1;

    /// How the inspector draws and constrains one numeric field: the decisions of `ImGuiPropertyVisitor`, as plain data
    struct Presentation {
        NumberKind kind = NumberKind::Float;
        NumberWidget widget = NumberWidget::Drag;
        double rangeMin = 0.0;  ///< Slider: the lowest value
        double rangeMax = 0.0;  ///< Slider: the highest value
        double speed = kFloatDragSpeed;
        double snapStep = 0.0;  ///< 0: an edit is not snapped
        StringView tooltip{};   ///< empty: none
        bool disabled = false;  ///< `readonly`
    };

    /**
     * @brief The presentation of a numeric field with @p attributes
     *
     * A range with `rangeMin >= rangeMax` and a step `<= 0` count as absent: the field is a drag and an edit is not
     * snapped. `speed` is the step when there is one, the default of the kind otherwise.
     */
    Presentation Describe(const FieldAttributes& attributes, NumberKind kind);

    /// The grid point of `min + k * step` nearest to @p value (halves go up). A step `<= 0` leaves @p value as it is
    double SnapToStep(double value, double min, double step);

    /**
     * @brief The value an edit leaves in the field: clamped to the range and snapped to the step from the range's start
     *        (from 0 with no range), then rounded for an integer kind
     *
     * The result stays inside the range even when its `max` is not on the grid. A field with no range is not clamped.
     */
    double Constrain(const Presentation& presentation, double value);

    /// @p value as a @p T: rounded for an integer type and clamped to the limits of @p T (a double above them does not overflow)
    template <typename T>
    T FromDouble(double value) {
        if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(value);
        } else {
            constexpr double kLowest = static_cast<double>(std::numeric_limits<T>::lowest());
            constexpr double kHighest = static_cast<double>(std::numeric_limits<T>::max());
            const double rounded = std::round(value);
            if (rounded <= kLowest) {
                return std::numeric_limits<T>::lowest();
            }
            if (rounded >= kHighest) {
                return std::numeric_limits<T>::max();
            }
            return static_cast<T>(rounded);
        }
    }

}  // namespace BECore::FieldPresentation
