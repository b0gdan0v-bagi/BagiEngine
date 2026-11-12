#include "EditorLayoutWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Generated/EditorLayoutWidget.gen.hpp>
#include <Widgets/EditorLayout/GitBranch.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        constexpr const char* kSaveLayoutPopupId = "Save Layout As";
        constexpr float kSaveLayoutFieldWidth = 280.0f;

        /// Where the search for the engine's repository starts: the folder above the lowest layer of the `config` mount (the engine's own
        /// content), else the working directory
        std::filesystem::path BranchSearchStart() {
            const auto layers = CoreManager::GetFileSystem().GetMountLayers("config"_intern);
            if (!layers.empty() && layers.front().has_parent_path()) {
                return layers.front().parent_path();
            }
            std::error_code error;
            return std::filesystem::current_path(error);
        }

        /// Moves the cursor back over the gap ImGui leaves after an item, so the bars and the dock space touch
        void CloseItemGap() {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);
        }

    }  // namespace

    bool EditorLayoutWidget::Initialize(IDeserializer& /*deserializer*/) {
        const std::filesystem::path userDirectory = EditorUserDirectory();
        if (userDirectory.empty()) {
            LOG_WARNING("Editor: there is no per-user folder, so preferences, hotkeys and layouts are not stored");
        } else {
            _layouts.emplace(userDirectory);
        }
        _settings.emplace(userDirectory);
        _settings->Load();
        ApplyPreferences(_settings->GetPreferences());
        _statusBar.SetBranch(ReadGitBranch(BranchSearchStart()));

        Subscribe<EditorCommandEvent, &EditorLayoutWidget::OnCommand>(this);
        Subscribe<ViewportPreviewChangedEvent, &EditorLayoutWidget::OnViewportPreviewChanged>(this);
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
        } else if (event.commandId == kViewSaveLayout) {
            _openSaveLayoutPopup = _layouts.has_value();
        } else if (event.commandId == kViewCommandPalette) {
            _palette.Open();
        } else if (event.commandId == kViewPreferences) {
            _preferencesWindow.Open();
        }
    }

    void EditorLayoutWidget::OnViewportPreviewChanged(const ViewportPreviewChangedEvent& event) {
        // The viewport already shows this state, so the panels are not told again (no EditorPreferencesChangedEvent)
        EditorPreferences preferences = _settings->GetPreferences();
        preferences.viewportPreview = event.GetPreview();
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
        _toolbar.Draw(_playModel);
        CloseItemGap();
        DispatchHotkeys();
        DrawSaveLayoutPopup();
        if (const PoolString chosen = _palette.Draw(_settings->GetHotkeys()); !chosen.Empty()) {
            EditorCommandEvent::Emit(chosen);
        }

        // --- DockSpace ---
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

        // The panels' Initialize ran before the first Update, so they hear the stored preferences now
        bool preferencesChanged = !_preferencesAnnounced;
        _preferencesAnnounced = true;
        if (_preferencesWindow.Draw(*_settings)) {
            ApplyPreferences(_settings->GetPreferences());
            preferencesChanged = true;
        }
        if (preferencesChanged) {
            AnnouncePreferences();
        }
    }

    void EditorLayoutWidget::Draw() {}

    void EditorLayoutWidget::DrawMenuBar() {
        using namespace EditorCommandIds;
        if (!ImGui::BeginMenuBar()) {
            return;
        }
        if (ImGui::BeginMenu("File")) {
            CommandMenuItem(kFileQuit, "Quit");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            CommandMenuItem(kViewCommandPalette, "Command Palette...");
            CommandMenuItem(kViewPreferences, "Preferences...");
            ImGui::Separator();
            DrawLayoutsMenu();
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    void EditorLayoutWidget::DrawLayoutsMenu() {
        using namespace EditorCommandIds;
        const bool hasStore = _layouts.has_value();

        if (ImGui::BeginMenu("Layouts", hasStore)) {
            const eastl::vector<eastl::string> names = _layouts->List();
            if (names.empty()) {
                ImGui::TextDisabled("No saved layouts");
            }
            for (const eastl::string& name : names) {
                if (ImGui::MenuItem(name.c_str())) {
                    QueueLayout(name);
                }
            }
            ImGui::EndMenu();
        }

        CommandMenuItem(kViewSaveLayout, "Save Layout As...", hasStore);

        if (ImGui::BeginMenu("Delete Layout", hasStore)) {
            const eastl::vector<eastl::string> names = _layouts->List();
            if (names.empty()) {
                ImGui::TextDisabled("No saved layouts");
            }
            for (const eastl::string& name : names) {
                if (ImGui::MenuItem(name.c_str())) {
                    if (const auto result = _layouts->Delete(name); !result) {
                        LOG_WARNING(Format("Editor: cannot delete layout '{}': {}", name, result.error().detail));
                    }
                }
            }
            ImGui::EndMenu();
        }

        CommandMenuItem(kViewResetLayout, "Reset to Defaults");
    }

    void EditorLayoutWidget::AnnouncePreferences() const {
        const EditorPreferences& preferences = _settings->GetPreferences();
        EditorPreferencesChangedEvent::Emit(preferences.viewportGrid, preferences.viewportGridStep, preferences.viewportPreview);
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
            const std::optional<KeyChord> chord = hotkeys.GetChord(command.id);
            if (chord && ImGui::Shortcut(chord->ToImGui(), ImGuiInputFlags_RouteGlobal)) {
                EditorCommandEvent::Emit(command.id);
            }
        }
    }

    void EditorLayoutWidget::DrawSaveLayoutPopup() {
        if (_openSaveLayoutPopup) {
            _openSaveLayoutPopup = false;
            _layoutNameBuffer[0] = '\0';
            _saveLayoutError.clear();
            ImGui::OpenPopup(kSaveLayoutPopupId);
        }

        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(kSaveLayoutPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::TextUnformatted("Name of the layout:");
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(kSaveLayoutFieldWidth);
        const bool enterPressed = ImGui::InputText("##LayoutName", _layoutNameBuffer, sizeof(_layoutNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

        const eastl::string_view name = _layoutNameBuffer;
        const bool isEmpty = name.empty();
        const bool isValid = IsValidLayoutName(name);
        const bool exists = isValid && _layouts->Exists(name);

        // The button names what it will do, so replacing a layout is never a surprise
        if (!isEmpty && !isValid) {
            ImGui::TextColored(GetImGuiPalette().warning, "Use letters, digits, space, - and _ (no space at the ends)");
        } else if (exists) {
            ImGui::TextColored(GetImGuiPalette().warning, "A layout with this name exists; saving replaces it");
        }
        if (!_saveLayoutError.empty()) {
            ImGui::TextColored(GetImGuiPalette().warning, "%s", _saveLayoutError.c_str());
        }

        ImGui::BeginDisabled(!isValid);
        const bool savePressed = ImGui::Button(exists ? "Overwrite" : "Save");
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool cancelPressed = ImGui::Button("Cancel");

        // Enter saves a new name; replacing a layout needs a click on "Overwrite"
        if (savePressed || (enterPressed && isValid && !exists)) {
            if (const auto result = SaveCurrentLayout(name); result) {
                ImGui::CloseCurrentPopup();
            } else {
                _saveLayoutError = result.error();
            }
        } else if (cancelPressed) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void EditorLayoutWidget::CommandMenuItem(PoolString id, const char* label, bool enabled) {
        eastl::string chordText;
        if (_settings) {
            if (const std::optional<KeyChord> chord = _settings->GetHotkeys().GetChord(id)) {
                chordText = ToString(*chord);
            }
        }
        if (ImGui::MenuItem(label, chordText.c_str(), false, enabled)) {
            EditorCommandEvent::Emit(id);
        }
    }

    void EditorLayoutWidget::QueueLayout(const eastl::string& name) {
        const auto text = _layouts->Load(name);
        if (!text) {
            LOG_WARNING(Format("Editor: cannot load layout '{}': {}", name, text.error().detail));
            return;
        }
        // ImGui reports no error for a bad ini and drops the dock tree before it reads one: keep the current layout instead
        if (!LooksLikeLayoutText(*text)) {
            LOG_WARNING(Format("Editor: layout '{}' has no dock tree; the current layout stays", name));
            return;
        }
        _pendingLayoutText = *text;
    }

    std::expected<void, eastl::string> EditorLayoutWidget::SaveCurrentLayout(eastl::string_view name) {
        size_t size = 0;
        const char* text = ImGui::SaveIniSettingsToMemory(&size);
        // SaveIniSettingsToMemory cancels the pending write of imgui.ini
        ImGui::MarkIniSettingsDirty();
        const auto result = _layouts->Save(name, eastl::string_view(text, size));
        if (!result) {
            LOG_ERROR(Format("Editor: cannot save layout '{}': {}", name, result.error().detail));
            return std::unexpected(result.error().detail);
        }
        LOG_INFO(Format("Editor: layout '{}' saved", name));
        return {};
    }

    void EditorLayoutWidget::BuildDefaultLayout(ImGuiID id, ImVec2 size) {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(id, size);

        // Right: Sprite Library (25%)
        ImGuiID right, center;
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.25f, &right, &center);

        // Left: Scene Editor / Config Editor (22% of remaining)
        ImGuiID left, viewport;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, &left, &viewport);

        // Left panel split: Scene Editor (top 70%) / Config Editor (bottom 30%)
        ImGuiID leftTop, leftBottom;
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.3f, &leftBottom, &leftTop);

        ImGui::DockBuilderDockWindow("Scene Editor", leftTop);
        ImGui::DockBuilderDockWindow("Config Editor", leftBottom);
        ImGui::DockBuilderDockWindow("Viewport", viewport);
        ImGui::DockBuilderDockWindow("Sprite Library", right);

        ImGui::DockBuilderFinish(id);
    }

}  // namespace BECore
