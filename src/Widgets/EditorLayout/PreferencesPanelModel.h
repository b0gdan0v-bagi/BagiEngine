#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <Widgets/EditorLayout/KeyChord.h>
#include <Widgets/EditorLayout/PreferencesHotkeys.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <cstdint>

namespace BECore {

    class IEditorLayoutHost;

    /**
     * @brief What `PreferencesPanel.xml` binds to: the fields of the General tab
     *
     * The controls of the panel write `fontScale`, `viewportGrid`, `viewportGridStep` and `externalEditor` the moment the user changes them; the
     * model reads them in its next `Update`. Every other field is the model's. The Hotkeys tab's fields are made again only when a hotkey command
     * ran or a chord was captured; `rebindOpen` is the `open` of the Rebind Shortcut popup.
     */
    struct PreferencesPanelView {
        BE_CLASS(PreferencesPanelView)

        BE_REFLECT_FIELD float fontScale = 1.0f;
        BE_REFLECT_FIELD bool viewportGrid = false;
        BE_REFLECT_FIELD int32_t viewportGridStep = 32;
        BE_REFLECT_FIELD String externalEditor;
        /// The editor's host can start the program of `externalEditor`: a browser page cannot
        BE_REFLECT_FIELD bool externalEditorSupported = true;
        /// The text of the last failed save; empty after a save that worked
        BE_REFLECT_FIELD String error;
        BE_REFLECT_FIELD bool hasError = false;
        /// The line shown for `error`
        BE_REFLECT_FIELD String message;
        /// There is no editor host or it holds no settings: the fields are not applied, nor stored
        BE_REFLECT_FIELD bool noFolder = true;

        BE_REFLECT_FIELD Vector<HotkeyRow> hotkeyRows;
        BE_REFLECT_FIELD bool rebindOpen = false;
        /// The title of the command being rebound
        BE_REFLECT_FIELD String rebindTitle;
        /// What the Capturing stage asks of the user
        BE_REFLECT_FIELD String rebindText;
        BE_REFLECT_FIELD String refusal;
        BE_REFLECT_FIELD bool hasRefusal = false;
        BE_REFLECT_FIELD String conflictText;
        BE_REFLECT_FIELD bool inConflict = false;
        /// The last failed save of the hotkeys, or why a Reset did nothing
        BE_REFLECT_FIELD String hotkeysMessage;
        BE_REFLECT_FIELD bool hasHotkeysMessage = false;
    };

    /**
     * @brief The model of the Preferences window asset: the General and Hotkeys tabs over the host's `EditorSettings`, with no ImGui but the key read
     *
     * The host is the `IEditorLayoutHost` of the editor (`EditorLayoutHostRegistry`); the model never holds it between calls, so a host that
     * goes away leaves the panel with `noFolder`. A changed field is applied to the host's settings at once and the host is told
     * (`NotifyPreferencesChanged`: the font scale is live, the panels read the grid and the editor). It is saved when the user lets go of the
     * control (`preferences.commit`, the `commitCommand` of the controls), and a changed grid check is saved at once. A save that fails sets `error`.
     * The settings clamp what they take, and the view shows the clamped value from the next frame on.
     *
     * The Hotkeys tab is `PreferencesHotkeys` behind the view. The rebind dialog reads the next chord from a `ChordReader` while it is Capturing (ImGui's
     * key state, which is final before `Update` runs; a test sets its own). The first `Update` after the dialog opened reads nothing, so the key that
     * pressed the Rebind button is never taken.
     *
     * Commands (`EditorCommandIds`, run only while the panel is open): `preferences.commit`; `preferences.rebind`, `preferences.clear` and
     * `preferences.reset` take a command id as the argument (an unknown or fixed one is ignored); `preferences.resetAll`, `preferences.swap` and
     * `preferences.cancelRebind`, which is also the popup's `closeCommand`.
     */
    class PreferencesPanelModel : public IPanelModel {
        BE_CLASS(PreferencesPanelModel)
    public:
        PreferencesPanelModel();

        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        /// Reads the view from the host's preferences when the panel opens, so a value changed behind the panel's back is not written over
        void OnOpenChanged(bool open) override;
        bool RunCommand(StringView id, StringView argument) override;

        /// The chord pressed in this frame, if any
        using ChordReader = MoveOnlyFunction<Optional<KeyChord>()>;
        /// Reads chords through @p reader instead of ImGui (a test)
        void SetChordReader(ChordReader reader);

        [[nodiscard]] PreferencesPanelView& GetView() {
            return _view;
        }
        [[nodiscard]] const PreferencesPanelView& GetView() const {
            return _view;
        }

    private:
        /// The fields of the view as they were last read from or given to the settings
        struct Applied {
            float fontScale = 1.0f;
            bool viewportGrid = false;
            int32_t viewportGridStep = 32;
            String externalEditor;
        };

        /// The host when it holds settings; sets `noFolder` either way
        [[nodiscard]] IEditorLayoutHost* FindHost();
        void LoadFromSettings(const EditorPreferences& preferences);
        /// Gives a changed view to the settings; saves at once when the grid check changed
        void ApplyView(IEditorLayoutHost& host);
        void Save(EditorSettings& settings);
        /// Makes the rows from the host's hotkeys and copies the texts of the dialog into the view
        void LoadHotkeys(EditorSettings& settings);
        void SyncHotkeyView();
        /// While the dialog is Capturing: reads the pressed chord and gives it to `PreferencesHotkeys`
        void UpdateCapture(EditorSettings& settings);
        bool RunHotkeyCommand(StringView id, StringView argument, EditorSettings* settings);

        PreferencesPanelView _view;
        PreferencesHotkeys _hotkeys;
        ChordReader _readChord;
        bool _skipCapture = false;  // the frame after the dialog opened
        Applied _applied;
        bool _loaded = false;  // the view holds the settings' values
    };

}  // namespace BECore
