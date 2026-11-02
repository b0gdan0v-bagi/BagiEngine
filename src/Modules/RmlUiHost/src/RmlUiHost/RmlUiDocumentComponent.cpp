#include "RmlUiDocumentComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/InputEvents.h>
#include <Generated/RmlUiDocumentComponent.gen.hpp>
#include <RmlUi/Core.h>
#include <RmlUiHost/RmlUiRuntime.h>

namespace BECore {

    namespace {

        constexpr int kInitialContextWidth = 1280;
        constexpr int kInitialContextHeight = 720;
        constexpr const char* kVarsModel = "vars";
        constexpr const char* kActionAttribute = "data-action";
        constexpr const char* kDisabledAttribute = "disabled";

        constexpr uint32_t ButtonBit(uint8_t button) {
            return button < 32 ? 1u << button : 0u;
        }

        /// RmlUi numbers the buttons from 0 (left, right, middle), SDL from 1 (left, middle, right); -1 for a button RmlUi has no index for
        int RmlButtonIndex(uint8_t sdlButton) {
            switch (sdlButton) {
                case SDL_BUTTON_LEFT:
                    return 0;
                case SDL_BUTTON_RIGHT:
                    return 1;
                case SDL_BUTTON_MIDDLE:
                    return 2;
                default:
                    return -1;
            }
        }

        int RoundToPixels(float value) {
            return std::max(1, static_cast<int>(std::lround(value)));
        }

    }  // namespace

    void RmlUiDocumentComponent::ActionListener::ProcessEvent(Rml::Event& event) {
        for (Rml::Element* element = event.GetTargetElement(); element != nullptr; element = element->GetParentNode()) {
            if (element->HasAttribute(kDisabledAttribute)) {
                return;
            }
            if (const Rml::Variant* action = element->GetAttribute(kActionAttribute)) {
                const Rml::String name = action->Get<Rml::String>();
                if (!name.empty()) {
                    pending.push_back(PoolString::Intern(eastl::string_view(name.data(), name.size())));
                }
                return;
            }
        }
    }

    RmlUiDocumentComponent::~RmlUiDocumentComponent() {
        if (_registered) {
            CoreManager::GetUIInput().Unregister(*this);
        }
        DestroyContext();
    }

    bool RmlUiDocumentComponent::CreateContext() {
        RmlUiRuntime& runtime = RmlUiRuntime::GetInstance();
        if (!runtime.Acquire()) {
            return false;
        }
        _holdsRuntime = true;
        _contextName = runtime.NextContextName();
        _context = Rml::CreateContext(_contextName, Rml::Vector2i(kInitialContextWidth, kInitialContextHeight));
        if (!_context) {
            LOG_ERROR("RmlUiDocumentComponent: cannot create the context '{}'"_format(eastl::string_view(_contextName.data(), _contextName.size())));
            DestroyContext();
            return false;
        }
        if (Rml::DataModelConstructor constructor = _context->CreateDataModel(kVarsModel)) {
            constructor.BindCustomDataVariable(kVarsModel, _vars.GetVariable());
            _varsModel = constructor.GetModelHandle();
        }
        _varsVersion = CoreManager::GetUIVars().GetVersion();
        return true;
    }

    void RmlUiDocumentComponent::DestroyContext() {
        if (!_holdsRuntime) {
            return;
        }
        RmlUiRuntime& runtime = RmlUiRuntime::GetInstance();
        // After the application cleaned up, RmlUi is already down and took the contexts with it
        if (_context && runtime.IsUp()) {
            Rml::RemoveContext(_contextName);
        }
        _context = nullptr;
        _document = nullptr;
        _varsModel = {};
        _holdsRuntime = false;
        runtime.Release();
    }

    void RmlUiDocumentComponent::AttachDocument(Rml::ElementDocument* document) {
        _document = document;
        document->AddEventListener("click", &_listener);
        document->Show();
    }

    void RmlUiDocumentComponent::Initialize() {
        // Called after loading and again by AddComponent
        if (_context || !CreateContext()) {
            return;
        }
        if (_source.Empty()) {
            return;
        }
        Rml::ElementDocument* document = LoadSource();
        if (!document) {
            LOG_ERROR("RmlUiDocumentComponent: cannot load the document '{}'"_format(_source.ToStringView()));
            return;
        }
        AttachDocument(document);
    }

    Rml::ElementDocument* RmlUiDocumentComponent::LoadSource() {
        RmlUiFileInterface& files = RmlUiRuntime::GetInstance().GetFileInterface();
        // RmlUi keeps style sheets and templates by path, so a cached one would not be opened again: drop them, and the list of files below is the whole document
        Rml::Factory::ClearStyleSheetCache();
        Rml::Factory::ClearTemplateCache();
        files.BeginRecording();
        Rml::ElementDocument* document = _context->LoadDocument(Rml::String(_source.ToStringView().data(), _source.ToStringView().size()));
        _watched = files.EndRecording();
        return document;
    }

