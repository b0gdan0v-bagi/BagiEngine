#include "ArtGenWidget.h"

#include <BECore/Control/ControlService.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceManager.h>
#include <Generated/ArtGenWidget.gen.hpp>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kThumbnailSize = 160.0f;
        constexpr float kSelectedBorder = 3.0f;
        constexpr float kDefaultWidth = 520.0f;
        constexpr float kDefaultHeight = 640.0f;

        bool IsUsable(const ArtGenSession& session, StringView preset) {
            for (const auto& known : session.GetServiceStatus().presets) {
                if (StringView(known.name) == preset) {
                    return known.error.empty();
                }
            }
            return false;
        }

    }  // namespace

    ArtGenWidget::~ArtGenWidget() {
        DropThumbnails();
    }

    bool ArtGenWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorCommandEvent, &ArtGenWidget::OnCommand>(this);
        auto& session = ArtGenSession::GetInstance();
        const std::string root = CoreManager::GetFileSystem().GetRootPath().generic_string();  //-NoStd: std::filesystem gives a std::string
        session.SetGameRoot(StringView(root.data(), root.size()));
        session.RegisterControl(CoreManager::GetControlService());
        return true;
    }

    void ArtGenWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewGenerate) {
            _open = !_open;
        }
    }

    void ArtGenWidget::Draw() {}

    void ArtGenWidget::Update() {
        if (!_open) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(kDefaultWidth, kDefaultHeight), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(ICON_LC_WAND_SPARKLES " Generate###Generate", &_open)) {
            ImGui::End();
            return;
        }
        auto& session = ArtGenSession::GetInstance();
        DrawServiceLine(session);
        ImGui::Separator();
        DrawForm(session);
        ImGui::Separator();
        DrawRun(session);
        ImGui::End();
    }

    void ArtGenWidget::DrawServiceLine(const ArtGenSession& session) {
        const auto& palette = GetImGuiPalette();
        const ControlServerStatus control = CoreManager::GetControlService().GetServerStatus();
        if (!control.listening) {
            ImGui::TextColored(palette.warning, ICON_LC_PLUG " The control API is off: start the editor with --control");
            ImGui::TextWrapped("The generation service is a client of it (tools/artgen/artgen_bridge.py).");
            return;
        }
        if (!session.IsServiceConnected()) {
            ImGui::TextColored(palette.warning, ICON_LC_PLUG " The generation service is not connected");
            ImGui::TextWrapped("Run: python engine/tools/artgen/artgen_bridge.py --port %u", static_cast<unsigned>(control.port));
            return;
        }
        const auto& status = session.GetServiceStatus();
        if (status.state == "ready") {
            ImGui::TextColored(palette.success, ICON_LC_PLUG " Service ready (%s)", status.generator.c_str());
        } else {
            ImGui::TextColored(palette.warning, ICON_LC_CIRCLE_ALERT " Generator %s", status.state.c_str());
            if (!status.reason.empty()) {
                ImGui::TextWrapped("%s", status.reason.c_str());
            }
        }
    }

    void ArtGenWidget::DrawForm(ArtGenSession& session) {
        const auto& presets = session.GetServiceStatus().presets;
        if (!IsUsable(session, _presetName)) {
            for (const auto& preset : presets) {
                if (preset.error.empty()) {
                    _presetName = preset.name;
                    break;
                }
            }
        }
        const char* current = _presetName.empty() ? "(no preset)" : _presetName.c_str();
        for (const auto& preset : presets) {
            if (preset.name == _presetName) {
                current = preset.title.c_str();
            }
        }
        if (ImGui::BeginCombo("Preset", current)) {
            for (const auto& preset : presets) {
                ImGui::BeginDisabled(!preset.error.empty());
                if (ImGui::Selectable(preset.title.c_str(), preset.name == _presetName)) {
                    _presetName = preset.name;
                }
                ImGui::EndDisabled();
                if (!preset.error.empty()) {
                    ItemTooltip(preset.error.c_str());
                }
            }
            ImGui::EndCombo();
        }
        if (presets.empty()) {
            ImGui::TextDisabled("The game has no presets in artgen/presets/ (tools/artgen/CLAUDE.md, \"Presets\")");
        }

        ImGui::InputTextWithHint("Subject", "white knight, a red gem, ...", _subject, sizeof(_subject));
        {
            ScopedMonoFont mono;
            ImGui::InputInt("Count", &_count);
        }
        _count = std::clamp(_count, 1, ArtGenSession::kMaxCount);
        ImGui::Checkbox("Random seed", &_randomSeed);
        if (!_randomSeed) {
            ImGui::SameLine();
            ScopedMonoFont mono;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputScalar("##seed", ImGuiDataType_S64, &_seed);
            _seed = std::max<int64_t>(_seed, 0);
        }

        const bool busy = session.IsBusy();
        const bool canGenerate = !busy && !_presetName.empty() && _subject[0] != '\0';
        ImGui::BeginDisabled(!canGenerate);
        if (IconTextButton(ICON_LC_SPARKLES, "Generate", "Ask the service for the images (it may still be starting: the request waits)")) {
            auto result = session.Request(_presetName, StringView(_subject), _count, _randomSeed ? Optional<int64_t>() : Optional<int64_t>(_seed));
            _requestError = result ? String() : result.error();
        }
        ImGui::EndDisabled();
        if (busy) {
            ImGui::SameLine();
            ImGui::BeginDisabled(session.GetRun().cancelRequested);
            if (IconTextButton(ICON_LC_X, "Cancel", "Stop the run; the images already made stay")) {
                session.Cancel();
            }
            ImGui::EndDisabled();
        }
        if (!_requestError.empty()) {
            ImGui::TextColored(GetImGuiPalette().error, "%s", _requestError.c_str());
        }
    }

    void ArtGenWidget::DrawRun(ArtGenSession& session) {
        const ArtGenRun& run = session.GetRun();
        SyncThumbnails(run);
        const auto& palette = GetImGuiPalette();
        switch (run.state) {
            case ArtGenRunState::Idle:
                EmptyState(ICON_LC_IMAGE, "Pick a preset, write a subject and press Generate");
                return;
            case ArtGenRunState::Queued:
                ImGui::TextDisabled("Waiting for the service to take the request...");
                break;
            case ArtGenRunState::Running: {
                const float perImage = run.steps > 0 ? static_cast<float>(run.step) / static_cast<float>(run.steps) : 0.0f;
                const float fraction = run.count > 0 ? (static_cast<float>(run.variants.size()) + perImage) / static_cast<float>(run.count) : 0.0f;
                char overlay[96];
                std::snprintf(overlay, sizeof(overlay), "image %d of %d, step %d of %d", static_cast<int>(run.variants.size()) + 1, run.count, run.step, run.steps);
                ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
                if (run.cancelRequested) {
                    ImGui::TextDisabled("Cancelling...");
                }
                break;
            }
            case ArtGenRunState::Done:
                ImGui::TextColored(palette.success, "%d image(s) of \"%s\"", static_cast<int>(run.variants.size()), run.subject.c_str());
                break;
            case ArtGenRunState::Failed:
                ImGui::TextColored(palette.error, ICON_LC_CIRCLE_ALERT " Failed: %s", run.error.c_str());
                break;
            case ArtGenRunState::Cancelled:
                ImGui::TextDisabled("Cancelled; %d image(s) kept", static_cast<int>(run.variants.size()));
                break;
            default:
                break;
        }
        DrawVariants(run);
    }

    void ArtGenWidget::DrawVariants(const ArtGenRun& run) {
        if (run.variants.empty()) {
            return;
        }
        const auto& palette = GetImGuiPalette();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (kThumbnailSize + spacing)));
        ImGui::BeginChild("##variants");
        for (size_t i = 0; i < run.variants.size(); ++i) {
            if (i % static_cast<size_t>(columns) != 0) {
                ImGui::SameLine();
            }
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginGroup();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##thumb", ImVec2(kThumbnailSize, kThumbnailSize))) {
                _selected = static_cast<int32_t>(i);
            }
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 corner(origin.x + kThumbnailSize, origin.y + kThumbnailSize);
            const ITexture* texture = i < _thumbnails.size() ? _thumbnails[i].Get() : nullptr;
            if (texture != nullptr && _thumbnails[i]->GetState() == ResourceState::Loaded) {
                const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->GetNativeHandle()));
                drawList->AddImage(id, origin, corner);
            } else {
                drawList->AddRectFilled(origin, corner, ImGui::GetColorU32(palette.frameBg));
            }
            if (_selected == static_cast<int32_t>(i)) {
                drawList->AddRect(origin, corner, ImGui::GetColorU32(palette.accent), 0.0f, 0, kSelectedBorder);
            }
            {
                ScopedMonoFont mono;
                ImGui::TextDisabled("seed %lld", static_cast<long long>(run.variants[i].seed));
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void ArtGenWidget::SyncThumbnails(const ArtGenRun& run) {
        if (run.id != _thumbRunId) {
            DropThumbnails();
            _thumbRunId = run.id;
            _selected = -1;
        }
        auto& resources = CoreManager::GetResourceManager();
        while (_thumbnails.size() < run.variants.size()) {
            const String& path = run.variants[_thumbnails.size()].path;
            _thumbnails.push_back(resources.Load<ITexture>(StringView(path.data(), path.size())));
            _thumbnailPaths.push_back(path);
        }
    }

    void ArtGenWidget::DropThumbnails() {
        // The images of a run are seen once: keeping them in the cache would hold every generated texture until the editor closes
        auto& resources = CoreManager::GetResourceManager();
        for (const auto& path : _thumbnailPaths) {
            resources.Evict(StringView(path.data(), path.size()));
        }
        _thumbnails.clear();
        _thumbnailPaths.clear();
    }

}  // namespace BECore
