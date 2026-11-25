#include "AssetPanelWidget.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <Generated/AssetPanelWidget.gen.hpp>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        /// The banner lists this many issues and then says how many more there are
        constexpr size_t kBannerIssues = 3;
        constexpr const char* kBannerId = "banner";

        bool IsKnownCommand(StringView name) {
            return FindEditorCommand(name) != nullptr;
        }

    }  // namespace

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
            }
            _windowTitle = Format("{}###{}", _asset, _asset);
            return;
        }
        if (!_openDecided) {
            _open = asset->open;
            _openDecided = true;
        }
        if (_holder.Version() == _preparedVersion) {
            return;
        }
        _preparedVersion = _holder.Version();
        _windowTitle = Format("{}###{}", asset->title, asset->id);
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
                entry.handle = _sources.Register(source.name, [model] { return model->GetObject(); });
            }
            next.push_back(std::move(entry));
        }
        _models = std::move(next);
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
        if (asset != nullptr && !asset->command.empty() && id == StringView(asset->command)) {
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
        ApplyPendingReload();
        if (!_open) {
            return;
        }
        for (const ModelEntry& entry : _models) {
            entry.model->Update();
        }
        const PanelAsset* asset = _holder.Current().Get();
        if (asset != nullptr && (asset->width > 0.0f || asset->height > 0.0f)) {
            ImGui::SetNextWindowSize(ImVec2(asset->width, asset->height), ImGuiCond_FirstUseEver);
        }
        _result.Clear();
        if (ImGui::Begin(_windowTitle.c_str(), &_open)) {
            DrawBanner();
            if (_interpreter.IsPrepared()) {
                _interpreter.Draw(_sources, _result);
            }
        }
        ImGui::End();
        if (_result.clicked) {
            // The walk is over: a command may change the list it walked, or the asset itself
            if (const EditorCommand* command = FindEditorCommand(StringView(_result.command))) {
                EditorCommandEvent::Emit(command->id, _result.argument);
            }
        }
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
