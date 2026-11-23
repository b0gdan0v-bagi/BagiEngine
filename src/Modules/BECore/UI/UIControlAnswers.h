#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIBoxMesh.h>
#include <BECore/UI/UICanvasScaler.h>
#include <BECore/UI/UILayoutValidator.h>
#include <cstdint>

namespace BECore {

    // The answers of `ui_tree` and `ui_click` (`UIControl.cpp`). A float of the engine is a double here, so the text of the widened float stays (`0.1f` is `0.10000000149011612`)

    /// A region the game published for a canvas, in canvas units
    struct UIRegionAnswer {
        BE_CLASS(UIRegionAnswer)

        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD Rect rect;
    };

    /// The view a `SceneView` canvas is laid out against: an empty `name` is the default view
    struct UICanvasViewAnswer {
        BE_CLASS(UICanvasViewAnswer)

        BE_REFLECT_FIELD double height = 0.0;
        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD double width = 0.0;
    };

    /// A finding of the layout validator: `UILayoutIssue` without the `source` pointer
    struct UILayoutIssueAnswer {
        BE_CLASS(UILayoutIssueAnswer)

        BE_REFLECT_FIELD PoolString element;
        BE_REFLECT_FIELD UILayoutIssueKind kind = UILayoutIssueKind::OutsideCanvas;
        BE_REFLECT_FIELD PoolString other;
        BE_REFLECT_FIELD Rect rect;
    };

    struct UIBoxGradientAnswer {
        BE_CLASS(UIBoxGradientAnswer)

        BE_REFLECT_FIELD String color;
        BE_REFLECT_FIELD UIGradientDirection direction = UIGradientDirection::None;
    };

    /// A shadow or a glow of a box
    struct UIShadowAnswer {
        BE_CLASS(UIShadowAnswer)

        BE_REFLECT_FIELD double blur = 0.0;
        BE_REFLECT_FIELD String color;
        BE_REFLECT_FIELD double offsetX = 0.0;
        BE_REFLECT_FIELD double offsetY = 0.0;
        BE_REFLECT_FIELD double spread = 0.0;
    };

    /// The authored box style of an element: each member only when it differs from the default `UIBoxStyle`
    struct UIBoxAnswer {
        BE_CLASS(UIBoxAnswer)

        BE_REFLECT_FIELD Optional<String> borderColor;
        BE_REFLECT_FIELD Optional<double> borderWidth;
        /// Top left, top right, bottom right, bottom left
        BE_REFLECT_FIELD Optional<Vector<double>> cornerRadius;
        BE_REFLECT_FIELD Optional<UIShadowAnswer> glow;
        BE_REFLECT_FIELD Optional<UIBoxGradientAnswer> gradient;
        BE_REFLECT_FIELD Optional<String> highlight;
        BE_REFLECT_FIELD Optional<UIShadowAnswer> shadow;
    };

    /**
     * @brief An element of `ui_tree` with its children.
     *
     * The first six members are always written. An `Optional` is set exactly where the facts exist: `visible`, `rect` and `outputRect` once the canvas is laid out, the rest by the
     * components and the control class of the element. A slider's number is `sliderValue`, not `value`: `value` is the string of a clicker.
     */
    struct UITreeElement {
        BE_CLASS(UITreeElement)

