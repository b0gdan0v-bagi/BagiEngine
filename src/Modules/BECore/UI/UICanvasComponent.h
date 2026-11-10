#pragma once

#include <BECore/Input/UIInput.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/UI/UICanvasLayout.h>

namespace BECore {

    class IRenderer;
    class Scene;

    /**
     * @brief A canvas of in-game UI on a scene node: the unit that owns a coordinate space, a scale and an input order. A scene may have many.
     *
     * The reflected fields choose the space and the scaler (UICanvasScaler); the element tree, the variants and the cached layout are a UICanvasLayout, which
     * the component feeds with the context of each frame (output size, window metrics, the scene's view size, the node's box for a World canvas).
     *
     * **Drawing.** `OnDraw()` runs in the scene's draw order (node layer, then scene order), sets the canvas's own view transform for its elements and restores the scene's
     * afterwards. Elements here are plain rects with a color; the controls come with the XML front-end.
     *
     * **Input.** The canvas registers as a UIInput handler, with the draw rank of its node as the order: the topmost canvas is asked first, and a `modal`
     * canvas consumes every mouse event while it is shown, so nothing below it and no game code sees one. Otherwise a canvas consumes an event that lands on
     * a hit-testable element (motion over one is consumed as a hover, unless a button is held whose press the canvas did not take: that drag is the game's),
     * uses the layout of its last draw, and follows a press that it consumed to the release. The press of an `interactive` element and the motion while it is held reach the element through
     * `UIElement::OnPress` / `OnDrag` (a slider acts on them); the click on the release is still `OnClick`. A canvas of an inactive scene, a hidden one (`visible`) and one with `inputEnabled` off
     * never consume anything.
     *
     * **Popups** (`UIElement::HasPopupChildren`, a dropdown's list). The children of an open popup are drawn after the whole tree and hit-tested before it. A left press that lands on neither an open
     * popup element nor inside it closes the popup and is consumed (the release too; no click on what is below, no pressed element). A click that ends inside an open popup goes to
     * `UIElement::OnPopupChoice` of the popup instead of the child's `OnClick`. Opening one popup closes another; a canvas that cannot take input closes its popups. The canvas
     * invalidates its layout when the set of open popups changed (the layout cache does not see element edits), at the latest on the next draw.
     */
    class UICanvasComponent : public IComponent, public IUIInputHandler {
        BE_CLASS(UICanvasComponent)
    public:
        UICanvasComponent() = default;
        ~UICanvasComponent() override;

        BE_REFLECT_FIELD CanvasSpace _space = CanvasSpace::Screen;
        BE_REFLECT_FIELD CanvasScaleMode _scaleMode = CanvasScaleMode::ScaleWithScreen;
        BE_REFLECT_FIELD float _referenceWidth = 1280.0f;
        BE_REFLECT_FIELD float _referenceHeight = 720.0f;
        BE_REFLECT_FIELD float _match = 0.5f;
        BE_REFLECT_FIELD float _minScale = 0.25f;
        BE_REFLECT_FIELD float _maxScale = 8.0f;
        BE_REFLECT_FIELD bool _useSafeArea = false;
        BE_REFLECT_FIELD bool _extendToOutput = false;  ///< SceneView only: bounds are the whole output in scene units, so the canvas also reaches the bars around the view (UICanvasScaler)
        BE_REFLECT_FIELD bool _modal = false;           ///< while shown, no mouse event gets past this canvas
        BE_REFLECT_FIELD bool _visible = true;          ///< drawn and, with `inputEnabled`, takes input
        BE_REFLECT_FIELD bool _inputEnabled = true;

        /// A screen in a file of its own, a UIDocument such as `assets/ui/hud.ui.xml`. Its elements and variants come first, the inline ones below come after them
        /// (drawn on top, and a later variant wins). The elements of a file belong to the file: they are not saved back with the scene
        BE_REFLECT_FIELD PoolString _source;

        /// The elements and variants written inline in the scene file (`<elements><element type="UIPanel" .../></elements>`, `<variants>`). Initialize() hands them to
        /// the layout; they stay here as the part of the screen that is saved back. Elements added in code are not in this list
        BE_REFLECT_FIELD eastl::vector<IntrusivePtrNonAtomic<UIElement>> _elements;  // not the IntrusivePtr alias: the generator looks for the long name to find a factory field
        BE_REFLECT_FIELD eastl::vector<UIVariant> _variants;

