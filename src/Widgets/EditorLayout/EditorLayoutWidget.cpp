#include "EditorLayoutWidget.h"

#include <BECore/Control/ControlService.h>
#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Time/GamePause.h>
#include <BECore/Widgets/WidgetManager.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Generated/EditorLayoutWidget.gen.hpp>
#include <Widgets/EditorLayout/GitBranch.h>
#include <Widgets/Feedback/TouchHighlights.h>
#include <Widgets/ImGuiLayoutText.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/PanelBars.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        constexpr StringView kMainMenuAsset = "MainMenuPanel";

        /// Where the search for the engine's repository starts: the folder above the lowest layer of the `config` mount (the engine's own
        /// content), else the working directory
        Path BranchSearchStart() {
            const auto layers = CoreManager::GetFileSystem().GetMountLayers(MountPoints::config);
            if (!layers.empty() && !layers.front().Parent().Empty()) {
                return layers.front().Parent();
            }
            const std::expected<String, String> current = CurrentDirectory();
            return current ? Path(*current) : Path();
        }

        /// Moves the cursor back over the gap ImGui leaves after an item, so the bars and the dock space touch
        void CloseItemGap() {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);
        }

    }  // namespace

    EditorLayoutWidget::~EditorLayoutWidget() {
        TouchHighlights::GetInstance().Reset();
        GamePause& pause = CoreManager::GetPause();
        if (pause.IsPaused() && !CoreManager::GetControlService().IsManualClock()) {
            pause.SetPaused(false);
        }
    }

    bool EditorLayoutWidget::Initialize(IDeserializer& /*deserializer*/) {
        const Path userDirectory = EditorUserDirectory();
        if (userDirectory.Empty()) {
            LOG_WARNING("Editor: there is no per-user folder, so preferences, hotkeys and layouts are not stored");
        } else {
            _layouts.emplace(userDirectory);
        }
        _settings.emplace(userDirectory);
        _settings->Load();
        ApplyPreferences(_settings->GetPreferences());
        _statusBar.SetBranch(ReadGitBranch(BranchSearchStart()));
        _statusBar.StartHardwareMonitor();

        _hostHandle = EditorLayoutHostRegistry::GetInstance().Register(*this);
        _toasts.Connect(CoreManager::GetControlService().Activity());
        TouchHighlights::GetInstance().Connect(CoreManager::GetControlService().Touches());

        Subscribe<EditorCommandEvent, &EditorLayoutWidget::OnCommand>(this);
        Subscribe<ViewportPreviewChangedEvent, &EditorLayoutWidget::OnViewportPreviewChanged>(this);
        Subscribe<ViewportInputModeChangedEvent, &EditorLayoutWidget::OnViewportInputModeChanged>(this);
        Subscribe<ConsoleCommandSubmittedEvent, &EditorLayoutWidget::OnConsoleCommandSubmitted>(this);
        Subscribe<RenderEvents::ImGuiNewFrameEvent, &EditorLayoutWidget::OnImGuiNewFrame>(this);
        return true;
    }

    void EditorLayoutWidget::ApplyPreferences(const EditorPreferences& preferences) {
        ImGui::GetStyle().FontScaleMain = preferences.fontScale;
    }

    void EditorLayoutWidget::OnCommand(const EditorCommandEvent& event) {
        using namespace EditorCommandIds;
        if (event.commandId == kFileQuit) {
            ApplicationEvents::QuitEvent::Emit();
        } else if (event.commandId == kModeToggle) {
            CoreManager::GetWidgetManager().RequestToggle();
        } else if (event.commandId == kViewResetLayout) {
            _forceResetLayout = true;
        } else if (event.commandId == kViewCommandPalette) {
            _palette.Open();
        } else if (event.commandId == kGamePlay) {
            _playModel.PressPlay();
            WritePauseToEngine();
        } else if (event.commandId == kGamePause) {
            TogglePause();
        } else if (event.commandId == kGameStep) {
            StepOnce();
        }
    }

    void EditorLayoutWidget::TogglePause() {
        _playModel.PressPause();
        WritePauseToEngine();
    }

    void EditorLayoutWidget::StepOnce() {
        if (_playModel.PressStep()) {
            CoreManager::GetPause().RequestSteps(1, kDefaultStepSeconds);
        }
    }

    void EditorLayoutWidget::WritePauseToEngine() {
        const bool paused = _playModel.GetState() == EditorPlayState::Paused;
        // A manual clock is a pause that only `step` advances: the model catches up with it on the next frame
        if (!paused && CoreManager::GetControlService().IsManualClock()) {
            return;
        }
        CoreManager::GetPause().SetPaused(paused);
    }

    void EditorLayoutWidget::OnViewportPreviewChanged(const ViewportPreviewChangedEvent& event) {
        // The viewport already shows this state, so the panels are not told again (no EditorPreferencesChangedEvent)
        EditorPreferences preferences = _settings->GetPreferences();
        preferences.viewportPreview = event.GetPreview();
        _settings->SetPreferences(preferences);
        _settings->SavePreferences();
    }

    void EditorLayoutWidget::OnViewportInputModeChanged(const ViewportInputModeChangedEvent& event) {
        // The viewport already shows this mode, so the panels are not told again (no EditorPreferencesChangedEvent)
        EditorPreferences preferences = _settings->GetPreferences();
        preferences.viewportInputMode = event.mode;
        _settings->SetPreferences(preferences);
        _settings->SavePreferences();
    }

    void EditorLayoutWidget::OnConsoleCommandSubmitted(const ConsoleCommandSubmittedEvent& event) {
        // The Console keeps its own history, so the panels are not told again (no EditorPreferencesChangedEvent)
        EditorPreferences preferences = _settings->GetPreferences();
        AddConsoleHistory(preferences, StringView(event.command.data(), event.command.size()));
        _settings->SetPreferences(preferences);
        _settings->SavePreferences();
    }

    void EditorLayoutWidget::OnImGuiNewFrame() {
        if (_pendingLayoutText.empty()) {
            return;
        }
        ImGui::LoadIniSettingsFromMemory(_pendingLayoutText.c_str(), _pendingLayoutText.size());
        _pendingLayoutText.clear();
        // imgui.ini follows the loaded layout; and if the text had no tree for our dockspace, Update builds the default one
        ImGui::MarkIniSettingsDirty();
        _layoutInitialized = false;
    }

    void EditorLayoutWidget::Update() {
        // The control API and the manual clock can pause too
        _playModel.SetPaused(CoreManager::GetPause().IsPaused());
        // Before the panels that draw them, which come after this widget
        TouchHighlights::GetInstance().Update(ImGui::GetIO().DeltaTime);

        // Cover the entire OS window work-area with a borderless, decoration-less host.
        ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::SetNextWindowViewport(mainViewport->ID);

        constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                               ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("##EditorRoot", nullptr, hostFlags);
        ImGui::PopStyleVar(3);

        DrawMenuBar();
        MainToolbar::Draw();
        CloseItemGap();
        DispatchHotkeys();
        if (const PoolString chosen = _palette.Draw(_settings->GetHotkeys()); !chosen.Empty()) {
            EditorCommandEvent::Emit(chosen);
        }

        // It takes what the status bar leaves; the bars are children of the host, so no dock node can overlap them
        const float dockHeight = eastl::max(0.0f, ImGui::GetContentRegionAvail().y - StatusBar::GetHeight());
        const ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");

        bool doReset = _forceResetLayout;
        _forceResetLayout = false;

        if (!_layoutInitialized) {
            _layoutInitialized = true;
            doReset = (ImGui::DockBuilderGetNode(dockspaceId) == nullptr);
        }

        if (doReset) {
            BuildDefaultLayout(dockspaceId, ImVec2(mainViewport->WorkSize.x, dockHeight));
        }

        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, dockHeight), ImGuiDockNodeFlags_None);
        CloseItemGap();
        _statusBar.Draw();

        ImGui::End();
        _toasts.Draw(StatusBar::GetHeight());

        // The panels' Initialize ran before the first Update, so they hear the stored preferences now
        bool preferencesChanged = !_preferencesAnnounced;
        _preferencesAnnounced = true;
        if (_preferencesNotified) {
            _preferencesNotified = false;
            ApplyPreferences(_settings->GetPreferences());
            preferencesChanged = true;
        }
        if (preferencesChanged) {
            AnnouncePreferences();
        }
    }

    void EditorLayoutWidget::Draw() {}

    EditorSettings* EditorLayoutWidget::GetSettings() {
        return _settings ? &*_settings : nullptr;
    }

    LayoutStore* EditorLayoutWidget::GetLayouts() {
        return _layouts ? &*_layouts : nullptr;
    }

    const EditorPlayModel* EditorLayoutWidget::GetPlayModel() const {
        return &_playModel;
    }

    void EditorLayoutWidget::NotifyPreferencesChanged() {
        _preferencesNotified = true;
    }

    void EditorLayoutWidget::DrawMenuBar() {
        if (!ImGui::BeginMenuBar()) {
            return;
        }
        PanelBarsRegistry::Draw(kMainMenuAsset);
        ImGui::EndMenuBar();
    }

    void EditorLayoutWidget::AnnouncePreferences() const {
        const EditorPreferences& preferences = _settings->GetPreferences();
        EditorPreferencesChangedEvent::Emit(preferences.viewportGrid, preferences.viewportGridStep, preferences.viewportPreview, preferences.viewportInputMode, preferences.externalEditor,
                                            preferences.consoleHistory);
    }

    void EditorLayoutWidget::DispatchHotkeys() {
        // A menu, the palette or a modal popup is open: the keys belong to it
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
            return;
        }
        const HotkeyMap& hotkeys = _settings->GetHotkeys();
        for (const EditorCommand& command : GetEditorCommands()) {
            // The fixed command (mode.toggle) is read by Application from the raw SDL event, before ImGui
            if (!command.rebindable) {
                continue;
            }
            const Optional<KeyChord> chord = hotkeys.GetChord(command.id);
            const ImGuiInputFlags flags = command.repeats ? ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_Repeat : ImGuiInputFlags_RouteGlobal;
            if (chord && ImGui::Shortcut(chord->ToImGui(), flags)) {
                EditorCommandEvent::Emit(command.id);
            }
        }
    }

    void EditorLayoutWidget::LoadLayout(StringView name) {
        if (!_layouts) {
            return;
        }
        const auto text = _layouts->Load(name);
        if (!text) {
            LOG_WARNING("Editor: cannot load layout '{}': {}", name, text.error().detail);
            return;
        }
        // ImGui reports no error for a bad ini and drops the dock tree before it reads one: keep the current layout instead
        if (!LooksLikeLayoutText(*text)) {
            LOG_WARNING("Editor: layout '{}' has no dock tree; the current layout stays", name);
            return;
        }
        // a layout saved before a window was renamed would float the window
        _pendingLayoutText = MigrateLegacyWindowNames(*text);
    }

    std::expected<void, String> EditorLayoutWidget::SaveCurrentLayoutAs(StringView name) {
        size_t size = 0;
        const char* text = ImGui::SaveIniSettingsToMemory(&size);
        // SaveIniSettingsToMemory cancels the pending write of imgui.ini
        ImGui::MarkIniSettingsDirty();
        const auto result = _layouts->Save(name, StringView(text, size));
        if (!result) {
            LOG_ERROR("Editor: cannot save layout '{}': {}", name, result.error().detail);
            return std::unexpected(result.error().detail);
        }
        LOG_INFO("Editor: layout '{}' saved", name);
        return {};
    }

    void EditorLayoutWidget::BuildDefaultLayout(ImGuiID id, ImVec2 size) {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(id, size);

        // Right: Sprite Library
        ImGuiID right, center;
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.25f, &right, &center);

        // Left: Scene Editor and Config Editor
        ImGuiID left, viewport;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, &left, &viewport);

        // Left panel: Scene Editor above Config Editor
        ImGuiID leftTop, leftBottom;
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.3f, &leftBottom, &leftTop);

        ImGui::DockBuilderDockWindow("Scene Editor", leftTop);
        ImGui::DockBuilderDockWindow("Config Editor###ConfigEditor", leftBottom);
        ImGui::DockBuilderDockWindow("Viewport", viewport);
        ImGui::DockBuilderDockWindow("Sprite Library###SpriteLibrary", right);

        ImGui::DockBuilderFinish(id);
    }

}  // namespace BECore
