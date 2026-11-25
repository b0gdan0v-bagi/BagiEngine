#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/CommandPalette.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorPlayModel.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <Widgets/EditorLayout/IEditorLayoutHost.h>
#include <Widgets/EditorLayout/LayoutStore.h>
#include <Widgets/EditorLayout/MainToolbar.h>
#include <Widgets/EditorLayout/StatusBar.h>
#include <expected>
#include <imgui.h>

namespace BECore {

    class IDeserializer;

    // Full-screen DockSpace host that provides the unified editor layout, with the main toolbar under the menu bar and the status bar
    // at the bottom (both children of the host, so the dock layout and imgui.ini do not know them).
    // Must be registered immediately after ImGuiWidget so all other editor
    // windows dock into this space.
    //
    // It also owns the per-user editor state (EditorSettings, LayoutStore) and runs the commands it is in charge of
    // (EditorCommandEvent): quit, switch mode, reset the layout and open the palette (`view.preferences` is the Preferences asset's own command, `view.saveLayout` is run by `EditorDialogsModel`). It
    // also turns the chords of the hotkey map into those events. The toolbar's buttons are the asset `MainToolbarPanel`; its commands game.play, game.pause and game.step drive the engine's GamePause,
    // which is the single truth: the model copies its flag every frame.
    //
    // It is the IEditorLayoutHost of the editor (registered in Initialize, for its lifetime): the models of the panel assets reach the
    // settings and the layouts through it. A NotifyPreferencesChanged is applied and announced in the next Update.
    class EditorLayoutWidget : public IWidget, public IEditorLayoutHost {
        BE_CLASS(EditorLayoutWidget)
    public:
        EditorLayoutWidget() = default;
        /// Resumes a pause set from the editor (unless the manual clock holds it), so Game mode is never left frozen without a button
        ~EditorLayoutWidget() override;

        bool Initialize(IDeserializer& deserializer) override;
        void Update() override;
        void Draw() override;

        [[nodiscard]] EditorSettings* GetSettings() override;
        [[nodiscard]] LayoutStore* GetLayouts() override;
        [[nodiscard]] const EditorPlayModel* GetPlayModel() const override;
        /// Needs an ImGui context: the text is ImGui's ini of the moment
        std::expected<void, String> SaveCurrentLayoutAs(StringView name) override;
        void NotifyPreferencesChanged() override;
        void NotifyLayoutsChanged() override;

    private:
        void BuildDefaultLayout(ImGuiID dockspaceId, ImVec2 size);

        void OnCommand(const EditorCommandEvent& event);
        /// Keeps the preview choice of the viewport in the preferences and saves them
        void OnViewportPreviewChanged(const ViewportPreviewChangedEvent& event);
        /// Keeps the viewport's input mode (Play or Edit) in the preferences and saves them
        void OnViewportInputModeChanged(const ViewportInputModeChangedEvent& event);
        /// Appends the command to the saved Console history and saves the preferences
        void OnConsoleCommandSubmitted(const ConsoleCommandSubmittedEvent& event);
        /// Runs before ImGui::NewFrame (RenderEvents::ImGuiNewFrameEvent): the only safe moment to load an ini
        void OnImGuiNewFrame();

        /// Sets the font scale of ImGui (the colours and sizes are ImGuiTheme.h's, not a preference). Called at start and when the preferences change, not every frame.
        void ApplyPreferences(const EditorPreferences& preferences);

        void DrawMenuBar();
        void DrawLayoutsMenu();
        /// Emits the command of every chord pressed this frame
        void DispatchHotkeys();
        /// Pause / Resume through the model, written to the engine's GamePause (the pause of a manual clock is not lifted)
        void TogglePause();
        /// One stepped frame of kDefaultStepSeconds, only while the model is paused
        void StepOnce();
        /// Writes whether the model is Paused to GamePause
        void WritePauseToEngine();
        /// Tells the panels the preferences they read (EditorPreferencesChangedEvent)
        void AnnouncePreferences() const;
        /// A menu item that emits the command @p id, with its chord shown right-aligned
        void CommandMenuItem(PoolString id, const char* label, bool enabled = true);

        /// Reads the layout @p name and keeps its text until the next ImGuiNewFrameEvent
        void QueueLayout(const String& name);

        Optional<EditorSettings> _settings;
        Optional<LayoutStore> _layouts;  // none when the system gives no per-user folder
        LayoutNameCache _layoutNames;    // stale when a Layouts menu opens and after a save or a delete
        MenuOpenTracker _layoutsMenuOpen;
        MenuOpenTracker _deleteLayoutMenuOpen;

        EditorPlayModel _playModel;
        StatusBar _statusBar;

        CommandPalette _palette;

        String _pendingLayoutText;  // a named layout waiting for the next frame start

        EditorLayoutHostHandle _hostHandle;
        bool _preferencesNotified = false;  // NotifyPreferencesChanged waits here for the next Update
        bool _preferencesAnnounced = false;
        bool _layoutInitialized = false;
        bool _forceResetLayout = false;
    };

}  // namespace BECore