        void Initialize() override;
        void OnAttached() override;
        void OnDraw() override;

        bool OnMouseButtonDown(const SDL_MouseButtonEvent& event) override;
        bool OnMouseButtonUp(const SDL_MouseButtonEvent& event) override;
        bool OnMouseMotion(const SDL_MouseMotionEvent& event) override;
        bool OnMouseWheel(const SDL_MouseWheelEvent& event) override;

        /// The layout: the tree (`root`), the variants and the result of the last draw. After editing the tree or the variants call Invalidate()
        [[nodiscard]] UICanvasLayout& GetLayout() {
            return _layout;
        }
        [[nodiscard]] const UICanvasLayout& GetLayout() const {
            return _layout;
        }
        [[nodiscard]] UIElement& GetRoot() {
            return *_layout.root;
        }
        void Invalidate() {
            _layout.Invalidate();
        }

        /// Lays the canvas out for an output of this size now, as the next draw would. Draw() does it itself; this is for a caller that needs the rects before one
        void UpdateLayout(float outputWidth, float outputHeight);

        /// The place in the input order that this canvas has registered with (see UIInput::Register)
        [[nodiscard]] int GetInputOrder() const {
            return _inputOrder;
        }

        /// True while the canvas is shown, has input on and its scene is the active one: the only time it consumes an event
        [[nodiscard]] bool TakesInput() const;

        /// True between a mouse press that this canvas took and its release: the canvas keeps consuming the motion in between
        [[nodiscard]] bool HoldsPress() const {
            return _consumedButtons != 0;
        }

        /// Shows or hides every canvas on a node with this name in @p scene; false when there is none
        static bool SetVisibleByName(Scene& scene, PoolString nodeName, bool visible);

        /// Shows or hides the first element of this canvas with this name (its authored `style.visible`; a variant that sets `visible` still wins); false when there is none
        bool SetElementVisible(PoolString elementName, bool visible);

        /// The same in every canvas of @p scene; true when at least one element was found
        static bool SetElementVisibleByName(Scene& scene, PoolString elementName, bool visible);

    private:
        [[nodiscard]] UILayoutContext MakeContext(float outputWidth, float outputHeight) const;
        void SyncParams();
        void AdoptDocument();
        void DrawElement(IRenderer& renderer, UIElement& element) const;
        void DrawPopups(IRenderer& renderer) const;
        void RefreshInputOrder();

        /// The element under a point in output pixels, by the layout of the last draw
        [[nodiscard]] UIElement* HitTestOutput(float outputX, float outputY) const;
        // Pointer state of the interactive elements (UIElement::hovered, pressed, OnClick)
        void SetHovered(UIElement* element);
        void ClearPointerState();

        /// The open popups changed since the last look (opened, closed, a second one opened): keeps the newest, closes the others, invalidates the layout. @return true on a change
        bool SyncPopups();
        void CloseAllPopups();
        /// The open popup whose children contain @p element (the element itself or something under it) and, in @p child, that child of the popup; null when there is none
        [[nodiscard]] UIElement* FindPopupOf(const UIElement& element, UIElement*& child) const;
        /// @p element is an open popup or lies inside one
        [[nodiscard]] bool IsInOpenPopup(const UIElement* element) const;
        /// Ends the press of the left button: the element it began on when the release is on it too, else null
        [[nodiscard]] IntrusivePtr<UIElement> TakeClick(const SDL_MouseButtonEvent& event);
        [[nodiscard]] bool ConsumeButtonUp(const SDL_MouseButtonEvent& event);

        UICanvasLayout _layout;
        int _inputOrder = 0;
        bool _registered = false;
        uint32_t _consumedButtons = 0;     // bit per SDL button that went down on this canvas and is not up yet
        IntrusivePtr<UIElement> _hovered;  // the element under the pointer; kept alive here so that a tree edit cannot leave a dangling flag
        IntrusivePtr<UIElement> _pressed;  // the interactive element the left button went down on and is not up yet
        IntrusivePtr<UIElement> _popup;    // the open popup the last SyncPopups() saw
        bool _extendWarned = false;        // the ignored `extendToOutput` of a Screen or World canvas was reported
        bool _authoredAdopted = false;     // Initialize() has handed `_elements` and `_variants` to the layout
    };

}  // namespace BECore
