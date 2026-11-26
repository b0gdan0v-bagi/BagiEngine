#include "AssetPanelWidget.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <Generated/AssetPanelWidget.gen.hpp>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/PanelDrawSupport.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        /// The banner lists this many issues and then says how many more there are
        constexpr size_t kBannerIssues = 3;
        constexpr const char* kBannerId = "banner";

        /// The host of a `Dialogs` asset: a window that draws nothing and takes nothing, so only the modals opened from it show. It must stay a window (a popup is opened from one),
        /// and one pixel is enough: a modal is a window of its own, placed on the display
        constexpr ImGuiWindowFlags kDialogsHostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs |
                                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
        constexpr ImVec2 kDialogsHostSize{1.0f, 1.0f};

        bool IsKnownCommand(StringView name) {
            return FindEditorCommand(name) != nullptr;
        }

        /// The flags of the window of an asset that is no `Dialogs` host and no `Bar`; null (nothing read yet) is a docked one
        ImGuiWindowFlags PanelWindowFlags(const PanelAsset* asset) {
            return asset != nullptr && asset->window == PanelWindowKind::Floating ? ImGuiWindowFlags_NoDocking : ImGuiWindowFlags_None;
        }

    }  // namespace

    AssetPanelWidget::~AssetPanelWidget() {
        _barHandle.Reset();
        EndBarFrame();
        // The members are destroyed after this body, so the models can still be told
        _open = false;
        NotifyOpenChanged();
    }

    bool AssetPanelWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorCommandEvent, &AssetPanelWidget::OnCommand>(this);
        Subscribe<ConfigEvents::ConfigChangedEvent, &AssetPanelWidget::OnConfigChanged>(this);
        Subscribe<ConfigEvents::ConfigReloadFailedEvent, &AssetPanelWidget::OnReloadFailed>(this);
        _sources.SetParent(&PanelDataSources::GetInstance());
        LoadAsset();
        return true;
    }

    void AssetPanelWidget::SetConfigManager(const ConfigManager& configs) {
        _configs = &configs;
    }

    void AssetPanelWidget::SetAsset(StringView name) {
        _asset = String(name);
    }

    const ConfigManager& AssetPanelWidget::Configs() const {
        return _configs != nullptr ? *_configs : CoreManager::GetConfigManager();
    }

    void AssetPanelWidget::LoadAsset() {
        const FunctionRef<bool(StringView)> knownCommand(IsKnownCommand);
        const FunctionRef<bool(StringView)> knownModel(IsKnownPanelModel);
        PanelValidationContext context;
        context.isKnownCommand = knownCommand;
        context.isKnownModel = knownModel;
        _holder.Load(Configs(), _asset, context);
        OnAssetChanged();
    }

    void AssetPanelWidget::OnAssetChanged() {
        const PanelAsset* asset = _holder.Current().Get();
        if (asset == nullptr) {
            // A first load that failed: the banner is the only content, and it is shown at once, since nothing else tells that the file is wrong
            if (!_openDecided) {
                _open = true;
                _openDecided = true;
                _fade.Snap(_open);
            }
            _windowTitle = Format("{}###{}", _asset, _asset);
            _hasTitleBinding = false;
            return;
        }
        if (!_openDecided) {
            _open = asset->open;
            _openDecided = true;
            // Open from the start is no opening, also after a mode switch
            _fade.Snap(_open);
        }
        if (_holder.Version() == _preparedVersion) {
            return;
        }
        _preparedVersion = _holder.Version();
        _windowTitle = Format("{}###{}", asset->title, asset->id);
        _titleText = asset->title;
        _hasTitleBinding = false;
        if (!asset->titleBind.empty()) {
            // The loader refused a bad `titleBind`, so a parse failure here leaves the plain title
            if (std::expected<PanelBinding, String> binding = ParseBinding(asset->titleBind)) {
                _titleBinding = std::move(*binding);
                _hasTitleBinding = true;
            }
        }
        _interpreter.Prepare(_holder.Current());
        SyncModels();
    }

    void AssetPanelWidget::SyncModels() {
        Vector<ModelEntry> next;
        for (const PanelSource& source : _holder.Current()->sources) {
            if (source.model.empty()) {
                continue;
            }
            ModelEntry entry;
            for (ModelEntry& old : _models) {
                if (old.model != nullptr && old.name == source.name && old.modelName == source.model) {
                    entry = std::move(old);
                    break;
                }
            }
            if (entry.model == nullptr) {
                entry.name = source.name;
                entry.modelName = source.model;
                entry.model = CreatePanelModel(source.model);
                if (entry.model == nullptr) {
                    continue;
                }
                IPanelModel* model = entry.model.Get();
                model->Initialize();
                entry.handle = _sources.Register(source.name, [model] { return model->GetObject(); });
                if (_notifiedOpen) {
                    model->OnOpenChanged(true);
                }
            }
            next.push_back(std::move(entry));
        }
        // What is left in the old list was dropped by this asset
        if (_notifiedOpen) {
            for (const ModelEntry& dropped : _models) {
                if (dropped.model != nullptr) {
                    dropped.model->OnOpenChanged(false);
                }
            }
        }
        _models = std::move(next);
    }

    void AssetPanelWidget::NotifyOpenChanged() {
        if (_open == _notifiedOpen) {
            return;
        }
        _notifiedOpen = _open;
        for (const ModelEntry& entry : _models) {
            entry.model->OnOpenChanged(_open);
        }
    }

    void AssetPanelWidget::OnConfigChanged(const ConfigEvents::ConfigChangedEvent& event) {
        if (event.name.ToStringView() == StringView(_asset)) {
            _reloadPending = true;
            // A change after a failure is the newer news: the load decides what the banner says
            _failurePending = false;
        }
    }

    void AssetPanelWidget::ApplyPendingReload() {
        if (_reloadPending) {
            _reloadPending = false;
            LoadAsset();
            // A failure told after the change and before this load: the load would have cleared it
            if (_failurePending) {
                _failurePending = false;
                _holder.ApplyReloadFailure(_pendingFailure);
            }
        }
    }

    void AssetPanelWidget::OnReloadFailed(const ConfigEvents::ConfigReloadFailedEvent& event) {
        if (event.name.ToStringView() == StringView(_asset)) {
            if (_reloadPending) {
                _pendingFailure = event.reason;
                _failurePending = true;
            } else {
                _holder.ApplyReloadFailure(event.reason);
            }
        }
    }

    void AssetPanelWidget::OnCommand(const EditorCommandEvent& event) {
        const PanelAsset* asset = _holder.Current().Get();
        const StringView id = event.commandId.ToStringView();
        // A `Dialogs` host and a `Bar` are always open: their command, when they have one, reaches the models like any other
        const bool alwaysOpen = asset != nullptr && (asset->window == PanelWindowKind::Dialogs || asset->window == PanelWindowKind::Bar);
        if (asset != nullptr && !alwaysOpen && !asset->command.empty() && id == StringView(asset->command)) {
            _open = !_open;
            return;
        }
        if (!_open) {
            return;
        }
        RunModelCommand(id, event.argument);
        ApplyPendingReload();
    }

    bool AssetPanelWidget::RunModelCommand(StringView id, StringView argument) const {
        for (const ModelEntry& entry : _models) {
            if (entry.model->RunCommand(id, argument)) {
                return true;
            }
        }
        return false;
    }

    void AssetPanelWidget::Draw() {}

    void AssetPanelWidget::Update() {
        // A bar nobody drew last frame still owes its models the end of that frame
        EndBarFrame();
        ApplyPendingReload();
        const PanelAsset* asset = _holder.Current().Get();
        const bool dialogs = asset != nullptr && asset->window == PanelWindowKind::Dialogs;
        const bool bar = asset != nullptr && asset->window == PanelWindowKind::Bar;
        SyncBarRegistration(bar);
        if (dialogs || bar) {
            _open = true;
        }
        NotifyOpenChanged();
        if (!_open) {
            DrawFadingOut();
            return;
        }
        for (const ModelEntry& entry : _models) {
            entry.model->Update();
        }
        if (bar) {
            // The caller's window draws it, between this `Update` and the end of the frame
            _barFrameOpen = true;
            return;
        }
        SyncTitle();
        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        if (dialogs) {
            flags = kDialogsHostFlags;
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(kDialogsHostSize);
        } else {
            flags = PanelWindowFlags(asset);
            if (asset != nullptr && (asset->width > 0.0f || asset->height > 0.0f)) {
                ImGui::SetNextWindowSize(ImVec2(asset->width, asset->height), ImGuiCond_FirstUseEver);
            }
            _fade.Advance(true, ImGui::GetIO().DeltaTime, kPanelFadeSeconds);
        }
        _result.Clear();
        {
            // A dialog is not faded: its host draws nothing, and the alpha would reach its modals
            const ScopedAlpha alpha(dialogs ? 1.0f : _fade.Alpha());
            // No close button on a host that is always open
            if (ImGui::Begin(_windowTitle.c_str(), dialogs ? nullptr : &_open, flags)) {
                DrawBanner();
                if (_interpreter.IsPrepared()) {
                    _interpreter.SetChordText(PanelChordTextRegistry::GetInstance().Get());
                    _interpreter.Draw(_sources, _result);
                }
                const ImVec2 content = ImGui::GetCurrentWindow()->ContentSize;
                _contentWidth = content.x;
                _contentHeight = content.y;
            }
            ImGui::End();
        }
        // The close button wrote `_open` inside `Begin`
        NotifyOpenChanged();
        for (const ModelEntry& entry : _models) {
            entry.model->EndUpdate();
        }
        EmitClick();
    }

    void AssetPanelWidget::DrawFadingOut() {
        if (_fade.IsHidden()) {
            return;
        }
        _fade.Advance(false, ImGui::GetIO().DeltaTime, kPanelFadeSeconds);
        if (_fade.IsHidden()) {
            return;
        }
        const ScopedAlpha alpha(_fade.Alpha());
        // Only keeps the close button drawn: the window takes no input, so nothing clears it
        bool shown = true;
        if (ImGui::Begin(_windowTitle.c_str(), &shown, PanelWindowFlags(_holder.Current().Get()) | ImGuiWindowFlags_NoInputs)) {
            // The models are not updated any more, so nothing of theirs is read. The room stays, or the scroll of the window would be lost
            ImGui::Dummy(ImVec2(_contentWidth, _contentHeight));
        }
        ImGui::End();
    }

    void AssetPanelWidget::EmitClick() {
        if (_result.clicked) {
            // The walk is over: a command may change the list it walked, or the asset itself
            if (const EditorCommand* command = FindEditorCommand(StringView(_result.command))) {
                EditorCommandEvent::Emit(command->id, _result.argument);
            }
        }
    }

    void AssetPanelWidget::SyncBarRegistration(bool bar) {
        if (!bar) {
            _barHandle.Reset();
        } else if (!_barHandle) {
            _barHandle = PanelBarsRegistry::GetInstance().Register(_asset, *this);
        }
    }

    void AssetPanelWidget::EndBarFrame() {
        if (_barFrameOpen) {
            _barFrameOpen = false;
            for (const ModelEntry& entry : _models) {
                entry.model->EndUpdate();
            }
        }
    }

    void AssetPanelWidget::DrawBar() {
        if (!_interpreter.IsPrepared()) {
            return;
        }
        DrawBarBanner();
        _result.Clear();
        _interpreter.SetChordText(PanelChordTextRegistry::GetInstance().Get());
        _interpreter.Draw(_sources, _result);
        EndBarFrame();
        EmitClick();
    }

    void AssetPanelWidget::SyncTitle() {
        if (!_hasTitleBinding) {
            return;
        }
        const PanelAsset* asset = _holder.Current().Get();
        if (asset == nullptr) {
            return;
        }
        StringView text;
        if (_titleReader.ReadText(_sources, _titleBinding, _titleScratch, text) != PanelReadStatus::Ok) {
            text = StringView(asset->title);
        }
        if (text != StringView(_titleText)) {
            _titleText = String(text);
            _windowTitle = Format("{}###{}", _titleText, asset->id);
        }
    }

    void AssetPanelWidget::DrawBarBanner() const {
        const Vector<PanelIssue>& issues = _holder.Issues();
        const String& failure = _holder.Failure();
        if (issues.empty() && failure.empty()) {
            return;
        }
        // A bar has no room for a list: the first problem, on the line the bar would start at
        ImGui::PushID(kBannerId);
        if (!failure.empty()) {
            ImGui::TextColored(GetImGuiPalette().error, "Could not reload: %s", failure.c_str());
        } else {
            ImGui::TextColored(GetImGuiPalette().error, "%s: %s", issues[0].where.c_str(), issues[0].message.c_str());
        }
        ImGui::PopID();
    }

    void AssetPanelWidget::DrawBanner() const {
        const Vector<PanelIssue>& issues = _holder.Issues();
        const String& failure = _holder.Failure();
        if (issues.empty() && failure.empty()) {
            return;
        }
        ImGui::PushID(kBannerId);
        ImGui::PushStyleColor(ImGuiCol_Text, GetImGuiPalette().error);
        if (!failure.empty()) {
            ImGui::TextWrapped("Could not reload: %s", failure.c_str());
        }
        const size_t shown = std::min(issues.size(), kBannerIssues);
        for (size_t i = 0; i < shown; ++i) {
            ImGui::TextWrapped("%s: %s", issues[i].where.c_str(), issues[i].message.c_str());
        }
        if (issues.size() > shown) {
            ImGui::Text("and %zu more", issues.size() - shown);
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Separator();
    }

}  // namespace BECore
