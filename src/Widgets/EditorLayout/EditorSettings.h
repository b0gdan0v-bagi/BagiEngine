#pragma once

#include <BECore/Control/Json.h>
#include <BECore/UI/UIPreview.h>
#include <Widgets/EditorLayout/HotkeyMap.h>
#include <Widgets/Viewport/ViewportInput.h>

namespace BECore {

    inline constexpr float kMinFontScale = 0.75f;
    inline constexpr float kMaxFontScale = 2.0f;
    inline constexpr int32_t kMinViewportGridStep = 4;
    inline constexpr int32_t kMaxViewportGridStep = 256;

    /// The look of the editor, chosen in the preferences window. There is one theme (ImGuiTheme.h) and no setting for it; the
    /// `theme` key of an older `preferences.json` is kept as an unknown member and written back, never applied.
    struct EditorPreferences {
        float fontScale = 1.0f;
        bool viewportGrid = false;
        int32_t viewportGridStep = 32;
        /// The viewport's size, rotation, safe area and overlays: the user's choice, kept between runs (never part of a scene)
        PreviewSettings viewportPreview;
        /// Whether a press in the viewport goes to the game (Play) or to the editor (Edit); stored as "play" or "edit"
        ViewportInputMode viewportInputMode = ViewportInputMode::Play;

        [[nodiscard]] bool operator==(const EditorPreferences& other) const = default;
    };

    /// @p preferences with fontScale, viewportGridStep and the custom preview sides brought into their ranges, and a preview device index
    /// this build lacks turned into none
    [[nodiscard]] EditorPreferences ClampPreferences(EditorPreferences preferences);

    /// Names of the user files in the editor folder
    inline constexpr StringView kPreferencesFileName = "preferences.json";
    inline constexpr StringView kHotkeysFileName = "hotkeys.json";
    /// Name of the environment variable that moves the editor folder (tests, CI)
    inline constexpr const char* kEditorDirectoryEnvironmentVariable = "BAGI_EDITOR_DIR";

    /// The per-user folder of the editor files: the environment variable BAGI_EDITOR_DIR when it is set and not empty,
    /// else SDL_GetPrefPath of the engine's organization and "Editor". Empty when SDL gives no folder.
    [[nodiscard]] std::filesystem::path EditorUserDirectory();

    /**
     * @brief The preferences and the hotkeys of the editor, stored per user, never in the repository.
     *
     * Two JSON files in one folder (`preferences.json`, `hotkeys.json`, both with `"version": 1`). Needs no ImGui
     * context and no engine services, so a test can drive it with a folder of its own.
     *
     * Load never throws, asserts or writes. A missing file gives the defaults silently. A file that does not parse, or
     * a value of the wrong type, gives the default for that field and one warning in the log; the file stays as it is
     * until the user changes something, and the first save after a file that did not parse moves it to
     * `<name>.json.bad` so the old text is not lost. Keys that this build does not know are kept in memory and written
     * back, so the file of a newer build survives. `hotkeys.json` holds only the commands whose chord differs from the
     * command table (`"commandId": "Ctrl+K"`, or `""` for unbound), so a changed default of a later build reaches
     * users who never rebound the command. An entry of a command this build does not know is not applied but is kept
     * and written back; an entry with a bad chord, for a fixed command, or that conflicts is dropped with a warning.
     *
     * The viewport's preview choice is one object, `viewportPreview`, in `preferences.json`; its device is stored by name, and a
     * name this build lacks gives the Free shape, never another device. `viewportInputMode` is `"play"` or `"edit"`; a file without
     * the key (an older build) or with another text reads Play.
     *
     * Save writes atomically (UserFileIo.h). On failure it keeps the old file, logs the error and returns false; the
     * text is in GetLastSaveError().
     */
    class EditorSettings {
    public:
        explicit EditorSettings(std::filesystem::path directory);

        /// Reads both files (see above)
        void Load();

        bool SavePreferences();
        bool SaveHotkeys();

        [[nodiscard]] const EditorPreferences& GetPreferences() const {
            return _preferences;
        }
        /// Takes @p preferences clamped into their ranges; does not save
        void SetPreferences(const EditorPreferences& preferences);

        [[nodiscard]] HotkeyMap& GetHotkeys() {
            return _hotkeys;
        }
        [[nodiscard]] const HotkeyMap& GetHotkeys() const {
            return _hotkeys;
        }

        /// The error of the last Save call, empty when it succeeded
        [[nodiscard]] const eastl::string& GetLastSaveError() const {
            return _lastSaveError;
        }
        [[nodiscard]] const std::filesystem::path& GetDirectory() const {
            return _directory;
        }

    private:
        void LoadPreferences();
        void LoadHotkeys();
        bool Save(const std::filesystem::path& path, const Json& content, bool& unreadable);

        std::filesystem::path _directory;
        EditorPreferences _preferences;
        HotkeyMap _hotkeys;
        eastl::string _lastSaveError;

        // Members of the files that this build does not read, written back on save (JSON objects)
        Json _extraPreferences = Json::object();
        Json _extraViewportPreview = Json::object();  // members of the "viewportPreview" object that this build does not read
        Json _extraHotkeys = Json::object();
        Json _unknownBindings = Json::object();
        bool _preferencesUnreadable = false;
        bool _hotkeysUnreadable = false;
    };

}  // namespace BECore