    bool RmlUiDocumentComponent::Reload() {
        if (_source.Empty() || !_context) {
            return false;
        }
        Rml::ElementDocument* document = LoadSource();
        if (!document) {
            LOG_ERROR("RmlUiDocumentComponent: cannot reload the document '{}', the old one stays"_format(_source.ToStringView()));
            return false;
        }
        if (_document) {
            _context->UnloadDocument(_document);
        }
        AttachDocument(document);
        LOG_INFO("RmlUiDocumentComponent: reloaded '{}'"_format(_source.ToStringView()));
        return true;
    }

    bool RmlUiDocumentComponent::ReloadIfChanged() {
        const bool changed = std::any_of(_watched.begin(), _watched.end(), [](const RmlUiFileStamp& stamp) { return stamp.IsStale(); });
        return changed && Reload();
    }

    void RmlUiDocumentComponent::PollFiles() {
        const float interval = CoreManager::GetUIEnvironment().GetSettings().hotReloadInterval;
        if (interval <= 0.0f || _source.Empty()) {
            return;
        }
        const double now = CoreManager::GetTime().GetTotalTime();
        // A clock that went back (Time::Reset) polls at once
        if (now >= _lastPollTime && now - _lastPollTime < interval) {
            return;
        }
        _lastPollTime = now;
        ReloadIfChanged();
    }

    bool RmlUiDocumentComponent::SetMarkup(const eastl::string& markup) {
        if (!_context && !CreateContext()) {
            return false;
        }
        // The markup replaces whatever `source` gave, so there are no files to watch
        _watched.clear();
        if (_document) {
            _context->UnloadDocument(_document);
            _document = nullptr;
        }
        Rml::ElementDocument* document = _context->LoadDocumentFromMemory(Rml::String(markup.data(), markup.size()));
        if (!document) {
            return false;
        }
        AttachDocument(document);
        return true;
    }

    void RmlUiDocumentComponent::OnAttached() {
        // The scene attaches its nodes one by one, so this rank may be provisional: RefreshInputOrder() settles it on every draw
        _inputOrder = GetScene().GetDrawRank(GetNode());
        CoreManager::GetUIInput().Register(*this, _inputOrder);
        _registered = true;
    }

    void RmlUiDocumentComponent::RefreshInputOrder() {
        const int rank = GetScene().GetDrawRank(GetNode());
        if (rank == _inputOrder) {
            return;
        }
        UIInput& input = CoreManager::GetUIInput();
        input.Unregister(*this);
        _inputOrder = rank;
        input.Register(*this, _inputOrder);
    }

    UICanvasScaleParams RmlUiDocumentComponent::MakeParams() const {
        UICanvasScaleParams params;
        params.space = _space;
        params.scaleMode = _scaleMode;
        params.referenceWidth = _referenceWidth;
        params.referenceHeight = _referenceHeight;
        params.match = _match;
        params.minScale = _minScale;
        params.maxScale = _maxScale;
        params.useSafeArea = _useSafeArea;
        return params;
    }

    UILayoutContext RmlUiDocumentComponent::MakeContext(float outputWidth, float outputHeight) const {
        UILayoutContext context = CoreManager::GetUIEnvironment().MakeContext(CoreManager::GetMainWindow().Get(), outputWidth, outputHeight);
        context.sceneViewWidth = GetScene().GetViewWidth();
        context.sceneViewHeight = GetScene().GetViewHeight();
        if (_space == CanvasSpace::World) {
            if (auto transform = GetNode().GetComponent<TransformComponent>()) {
                context.worldBox = {transform->_x, transform->_y, transform->_width, transform->_height};
            }
        }
        return context;
    }

    void RmlUiDocumentComponent::SyncVars() {
        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        if (version == _varsVersion) {
            return;
        }
        _varsVersion = version;
        if (_varsModel) {
            _varsModel.DirtyVariable(kVarsModel);
        }
    }

