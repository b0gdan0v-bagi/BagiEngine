#include "ArtGenWidget.h"

#include <BECore/Control/ControlService.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/ApplicationEvents.h>
#include <Generated/ArtGenWidget.gen.hpp>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenLaunch.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>
#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kThumbnailSize = 160.0f;
        constexpr float kSelectedBorder = 3.0f;
        constexpr float kDefaultWidth = 520.0f;
        constexpr float kDefaultHeight = 700.0f;
        // Squares behind a thumbnail, so a transparent background (a cut-out sprite) shows as transparent
        constexpr float kCheckerCell = 10.0f;
        // Seconds a started bridge gets to make its first poll before the panel says it did not come
        constexpr double kBridgeStartSeconds = 15.0;
        constexpr double kBytesPerGigabyte = 1e9;
        constexpr double kBytesPerMegabyte = 1e6;

        void DrawChecker(ImDrawList* drawList, ImVec2 origin, float size, ImU32 light, ImU32 dark) {
            drawList->AddRectFilled(origin, ImVec2(origin.x + size, origin.y + size), light);
            const int cells = static_cast<int>(size / kCheckerCell);
            for (int y = 0; y < cells; ++y) {
                for (int x = (y % 2); x < cells; x += 2) {
                    const ImVec2 min(origin.x + static_cast<float>(x) * kCheckerCell, origin.y + static_cast<float>(y) * kCheckerCell);
                    drawList->AddRectFilled(min, ImVec2(min.x + kCheckerCell, min.y + kCheckerCell), dark);
                }
            }
        }

        bool IsUsable(const ArtGenSession& session, StringView preset) {
            for (const auto& known : session.GetServiceStatus().presets) {
                if (StringView(known.name) == preset) {
                    return known.error.empty();
                }
            }
            return false;
        }

        /// A sprite name from the subject: "White Knight!" -> "white_knight"; the user edits it before Accept
        void SpriteNameFromSubject(const char* subject, char* out, size_t size) {
            size_t length = 0;
            bool underscore = false;
            for (const char* c = subject; *c != '\0' && length + 1 < size; ++c) {
                const char lower = (*c >= 'A' && *c <= 'Z') ? static_cast<char>(*c - 'A' + 'a') : *c;
                if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) {
                    if (underscore && length > 0 && length + 2 < size) {
                        out[length++] = '_';
                    }
                    out[length++] = lower;
                    underscore = false;
                } else {
                    underscore = true;
                }
            }
            out[length] = '\0';
        }

    }  // namespace

    ArtGenWidget::~ArtGenWidget() {
        DropThumbnails();
    }

    bool ArtGenWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorCommandEvent, &ArtGenWidget::OnCommand>(this);
        Subscribe<SceneSelectionChangedEvent, &ArtGenWidget::OnSelectionChanged>(this);
        auto& session = ArtGenSession::GetInstance();
        const std::string root = CoreManager::GetFileSystem().GetRootPath().generic_string();  //-NoStd: std::filesystem gives a std::string
        session.SetGameRoot(StringView(root.data(), root.size()));
        session.SetAcceptor(&AcceptForControl);
        session.RegisterControl(CoreManager::GetControlService());
        return true;
    }

    void ArtGenWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewGenerate) {
            _open = !_open;
        }
    }

    void ArtGenWidget::OnSelectionChanged(const SceneSelectionChangedEvent& event) {
        // The pointers are valid for this event only: the session holds the node and checks it alive before use
        ArtGenSession::GetInstance().SetSelectedNode(event.nodes.empty() ? IntrusivePtr<SceneNode>() : IntrusivePtr<SceneNode>(event.nodes.back()));
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
        DrawSetup(session);
        ImGui::Separator();
        DrawForm(session);
        ImGui::Separator();
        DrawRun(session);
        ImGui::End();
    }

    void ArtGenWidget::StartBridge(uint16_t port) {
        _bridgeError.clear();
        const auto script = FindArtGenBridge(CoreManager::GetFileSystem().GetMountLayers("config"_intern));
        if (!script) {
            _bridgeError = Format("no {} under the content roots (a packaged build?)", kArtGenBridgeScript);
            return;
        }
        if (LaunchArtGenBridge(*script, port, EditorUserDirectory() / "artgen-bridge.log", _bridgeError)) {
            _bridgeStartedAt = ImGui::GetTime();
        }
    }

    void ArtGenWidget::DrawServiceLine(ArtGenSession& session) {
        const auto& palette = GetImGuiPalette();
        const ControlServerStatus control = CoreManager::GetControlService().GetServerStatus();
        if (!control.listening) {
            ImGui::TextColored(palette.warning, ICON_LC_PLUG " The generation service is not connected");
            if (_bridgeWaitsForControl) {
                ImGui::TextColored(palette.error, "The control API did not start (the Console says why).");
            }
            // The service talks to the editor over the control API, so the button turns it on first and starts the service next frame
            if (IconTextButton(ICON_LC_POWER, "Start the service", "Turn on the editor's control API and run the generation service for it; it stops when the editor closes")) {
                _bridgeWaitsForControl = true;
                ApplicationEvents::StartControlServerRequestEvent::Emit();
            }
            return;
        }
        if (_bridgeWaitsForControl) {
            _bridgeWaitsForControl = false;
            StartBridge(control.port);
        }
        if (!session.IsServiceConnected()) {
            const bool starting = _bridgeStartedAt >= 0.0 && ImGui::GetTime() - _bridgeStartedAt < kBridgeStartSeconds;
            if (starting) {
                ImGui::TextColored(palette.textDisabled, ICON_LC_LOADER " Starting the generation service...");
                return;
            }
            ImGui::TextColored(palette.warning, ICON_LC_PLUG " The generation service is not connected");
            if (IconTextButton(ICON_LC_POWER, "Start the service", "Run tools/artgen/artgen_bridge.py of the engine for this editor; it stops when the editor closes")) {
                StartBridge(control.port);
            }
            if (_bridgeStartedAt >= 0.0 && _bridgeError.empty()) {
                ImGui::TextWrapped("It did not connect: see artgen-bridge.log in the editor's user folder, or run it by hand: python engine/tools/artgen/artgen_bridge.py --port %u",
                                   static_cast<unsigned>(control.port));
            } else {
                ImGui::TextWrapped("Or by hand: python engine/tools/artgen/artgen_bridge.py --port %u", static_cast<unsigned>(control.port));
            }
            if (!_bridgeError.empty()) {
                ImGui::TextColored(palette.error, "%s", _bridgeError.c_str());
            }
            return;
        }
        _bridgeStartedAt = -1.0;
        const auto& status = session.GetServiceStatus();
        if (status.state == "ready") {
            ImGui::TextColored(palette.success, ICON_LC_PLUG " Service ready (%s)", status.generator.c_str());
            ImGui::SameLine();
            ImGui::BeginDisabled(session.IsSetupBusy() || session.IsBusy() || !status.startedHere);
            const char* stopTip = status.startedHere ? "Stop the generator (frees the GPU)" : "This ComfyUI was started by hand: close its window to stop it (then Start here can run it)";
            if (IconButton(ICON_LC_POWER_OFF, stopTip, "##StopGenerator")) {
                auto requested = session.RequestSetup(ArtGenSetupAction::Stop);
                _setupError = requested ? String() : requested.error();
            }
            ImGui::EndDisabled();
        } else {
            ImGui::TextColored(palette.warning, ICON_LC_CIRCLE_ALERT " Generator %s", status.state.c_str());
            if (!status.reason.empty()) {
                ImGui::TextWrapped("%s", status.reason.c_str());
            }
        }
    }

    void ArtGenWidget::DrawSetup(ArtGenSession& session) {
        if (!session.IsServiceConnected()) {
            return;
        }
        const auto& palette = GetImGuiPalette();
        const ArtGenSetup& setup = session.GetSetup();
        const String& state = session.GetServiceStatus().state;

        if (session.IsSetupBusy()) {
            const char* doing = setup.action == ArtGenSetupAction::Plan      ? "Looking at this machine"
                                : setup.action == ArtGenSetupAction::Install ? "Installing the generator"
                                : setup.action == ArtGenSetupAction::Start   ? "Starting the generator (a minute the first time)"
                                                                             : "Stopping the generator";
            ImGui::TextColored(palette.textDisabled, ICON_LC_LOADER " %s...", doing);
            if (!setup.current.empty()) {
                ImGui::TextWrapped("%s", setup.current.c_str());
            }
            if (setup.total > 0) {
                char overlay[64];
                std::snprintf(overlay, sizeof(overlay), "%.0f / %.0f MB", static_cast<double>(setup.done) / kBytesPerMegabyte, static_cast<double>(setup.total) / kBytesPerMegabyte);
                ImGui::ProgressBar(static_cast<float>(static_cast<double>(setup.done) / static_cast<double>(setup.total)), ImVec2(-1.0f, 0.0f), overlay);
            }
            if (!setup.log.empty()) {
                ImGui::TextDisabled("%s", setup.log.c_str());
            }
            if (setup.action == ArtGenSetupAction::Install) {
                ImGui::BeginDisabled(setup.cancelRequested);
                if (IconTextButton(ICON_LC_X, "Cancel", "Stop the install; what is downloaded stays and the next install resumes")) {
                    session.CancelSetup();
                }
                ImGui::EndDisabled();
            }
            return;
        }

        if (setup.state == ArtGenSetupState::Failed) {
            ImGui::TextColored(palette.error, ICON_LC_CIRCLE_ALERT " %s", setup.error.c_str());
        } else if (setup.state == ArtGenSetupState::Done && setup.action != ArtGenSetupAction::Plan && !setup.log.empty()) {
            // Start and Stop say what they did (a ComfyUI started by hand is left alone, one already running is used)
            ImGui::TextDisabled("%s", setup.log.c_str());
        }
        if (!_setupError.empty()) {
            ImGui::TextColored(palette.error, "%s", _setupError.c_str());
        }
        const bool planShown = setup.action == ArtGenSetupAction::Plan && setup.state == ArtGenSetupState::Done && state == "not_installed";
        if (planShown) {
            DrawPlan(session, setup);
            return;
        }
        if (state == "not_installed") {
            ImGui::BeginDisabled(session.IsBusy());
            if (IconTextButton(ICON_LC_DOWNLOAD, "Set up...", "See what installing a local generator (ComfyUI and its models) would download, then decide")) {
                auto requested = session.RequestSetup(ArtGenSetupAction::Plan);
                _setupError = requested ? String() : requested.error();
            }
            ImGui::EndDisabled();
            ImGui::TextDisabled(ICON_LC_CLOUD " A generator on another machine: not available yet (comfyui-remote-via-todo)");
        } else if (state == "not_running") {
            ImGui::BeginDisabled(session.IsBusy());
            if (IconTextButton(ICON_LC_PLAY, "Start", "Start the local generator in the background (the GPU is busy while it runs)")) {
                auto requested = session.RequestSetup(ArtGenSetupAction::Start);
                _setupError = requested ? String() : requested.error();
            }
            ImGui::EndDisabled();
        }
    }

    void ArtGenWidget::DrawPlan(ArtGenSession& session, const ArtGenSetup& setup) {
        const auto& palette = GetImGuiPalette();
        ImGui::TextWrapped(ICON_LC_MONITOR " This machine: %s. %s", setup.platform.c_str(), setup.platformNote.c_str());
        ImGui::TextWrapped("Into %s:", setup.root.c_str());
        if (ImGui::BeginTable("##SetupPlan", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            for (const auto& step : setup.plan) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(step.what.c_str());
                if (!step.licence.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Licence: %s\n%s", step.licence.c_str(), step.dest.c_str());
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", step.state.c_str());
                ImGui::TableNextColumn();
                if (step.size > 0) {
                    ImGui::Text("%.2f GB", static_cast<double>(step.size) / kBytesPerGigabyte);
                }
            }
            ImGui::EndTable();
        }
        const bool fromSource = std::any_of(setup.plan.begin(), setup.plan.end(), [](const ArtGenSetupStep& step) { return step.state == "install"; });
        if (fromSource) {
            ImGui::TextWrapped("Plus PyTorch and ComfyUI's requirements from PyPI (a few GB).");
        }
        char label[96];
        std::snprintf(label, sizeof(label), "Download %.1f GB and install", static_cast<double>(setup.downloadBytes) / kBytesPerGigabyte);
        if (IconTextButton(ICON_LC_DOWNLOAD, label, "Hover a row for its licence and where it goes. The download resumes if it stops")) {
            auto requested = session.RequestSetup(ArtGenSetupAction::Install);
            _setupError = requested ? String() : requested.error();
        }
        ImGui::SameLine();
        if (IconTextButton(ICON_LC_X, "Not now", "Download nothing")) {
            _setupError.clear();
            session.ClearSetup();
        }
        ImGui::TextColored(palette.textDisabled, ICON_LC_CLOUD " Or a generator on another machine: not available yet (comfyui-remote-via-todo)");
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
        ImGui::Checkbox("Keep the variants on screen", &_keepVariants);
        ItemTooltip("The new images are added to the ones shown instead of replacing them");

        const bool busy = session.IsBusy();
        const bool canGenerate = !busy && !_presetName.empty() && _subject[0] != '\0';
        ImGui::BeginDisabled(!canGenerate);
        if (IconTextButton(ICON_LC_SPARKLES, "Generate", "Ask the service for the images (it may still be starting: the request waits)")) {
            auto result = session.Request(_presetName, StringView(_subject), _count, _randomSeed ? Optional<int64_t>() : Optional<int64_t>(_seed), _keepVariants);
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
                const int32_t made = static_cast<int32_t>(run.variants.size()) - run.keptVariants;
                const float perImage = run.steps > 0 ? static_cast<float>(run.step) / static_cast<float>(run.steps) : 0.0f;
                const float fraction = run.count > 0 ? (static_cast<float>(made) + perImage) / static_cast<float>(run.count) : 0.0f;
                char overlay[96];
                std::snprintf(overlay, sizeof(overlay), "image %d of %d, step %d of %d", static_cast<int>(made) + 1, run.count, run.step, run.steps);
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
        DrawAccept(session);
        DrawVariants(run);
    }

    void ArtGenWidget::DrawAccept(ArtGenSession& session) {
        const auto& palette = GetImGuiPalette();
        if (!_selectedSeed || session.FindVariant(*_selectedSeed) == nullptr) {
            ImGui::TextDisabled("Click an image to accept it into the game");
            return;
        }
        ImGui::SeparatorText("Accept");
        {
            ScopedMonoFont mono;
            ImGui::TextDisabled("seed %lld", static_cast<long long>(*_selectedSeed));
        }
        ImGui::InputTextWithHint("Sprite", "white_knight", _spriteName, sizeof(_spriteName));
        ItemTooltip("The TextureLibrary name; the file goes to assets/artgen/<name>.png with its .artgen.json");
        const bool validName = IsValidSpriteName(StringView(_spriteName));
        const bool haveNode = session.GetSelectedNodeIfAlive() != nullptr;
        if (!haveNode) {
            _useOnSelected = false;
        }
        ImGui::BeginDisabled(!haveNode);
        ImGui::Checkbox("Use on the selected node", &_useOnSelected);
        ImGui::EndDisabled();
        ItemTooltip(haveNode ? "Put the sprite on the SpriteRendererComponent of the node selected in the Scene Editor" : "Select a node in the Scene Editor first");

        ImGui::BeginDisabled(!validName);
        if (IconTextButton(ICON_LC_CHECK, "Accept", "Copy the image into the game's assets, add or replace the sprite and reload it")) {
            auto result = session.Accept(*_selectedSeed, StringView(_spriteName), _useOnSelected);
            _acceptFailed = !result.has_value();
            if (result) {
                const auto reloaded = GetJsonInt(*result, "reloadedSprites").value_or(0);
                const auto texture = GetJsonString(*result, "texturePath").value_or(String());
                _acceptMessage = Format("Accepted as '{}' ({}); {} sprite(s) reloaded", StringView(_spriteName), texture, reloaded);
            } else {
                _acceptMessage = result.error().message;
            }
        }
        ImGui::EndDisabled();
        if (!validName && _spriteName[0] != '\0') {
            ImGui::SameLine();
            ImGui::TextColored(palette.warning, "1 to 64 of A-Z a-z 0-9 _ -");
        }
        if (!_acceptMessage.empty()) {
            ImGui::TextColored(_acceptFailed ? palette.error : palette.success, "%s", _acceptMessage.c_str());
        }
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
            const auto& variant = run.variants[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginGroup();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##thumb", ImVec2(kThumbnailSize, kThumbnailSize))) {
                if (_selectedSeed != variant.seed) {
                    _acceptMessage.clear();
                }
                _selectedSeed = variant.seed;
                if (_spriteName[0] == '\0') {
                    SpriteNameFromSubject(run.subject.c_str(), _spriteName, sizeof(_spriteName));
                }
            }
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 corner(origin.x + kThumbnailSize, origin.y + kThumbnailSize);
            const ITexture* texture = i < _thumbnails.size() ? _thumbnails[i].Get() : nullptr;
            if (texture != nullptr && texture->GetState() == ResourceState::Loaded) {
                const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->GetNativeHandle()));
                DrawChecker(drawList, origin, kThumbnailSize, ImGui::GetColorU32(palette.panelBg), ImGui::GetColorU32(palette.frameBg));
                drawList->AddImage(id, origin, corner);
            } else {
                drawList->AddRectFilled(origin, corner, ImGui::GetColorU32(palette.frameBg));
            }
            if (_selectedSeed == variant.seed) {
                drawList->AddRect(origin, corner, ImGui::GetColorU32(palette.accent), 0.0f, 0, kSelectedBorder);
            }
            if (IconButton(ICON_LC_DICES, "Use this seed for the next Generate (with another subject, a close picture)", "##seed")) {
                _randomSeed = false;
                _seed = variant.seed;
            }
            ImGui::SameLine();
            {
                ScopedMonoFont mono;
                ImGui::TextDisabled("%lld", static_cast<long long>(variant.seed));
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void ArtGenWidget::SyncThumbnails(const ArtGenRun& run) {
        // Keep the thumbnails of the paths still in the run (a run that kept the variants), drop the rest, load the new ones
        bool same = _thumbnailPaths.size() <= run.variants.size();
        for (size_t i = 0; same && i < _thumbnailPaths.size(); ++i) {
            same = _thumbnailPaths[i] == run.variants[i].path;
        }
        if (!same) {
            DropThumbnails();
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