        BE_REFLECT_FIELD Optional<PoolString> action;
        BE_REFLECT_FIELD Optional<PoolString> bind;
        BE_REFLECT_FIELD bool blocksInput = false;
        BE_REFLECT_FIELD Optional<UIBoxAnswer> box;
        BE_REFLECT_FIELD Optional<bool> checked;
        BE_REFLECT_FIELD Vector<UITreeElement> children;
        BE_REFLECT_FIELD Optional<Vector<String>> components;
        BE_REFLECT_FIELD Optional<bool> disabled;
        BE_REFLECT_FIELD Optional<double> fraction;
        BE_REFLECT_FIELD bool hovered = false;
        BE_REFLECT_FIELD Optional<PoolString> icon;
        BE_REFLECT_FIELD bool interactive = false;
        BE_REFLECT_FIELD Optional<double> maxValue;
        BE_REFLECT_FIELD Optional<double> minValue;
        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD Optional<PoolString> offValue;
        BE_REFLECT_FIELD Optional<PoolString> onValue;
        BE_REFLECT_FIELD Optional<bool> open;
        BE_REFLECT_FIELD Optional<Rect> outputRect;
        BE_REFLECT_FIELD bool pressed = false;
        BE_REFLECT_FIELD Optional<Rect> rect;
        BE_REFLECT_FIELD Optional<PoolString> region;
        BE_REFLECT_FIELD Optional<bool> selected;
        BE_REFLECT_FIELD Optional<String> selectedValue;
        BE_REFLECT_FIELD Optional<double> sliderValue;
        BE_REFLECT_FIELD Optional<double> step;
        BE_REFLECT_FIELD Optional<String> text;
        BE_REFLECT_FIELD Optional<String> tooltip;
        /// The element is deeper than `kMaxElementDepth`: its children are left out
        BE_REFLECT_FIELD Optional<bool> truncated;
        BE_REFLECT_FIELD String type;
        BE_REFLECT_FIELD Optional<PoolString> value;
        BE_REFLECT_FIELD Optional<bool> visible;
    };

    /// A canvas of `ui_tree`. `scale`, `bounds`, `safeBounds` and `contentBounds` exist once the canvas is laid out, `view` for a `SceneView` canvas; `issues` is empty before the layout
    struct UICanvasAnswer {
        BE_CLASS(UICanvasAnswer)

        BE_REFLECT_FIELD Optional<Rect> bounds;
        BE_REFLECT_FIELD Optional<Rect> contentBounds;
        BE_REFLECT_FIELD Vector<UITreeElement> elements;
        BE_REFLECT_FIELD bool inputEnabled = false;
        BE_REFLECT_FIELD int32_t inputOrder = 0;
        BE_REFLECT_FIELD Vector<UILayoutIssueAnswer> issues;
        BE_REFLECT_FIELD bool laidOut = false;
        BE_REFLECT_FIELD bool modal = false;
        BE_REFLECT_FIELD PoolString node;
        BE_REFLECT_FIELD Vector<UIRegionAnswer> regions;
        BE_REFLECT_FIELD Optional<Rect> safeBounds;
        BE_REFLECT_FIELD Optional<double> scale;
        BE_REFLECT_FIELD CanvasSpace space = CanvasSpace::Screen;
        BE_REFLECT_FIELD bool takesInput = false;
        BE_REFLECT_FIELD Optional<UICanvasViewAnswer> view;
        BE_REFLECT_FIELD bool visible = false;
    };

    /// `ui_tree`
    struct UITreeAnswer {
        BE_CLASS(UITreeAnswer)

        BE_REFLECT_FIELD Vector<UICanvasAnswer> canvases;
        BE_REFLECT_FIELD PoolString scene;
    };

    /// `ui_click`. `action` and `value` are both set or both absent: set for a button, a toggle, a slider and an option of a dropdown, empty when the press emitted nothing
    struct UIClickAnswer {
        BE_CLASS(UIClickAnswer)

        BE_REFLECT_FIELD Optional<PoolString> action;
        BE_REFLECT_FIELD PoolString canvas;
        BE_REFLECT_FIELD String clicked;
        BE_REFLECT_FIELD bool consumed = false;
        /// `none`, `element` or `target`
        BE_REFLECT_FIELD PoolString takenBy;
        BE_REFLECT_FIELD Optional<PoolString> value;
        BE_REFLECT_FIELD double x = 0.0;
        BE_REFLECT_FIELD double y = 0.0;
    };

}  // namespace BECore
