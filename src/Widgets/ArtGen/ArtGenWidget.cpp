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
        constexpr float kPromptLines = 4.5f;
        constexpr float kNegativeLines = 2.5f;
        constexpr int32_t kMaxImageSide = 8192;
        constexpr size_t kInputTextSize = 512;

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

        /// A JSON value as the form shows it: a string without quotes, anything else as JSON
        void FormatJsonValue(const Json& value, char* out, size_t size) {
            if (value.is_string()) {
                const auto& text = value.get_ref<const std::string&>();  //-NoStd: nlohmann holds a std::string
                std::snprintf(out, size, "%.*s", static_cast<int>(text.size()), text.data());
            } else if (value.is_number_integer()) {
                std::snprintf(out, size, "%lld", static_cast<long long>(value.get<int64_t>()));
            } else if (value.is_number_float()) {
                std::snprintf(out, size, "%g", value.get<double>());
            } else if (value.is_boolean()) {
                std::snprintf(out, size, "%s", value.get<bool>() ? "true" : "false");
            } else {
                out[0] = '\0';
            }
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
        } else {
            ImGui::TextColored(palette.warning, ICON_LC_CIRCLE_ALERT " Generator %s", status.state.c_str());
        }
        ImGui::SameLine();
        if (IconTextButton(ICON_LC_UNPLUG, "Disconnect", "Stop the generation service (a run or an install in work is cancelled); Start the service runs it again")) {
            session.RequestServiceStop();
        }
        if (status.state != "ready") {
            if (!status.reason.empty()) {
                ImGui::TextWrapped("%s", status.reason.c_str());
            }
            return;
        }
        if (status.startedHere) {
            ImGui::BeginDisabled(session.IsSetupBusy() || session.IsBusy());
            if (IconTextButton(ICON_LC_SQUARE, "Stop ComfyUI", "Stop the generator started from here; frees the GPU")) {
                auto requested = session.RequestSetup(ArtGenSetupAction::Stop);
                _setupError = requested ? String() : requested.error();
            }
            ImGui::EndDisabled();
        } else {
            ImGui::TextDisabled("ComfyUI was started by hand: close its window to stop it");
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
        DrawPresetBar(session);
        if (!_form.IsEmpty()) {
            DrawPresetFields(session);
        }
        ImGui::Spacing();

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
        const bool canGenerate = !busy && !_form.IsEmpty() && _subject[0] != '\0';
        ImGui::BeginDisabled(!canGenerate);
        if (IconTextButton(ICON_LC_SPARKLES, "Generate", "Ask the service for the images with the form as it is, saved or not (it may still be starting: the request waits)")) {
            const Optional<int64_t> seed = _randomSeed ? Optional<int64_t>() : Optional<int64_t>(_seed);
            // A saved preset goes by name, so its provenance names the file; anything else goes inline as a draft
            auto result = _form.IsDirty() ? session.RequestDraft(StringView(_form.name), _form.ToJson(), StringView(_subject), _count, seed, _keepVariants)
                                          : session.Request(StringView(_form.GetBaseName()), StringView(_subject), _count, seed, _keepVariants);
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

    void ArtGenWidget::DrawPresetBar(ArtGenSession& session) {
        const auto& palette = GetImGuiPalette();
        const auto& presets = session.GetServiceStatus().presets;
        const auto& workflows = session.GetServiceStatus().workflows;
        if (_form.IsEmpty()) {
            // The first preset that loads, else a new one on the first workflow, once the service has said what there is
            for (const auto& preset : presets) {
                if (preset.error.empty() && preset.data.is_object()) {
                    _form.Load(preset);
                    break;
                }
            }
            if (_form.IsEmpty() && !workflows.empty()) {
                _form.New(StringView(workflows.front().name));
            }
        }
        const bool dirty = !_form.IsEmpty() && _form.IsDirty();
        char preview[96];
        std::snprintf(preview, sizeof(preview), "%s%s", _form.IsEmpty() ? "(no preset)" : _form.name, dirty ? " *" : "");
        if (ImGui::BeginCombo("Preset", preview)) {
            for (const auto& preset : presets) {
                char label[192];
                std::snprintf(label, sizeof(label), "%s%s%s  (%s%s)", preset.error.empty() ? "" : ICON_LC_CIRCLE_ALERT " ", preset.kind == "audio" ? ICON_LC_AUDIO_LINES " " : "", preset.title.c_str(),
                              preset.name.c_str(), preset.source == "engine" ? ", engine" : "");
                ImGui::BeginDisabled(!preset.data.is_object());
                if (ImGui::Selectable(label, preset.name == _form.GetBaseName())) {
                    // Picking another preset drops what the form had unsaved
                    _form.Load(preset);
                    _formMessage.clear();
                }
                ImGui::EndDisabled();
                if (!preset.error.empty()) {
                    ItemTooltip(preset.error.c_str());
                }
            }
            ImGui::EndCombo();
        }
        ItemTooltip("The game's presets (artgen/presets/), then the engine's starters; * marks unsaved changes, which picking another one drops");

        ImGui::BeginDisabled(workflows.empty());
        if (IconButton(ICON_LC_FILE_PLUS, "New preset on the first workflow", "##NewPreset")) {
            _form.New(StringView(workflows.front().name));
            _formMessage.clear();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(_form.IsEmpty());
        if (IconButton(ICON_LC_COPY, "Duplicate: the same settings under a new name, not saved yet", "##DuplicatePreset")) {
            _form.Duplicate();
            _formMessage.clear();
        }
        ImGui::SameLine();
        const bool savedHere = !dirty && _form.GetBaseSource() == "game";
        ImGui::BeginDisabled(savedHere);
        if (IconButton(ICON_LC_SAVE, "Save as artgen/presets/<name>.json of the game (an engine starter becomes the game's own copy)", "##SavePreset")) {
            SaveForm(session, StringView(_form.name));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (IconButton(ICON_LC_SAVE_ALL, "Save as another name", "##SavePresetAs")) {
            std::snprintf(_saveAsName, sizeof(_saveAsName), "%s", _form.name);
            ImGui::OpenPopup("##SavePresetAsPopup");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!_form.HasBase() || !dirty);
        if (IconButton(ICON_LC_UNDO_2, "Revert to the saved preset", "##RevertPreset")) {
            _form.Revert();
            _formMessage.clear();
        }
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("##SavePresetAsPopup")) {
            ImGui::InputTextWithHint("##SaveAsName", "preset_name", _saveAsName, sizeof(_saveAsName));
            const bool valid = IsValidSpriteName(StringView(_saveAsName));
            ImGui::BeginDisabled(!valid);
            if (IconTextButton(ICON_LC_SAVE, "Save", "Write artgen/presets/<name>.json of the game")) {
                SaveForm(session, StringView(_saveAsName));
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (!valid) {
                ImGui::TextColored(palette.warning, "1 to 64 of A-Z a-z 0-9 _ -");
            }
            ImGui::EndPopup();
        }

        if (dirty) {
            ImGui::SameLine();
            ImGui::TextColored(palette.warning, _form.HasBase() ? "unsaved changes" : "not saved yet");
        } else if (_form.GetBaseSource() == "engine") {
            ImGui::SameLine();
            ImGui::TextDisabled("an engine starter: Save makes it the game's own");
        }
        if (!_formMessage.empty()) {
            ImGui::TextColored(_formMessageIsError ? palette.error : palette.success, "%s", _formMessage.c_str());
        }
        // The service's verdict on the saved file; a draft's is the failure of its run
        if (const ArtGenPreset* base = session.FindPreset(StringView(_form.GetBaseName())); base != nullptr && !base->error.empty() && !dirty) {
            ImGui::TextColored(palette.error, ICON_LC_CIRCLE_ALERT " %s", base->error.c_str());
        }
        if (presets.empty() && workflows.empty()) {
            ImGui::TextDisabled("The service has not said which presets and workflows there are yet");
        }
    }

    void ArtGenWidget::SaveForm(ArtGenSession& session, StringView name) {
        auto saved = session.SavePreset(name, _form.ToJson());
        _formMessageIsError = !saved.has_value();
        if (saved) {
            std::snprintf(_form.name, sizeof(_form.name), "%.*s", static_cast<int>(name.size()), name.data());
            _form.MarkSaved();
            _formMessage = Format("Saved {}", *saved);
        } else {
            _formMessage = saved.error();
        }
    }

    void ArtGenWidget::DrawPresetFields(ArtGenSession& session) {
        const auto& palette = GetImGuiPalette();
        if (ImGui::InputText("Name", _form.name, sizeof(_form.name))) {
            _form.Touch();
        }
        ItemTooltip("The file name: artgen/presets/<name>.json of the game; 1 to 64 of A-Z a-z 0-9 _ -");
        if (ImGui::InputText("Title", _form.title, sizeof(_form.title))) {
            _form.Touch();
        }
        const auto& workflows = session.GetServiceStatus().workflows;
        if (ImGui::BeginCombo("Workflow", _form.GetWorkflow().c_str())) {
            for (const auto& workflow : workflows) {
                char label[160];
                std::snprintf(label, sizeof(label), "%s%s  (%s, %s)", workflow.error.empty() ? "" : ICON_LC_CIRCLE_ALERT " ", workflow.name.c_str(), workflow.kind.c_str(), workflow.source.c_str());
                ImGui::BeginDisabled(!workflow.error.empty());
                if (ImGui::Selectable(label, workflow.name == _form.GetWorkflow())) {
                    _form.SetWorkflow(StringView(workflow.name), &workflow);
                }
                ImGui::EndDisabled();
                if (!workflow.error.empty()) {
                    ItemTooltip(workflow.error.c_str());
                }
            }
            ImGui::EndCombo();
        }
        ItemTooltip("The graph: the game's artgen/workflows/ first, then the engine's tools/artgen/workflows/");
        const float line = ImGui::GetTextLineHeight();
        if (ImGui::InputTextMultiline("Prompt", _form.prompt, sizeof(_form.prompt), ImVec2(0.0f, line * kPromptLines))) {
            _form.Touch();
        }
        ItemTooltip("{subject} is replaced by the subject below; without it the subject goes first");
        if (ImGui::InputTextMultiline("Negative", _form.negative, sizeof(_form.negative), ImVec2(0.0f, line * kNegativeLines))) {
            _form.Touch();
        }

        const ArtGenWorkflow* workflow = session.FindWorkflow(StringView(_form.GetWorkflow()));
        if (workflow == nullptr) {
            ImGui::TextColored(palette.warning, "The service does not know the workflow '%s'", _form.GetWorkflow().c_str());
            return;
        }
        if (ImGui::TreeNodeEx("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (workflow->HasSize()) {
                DrawSizeFields(*workflow);
            }
            for (const auto& input : workflow->inputs) {
                if (!IsArtGenServiceInput(StringView(input.name))) {
                    DrawInputField(input);
                }
            }
            ImGui::TreePop();
        }
    }

    void ArtGenWidget::DrawSizeFields(const ArtGenWorkflow& workflow) {
        const auto defaultOf = [&](StringView name) {
            const ArtGenWorkflowInput* input = workflow.FindInput(name);
            return input != nullptr && input->defaultValue.is_number_integer() ? static_cast<int32_t>(input->defaultValue.get<int64_t>()) : 0;
        };
        int32_t size[2] = {_form.width > 0 ? _form.width : defaultOf("width"), _form.height > 0 ? _form.height : defaultOf("height")};
        bool changed = false;
        {
            ScopedMonoFont mono;
            changed = ImGui::InputInt2("Size", size);
        }
        if (changed) {
            _form.width = std::clamp(size[0], 1, kMaxImageSide);
            _form.height = std::clamp(size[1], 1, kMaxImageSide);
            _form.Touch();
        }
        DrawResetButton(_form.width > 0, "##ResetSize", [this] {
            _form.width = _form.height = 0;
            _form.Touch();
        });
    }

    void ArtGenWidget::DrawInputField(const ArtGenWorkflowInput& input) {
        const Json* override = _form.FindParam(StringView(input.name));
        const Json& value = override != nullptr ? *override : input.defaultValue;
        const char* label = input.name.c_str();
        ImGui::PushID(label);
        if (input.choices.is_array() && !input.choices.empty()) {
            char current[160];
            FormatJsonValue(value, current, sizeof(current));
            if (ImGui::BeginCombo(label, current)) {
                for (const Json& choice : input.choices) {
                    char text[160];
                    FormatJsonValue(choice, text, sizeof(text));
                    if (ImGui::Selectable(text, choice == value)) {
                        _form.SetParam(StringView(input.name), choice);
                    }
                }
                ImGui::EndCombo();
            }
        } else if (value.is_boolean()) {
            bool flag = value.get<bool>();
            if (ImGui::Checkbox(label, &flag)) {
                _form.SetParam(StringView(input.name), Json(flag));
            }
        } else if (value.is_number_integer()) {
            int64_t number = value.get<int64_t>();
            ScopedMonoFont mono;
            if (ImGui::InputScalar(label, ImGuiDataType_S64, &number)) {
                _form.SetParam(StringView(input.name), Json(number));
            }
        } else if (value.is_number_float()) {
            double number = value.get<double>();
            ScopedMonoFont mono;
            if (ImGui::InputDouble(label, &number, 0.0, 0.0, "%.3f")) {
                _form.SetParam(StringView(input.name), Json(number));
            }
        } else {
            char text[kInputTextSize];
            FormatJsonValue(value, text, sizeof(text));
            if (ImGui::InputText(label, text, sizeof(text))) {
                _form.SetParam(StringView(input.name), MakeJsonString(StringView(text)));
            }
        }
        ItemTooltip(override != nullptr ? "Set by this preset" : "The workflow's own value");
        DrawResetButton(override != nullptr, "##Reset", [&] { _form.ClearParam(StringView(input.name)); });
        ImGui::PopID();
    }

    void ArtGenWidget::DrawResetButton(bool overridden, const char* id, FunctionRef<void()> reset) {
        if (!overridden) {
            return;
        }
        ImGui::SameLine();
        if (IconButton(ICON_LC_ROTATE_CCW, "Back to the workflow's value", id)) {
            reset();
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
        if (session.FindVariant(*_selectedSeed)->IsAudio()) {
            ImGui::TextDisabled(ICON_LC_AUDIO_LINES " %s", session.FindVariant(*_selectedSeed)->path.c_str());
            ImGui::TextDisabled("Listening to a sound and accepting it into a clip are not there yet");
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
            if (variant.IsAudio()) {
                drawList->AddRectFilled(origin, corner, ImGui::GetColorU32(palette.frameBg));
                const ImVec2 iconSize = ImGui::CalcTextSize(ICON_LC_AUDIO_LINES);
                drawList->AddText(ImVec2(origin.x + (kThumbnailSize - iconSize.x) * 0.5f, origin.y + (kThumbnailSize - iconSize.y) * 0.5f), ImGui::GetColorU32(palette.accent), ICON_LC_AUDIO_LINES);
            } else if (texture != nullptr && texture->GetState() == ResourceState::Loaded) {
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
            const ArtGenVariant& variant = run.variants[_thumbnails.size()];
            // A sound has no picture: its slot stays empty and the grid draws a sound tile (playing it is the sound panel's)
            _thumbnails.push_back(variant.IsAudio() ? ResourceHandle<ITexture>() : resources.Load<ITexture>(StringView(variant.path.data(), variant.path.size())));
            _thumbnailPaths.push_back(variant.path);
        }
    }

    void ArtGenWidget::DropThumbnails() {
        // The images of a run are seen once: keeping them in the cache would hold every generated texture until the editor closes
        auto& resources = CoreManager::GetResourceManager();
        for (size_t i = 0; i < _thumbnailPaths.size(); ++i) {
            if (_thumbnails[i]) {
                resources.Evict(StringView(_thumbnailPaths[i].data(), _thumbnailPaths[i].size()));
            }
        }
        _thumbnails.clear();
        _thumbnailPaths.clear();
    }

}  // namespace BECore