    void RmlUiDocumentComponent::UpdateLayout(float outputWidth, float outputHeight) {
        if (!_context || outputWidth <= 0.0f || outputHeight <= 0.0f) {
            return;
        }
        const UICanvasResolved resolved = UICanvasScaler::Resolve(MakeParams(), MakeContext(outputWidth, outputHeight));
        const Rect content = resolved.transform.ToOutput(resolved.contentBounds.x, resolved.contentBounds.y, resolved.contentBounds.w, resolved.contentBounds.h);
        _originX = content.x;
        _originY = content.y;
        _scale = resolved.transform.scale;

        const int width = RoundToPixels(content.w);
        const int height = RoundToPixels(content.h);
        if (width != _width || height != _height) {
            _width = width;
            _height = height;
            _context->SetDimensions(Rml::Vector2i(width, height));
        }
        if (_context->GetDensityIndependentPixelRatio() != _scale) {
            _context->SetDensityIndependentPixelRatio(_scale);
        }
        SyncVars();
        _context->Update();
    }

    void RmlUiDocumentComponent::OnDraw() {
        // Also while hidden, so a document that is shown later (a modal dialog) already has its place in the input order
        RefreshInputOrder();
        PollFiles();

        const auto& renderer = CoreManager::GetRenderer();
        if (!_visible) {
            if (_context) {
                _context->ProcessMouseLeave();  // a hidden screen keeps no hover
            }
            _consumedButtons = 0;
        }
        if (!renderer || !_visible || !_context || !_document) {
            return;
        }
        UpdateLayout(renderer->GetOutputWidth(), renderer->GetOutputHeight());

        // The context draws in its own pixels: a transform of scale 1 that moves its corner to where the scaler put it
        const ViewTransform sceneTransform = renderer->GetViewTransform();
        renderer->SetViewTransform(ViewTransform{1.0f, _originX, _originY});
        _context->Render();
        RmlUiRuntime::GetInstance().GetRenderInterface().EndPass();
        renderer->SetViewTransform(sceneTransform);
    }

    bool RmlUiDocumentComponent::TakesInput() const {
        return _visible && _inputEnabled && _context != nullptr && _document != nullptr && IsSceneActive();
    }

    bool RmlUiDocumentComponent::MovePointer(float outputX, float outputY) {
        const int x = static_cast<int>(std::lround(outputX - _originX));
        const int y = static_cast<int>(std::lround(outputY - _originY));
        // RmlUi answers true when the event went on (nothing under the pointer took it)
        return !_context->ProcessMouseMove(x, y, 0);
    }

    void RmlUiDocumentComponent::FlushActions() {
        if (_listener.pending.empty()) {
            return;
        }
        auto actions = std::move(_listener.pending);
        _listener.pending.clear();
        for (const PoolString action : actions) {
            InputEvents::ActionEvent::Emit(action);
        }
    }

    bool RmlUiDocumentComponent::OnMouseButtonDown(const SDL_MouseButtonEvent& event) {
        if (!TakesInput()) {
            return false;
        }
        bool over = MovePointer(event.x, event.y);
        const int index = RmlButtonIndex(event.button);
        if (index >= 0) {
            over = !_context->ProcessMouseButtonDown(index, 0);
        }
        const bool consumed = _modal || over;
        if (consumed) {
            _consumedButtons |= ButtonBit(event.button);
        }
        return consumed;
    }

    bool RmlUiDocumentComponent::OnMouseButtonUp(const SDL_MouseButtonEvent& event) {
        if (!TakesInput()) {
            _consumedButtons = 0;
            return false;
        }
        const bool over = MovePointer(event.x, event.y);
        const int index = RmlButtonIndex(event.button);
        if (index >= 0) {
            _context->ProcessMouseButtonUp(index, 0);
        }
        // A press that this document took is followed to its release wherever the pointer is
        const uint32_t bit = ButtonBit(event.button);
        const bool followed = (_consumedButtons & bit) != 0;
        _consumedButtons &= ~bit;
        const bool consumed = followed || _modal || over;
        // The actions run last: a handler may switch scenes and destroy this component
        FlushActions();
        return consumed;
    }

    bool RmlUiDocumentComponent::OnMouseMotion(const SDL_MouseMotionEvent& event) {
        if (!TakesInput()) {
            return false;
        }
        // A button is held and this document did not take its press: a drag of the game, which passing over the UI does not interrupt or light up
        const bool gameDrag = event.state != 0 && _consumedButtons == 0;
        bool over = false;
        if (gameDrag) {
            _context->ProcessMouseLeave();
        } else {
            over = MovePointer(event.x, event.y);
        }
        if (_modal || _consumedButtons != 0) {
            return true;
        }
        return over;
    }

    bool RmlUiDocumentComponent::OnMouseWheel(const SDL_MouseWheelEvent& event) {
        if (!TakesInput()) {
            return false;
        }
        const bool over = MovePointer(event.mouse_x, event.mouse_y);
        if (over) {
            _context->ProcessMouseWheel(Rml::Vector2f(-event.x, -event.y), 0);
        }
        return _modal || over;
    }

}  // namespace BECore
