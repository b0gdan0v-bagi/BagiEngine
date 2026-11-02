#pragma once

#include <BECore/Input/UIInput.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/UI/UICanvasScaler.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUiHost/RmlUiVarsDefinition.h>

namespace Rml {

    class Context;
    class ElementDocument;

}  // namespace Rml

namespace BECore {

    class IRenderer;

    /**
     * @brief A scene component that draws one RmlUi document (HTML-like markup and CSS-like styles in `.rml` / `.rcss`) and routes the mouse to it.
     *
     * This is the third candidate for authoring a screen (`.claude/plans/ui-authoring-decision.md`): the document has a DOM and a layout engine of its own, so it is not
     * a UIElement tree, and the scene editor, the validator and the control API's tree do not see into it. What it shares with a UICanvasComponent is the seam:
     * the same space / scale mode / safe area fields resolve to a rect and a scale with UICanvasScaler; it is drawn in the scene's draw order; it registers with UIInput in
     * the same order and consumes a mouse event that lands on an element; a `data-action` attribute on an element (a `<button>`) emits `InputEvents::ActionEvent` on click;
     * and the variables of CoreManager::GetUIVars() are readable in the document as `{{vars.name}}`.
     *
     * **Units.** The context is the size of the resolved rect in output pixels and its density-independent pixel ratio is the canvas scale, so `1dp` is one UI unit (what a
     * px is in a UICanvasComponent screen) and text is rasterised at its final size. A document sized in `dp` and `%` follows the scaler like the other front-ends do.
     * `@media` queries see the context in pixels.
     *
     * **Input.** Everything under the pointer takes the event, including `body`: a wrapper that must let clicks through says `pointer-events: none`, the same decision
     * the HTML front-end makes with its "a group takes clicks only with a background" rule. A `click` is queued by the listener and the action is emitted after RmlUi has
     * finished with the event, because a handler may switch scenes and destroy this component.
     */
    class RmlUiDocumentComponent : public IComponent, public IUIInputHandler {
        BE_CLASS(RmlUiDocumentComponent)
    public:
        RmlUiDocumentComponent() = default;
        ~RmlUiDocumentComponent() override;

        BE_REFLECT_FIELD CanvasSpace _space = CanvasSpace::Screen;
        BE_REFLECT_FIELD CanvasScaleMode _scaleMode = CanvasScaleMode::ScaleWithScreen;
        BE_REFLECT_FIELD float _referenceWidth = 1280.0f;
        BE_REFLECT_FIELD float _referenceHeight = 720.0f;
        BE_REFLECT_FIELD float _match = 0.5f;
        BE_REFLECT_FIELD float _minScale = 0.25f;
        BE_REFLECT_FIELD float _maxScale = 8.0f;
        BE_REFLECT_FIELD bool _useSafeArea = false;
        BE_REFLECT_FIELD bool _modal = false;
        BE_REFLECT_FIELD bool _visible = true;
        BE_REFLECT_FIELD bool _inputEnabled = true;

        /// The document to load, a virtual path such as `assets/ui/menu.rml`. Empty: the document comes from SetMarkup()
        BE_REFLECT_FIELD PoolString _source;

        void Initialize() override;
        void OnAttached() override;
        void OnDraw() override;

        bool OnMouseButtonDown(const SDL_MouseButtonEvent& event) override;
        bool OnMouseButtonUp(const SDL_MouseButtonEvent& event) override;
        bool OnMouseMotion(const SDL_MouseMotionEvent& event) override;
        bool OnMouseWheel(const SDL_MouseWheelEvent& event) override;

        /// Replaces the document with this markup (tests, generated screens). @return false when RmlUi refused it
        bool SetMarkup(const eastl::string& markup);

        /// Lays the document out for an output of this size and runs the data-model update, as the next draw would, without drawing. For a test that reads the boxes
        void UpdateLayout(float outputWidth, float outputHeight);

        [[nodiscard]] Rml::Context* GetContext() const {
            return _context;
        }
        [[nodiscard]] Rml::ElementDocument* GetDocument() const {
            return _document;
        }

        /// Where the top-left corner of the context is in output pixels, and its size in pixels, as of the last layout
        [[nodiscard]] float GetOriginX() const {
            return _originX;
        }
        [[nodiscard]] float GetOriginY() const {
            return _originY;
        }
        [[nodiscard]] int GetContextWidth() const {
            return _width;
        }
        [[nodiscard]] int GetContextHeight() const {
            return _height;
        }

        [[nodiscard]] int GetInputOrder() const {
            return _inputOrder;
        }

    private:
        /// Queues the `data-action` of the clicked element
        class ActionListener final : public Rml::EventListener {
        public:
            void ProcessEvent(Rml::Event& event) override;
            eastl::vector<PoolString> pending;
        };

        [[nodiscard]] UILayoutContext MakeContext(float outputWidth, float outputHeight) const;
        [[nodiscard]] UICanvasScaleParams MakeParams() const;
        void RefreshInputOrder();
        [[nodiscard]] bool TakesInput() const;
        bool CreateContext();
        void DestroyContext();
        void AttachDocument(Rml::ElementDocument* document);
        void SyncVars();
        void FlushActions();

        /// Moves the pointer of the context to an output position. @return true when the pointer is over something that takes the event
        bool MovePointer(float outputX, float outputY);

        Rml::Context* _context = nullptr;
        Rml::ElementDocument* _document = nullptr;
        Rml::String _contextName;
        Rml::DataModelHandle _varsModel;
        RmlUiVarsDefinition _vars;
        uint64_t _varsVersion = 0;
        ActionListener _listener;
        bool _holdsRuntime = false;

        float _originX = 0.0f;
        float _originY = 0.0f;
        float _scale = 1.0f;
        int _width = 0;
        int _height = 0;

        int _inputOrder = 0;
        bool _registered = false;
        uint32_t _consumedButtons = 0;
    };

}  // namespace BECore
