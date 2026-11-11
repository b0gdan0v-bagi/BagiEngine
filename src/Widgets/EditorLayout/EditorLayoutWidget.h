#pragma once

#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/CommandPalette.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <Widgets/EditorLayout/LayoutStore.h>
#include <Widgets/EditorLayout/PreferencesWindow.h>
#include <imgui.h>

namespace BECore {

    class IDeserializer;

    // Full-screen DockSpace host that provides the unified editor layout.
    // Must be registered immediately after ImGuiWidget so all other editor
    // windows dock into this space.
    //
    // It also owns the per-user editor state (EditorSettings, LayoutStore) and runs the commands it is in charge of
    // (EditorCommandEvent): quit, switch mode, reset and save the layout, open the palette and the preferences. It also turns the chords
    // of the hotkey map into those events.
    class EditorLayoutWidget : public IWidget {
        BE_CLASS(EditorLayoutWidget)
    public:
        EditorLayoutWidget() = default;
        ~EditorLayoutWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        void BuildDefaultLayout(ImGuiID dockspaceId, ImVec2 size);

        void OnCommand(const EditorCommandEvent& event);
        /// Keeps the preview choice of the viewport in the preferences and saves them
        void OnViewportPreviewChanged(const ViewportPreviewChangedEvent& event);
        /// Runs before ImGui::NewFrame (RenderEvents::ImGuiNewFrameEvent): the only safe moment to load an ini
        void OnImGuiNewFrame();

        /// Sets the theme and the font scale of ImGui. Called at start and when the preferences change, not every frame.
        void ApplyPreferences(const EditorPreferences& preferences);

        void DrawMenuBar();
        void DrawLayoutsMenu();
        void DrawSaveLayoutPopup();
        /// Emits the command of every chord pressed this frame
        void DispatchHotkeys();
        /// Tells the panels the preferences they read (EditorPreferencesChangedEvent)
        void AnnouncePreferences() const;
        /// A menu item that emits the command @p id, with its chord shown right-aligned
        void CommandMenuItem(PoolString id, const char* label, bool enabled = true);

        /// Reads the layout @p name and keeps its text until the next ImGuiNewFrameEvent
        void QueueLayout(const eastl::string& name);
        /// Writes the current dock layout under @p name; the error is a sentence for the popup
        std::expected<void, eastl::string> SaveCurrentLayout(eastl::string_view name);

        std::optional<EditorSettings> _settings;
        std::optional<LayoutStore> _layouts;  // none when the system gives no per-user folder

        CommandPalette _palette;
        PreferencesWindow _preferencesWindow;

        eastl::string _pendingLayoutText;  // a named layout waiting for the next frame start
        char _layoutNameBuffer[kMaxLayoutNameLength + 1] = {};
        eastl::string _saveLayoutError;
        bool _openSaveLayoutPopup = false;

        bool _preferencesAnnounced = false;
        bool _layoutInitialized = false;
        bool _forceResetLayout = false;
    };

}  // namespace BECore
