#include "EditorSettings.h"

#include <SDL3/SDL_filesystem.h>
#include <Widgets/EditorLayout/UserFileIo.h>

namespace BECore {

    namespace {

        constexpr int kFileVersion = 1;
        constexpr const char* kEditorOrganization = "BagiEngine";
        constexpr const char* kEditorApplication = "Editor";
        constexpr int kJsonIndent = 2;
        constexpr eastl::string_view kBadFileSuffix = ".bad";

        constexpr eastl::string_view kVersionKey = "version";
        constexpr eastl::string_view kThemeKey = "theme";
        constexpr eastl::string_view kFontScaleKey = "fontScale";
        constexpr eastl::string_view kViewportGridKey = "viewportGrid";
        constexpr eastl::string_view kViewportGridStepKey = "viewportGridStep";
        constexpr eastl::string_view kBindingsKey = "bindings";

        std::string ToStdString(eastl::string_view text) {
            return std::string(text.data(), text.size());
        }

        // Reads the file at @p path into @p content. Missing file: false, no warning. A file that cannot be read or
        // parsed, or whose root is not an object: false, one warning, @p unreadable set.
        bool ReadJsonObject(const std::filesystem::path& path, Json& content, bool& unreadable) {
            unreadable = false;
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) {
                return false;
            }
            unreadable = true;
            const std::expected<eastl::string, eastl::string> text = ReadTextFile(path);
            if (!text) {
                LOG_WARNING(Format("Editor settings: {}; using the defaults", text.error()));
                return false;
            }
            std::expected<Json, ControlError> parsed = ParseJson(eastl::string_view(text->data(), text->size()));
            if (!parsed || !parsed->is_object()) {
                LOG_WARNING(Format("Editor settings: '{}' is not a JSON object; using the defaults", path.string()));
                return false;
            }
            content = std::move(*parsed);
            unreadable = false;
            return true;
        }

        // The members of @p object that are not in @p known
        Json UnknownMembers(const Json& object, std::initializer_list<eastl::string_view> known) {
            Json extra = Json::object();
            for (const auto& [key, value] : object.items()) {
                const bool isKnown = std::ranges::any_of(known, [&](eastl::string_view name) { return key == ToStdString(name); });
                if (!isKnown) {
                    extra[key] = value;
                }
            }
            return extra;
        }

        void WarnWrongType(eastl::string_view file, eastl::string_view key, eastl::string_view expected) {
            LOG_WARNING(Format("Editor settings: '{}' in {} is not {}; using the default", key, file, expected));
        }

    }  // namespace

    EditorPreferences ClampPreferences(EditorPreferences preferences) {
        preferences.fontScale = std::clamp(preferences.fontScale, kMinFontScale, kMaxFontScale);
        preferences.viewportGridStep = std::clamp(preferences.viewportGridStep, kMinViewportGridStep, kMaxViewportGridStep);
        return preferences;
    }

    std::filesystem::path EditorUserDirectory() {
        if (const char* overridden = std::getenv(kEditorDirectoryEnvironmentVariable); overridden != nullptr && overridden[0] != '\0') {
            return std::filesystem::path(overridden);
        }
        char* prefPath = SDL_GetPrefPath(kEditorOrganization, kEditorApplication);
        if (prefPath == nullptr) {
            LOG_WARNING(Format("Editor settings: no per-user folder: {}", SDL_GetError()));
            return {};
        }
        std::filesystem::path directory(prefPath);
        SDL_free(prefPath);
        return directory;
    }

    EditorSettings::EditorSettings(std::filesystem::path directory) : _directory(std::move(directory)) {}

    void EditorSettings::SetPreferences(const EditorPreferences& preferences) {
        _preferences = ClampPreferences(preferences);
    }

    void EditorSettings::Load() {
        LoadPreferences();
        LoadHotkeys();
    }

    void EditorSettings::LoadPreferences() {
        _preferences = EditorPreferences{};
        _extraPreferences = Json::object();

        Json content;
        if (!ReadJsonObject(_directory / ToStdString(kPreferencesFileName), content, _preferencesUnreadable)) {
            return;
        }
        _extraPreferences = UnknownMembers(content, {kVersionKey, kThemeKey, kFontScaleKey, kViewportGridKey, kViewportGridStepKey});

        EditorPreferences preferences;
        if (content.contains(ToStdString(kThemeKey))) {
            const std::optional<eastl::string> name = GetJsonString(content, kThemeKey);
            const std::optional<EditorTheme> theme = name ? EnumUtils<EditorTheme>::Cast(eastl::string_view(name->data(), name->size())) : std::nullopt;
            if (theme) {
                preferences.theme = *theme;
            } else {
                WarnWrongType(kPreferencesFileName, kThemeKey, "Dark, Light or Classic");
            }
        }
        if (content.contains(ToStdString(kFontScaleKey))) {
            if (const std::optional<double> scale = GetJsonNumber(content, kFontScaleKey)) {
                preferences.fontScale = static_cast<float>(*scale);
            } else {
                WarnWrongType(kPreferencesFileName, kFontScaleKey, "a number");
            }
        }
        if (content.contains(ToStdString(kViewportGridKey))) {
            if (const std::optional<bool> grid = GetJsonBool(content, kViewportGridKey)) {
                preferences.viewportGrid = *grid;
            } else {
                WarnWrongType(kPreferencesFileName, kViewportGridKey, "true or false");
            }
        }
        if (content.contains(ToStdString(kViewportGridStepKey))) {
            if (const std::optional<int64_t> step = GetJsonInt(content, kViewportGridStepKey)) {
                preferences.viewportGridStep = static_cast<int32_t>(std::clamp<int64_t>(*step, kMinViewportGridStep, kMaxViewportGridStep));
            } else {
                WarnWrongType(kPreferencesFileName, kViewportGridStepKey, "an integer");
            }
        }
        _preferences = ClampPreferences(preferences);
    }

    void EditorSettings::LoadHotkeys() {
        _hotkeys = HotkeyMap();
        _extraHotkeys = Json::object();
        _unknownBindings = Json::object();

        Json content;
        if (!ReadJsonObject(_directory / ToStdString(kHotkeysFileName), content, _hotkeysUnreadable)) {
            return;
        }
        _extraHotkeys = UnknownMembers(content, {kVersionKey, kBindingsKey});

        const Json* bindings = FindJsonMember(content, kBindingsKey);
        if (bindings == nullptr) {
            return;
        }
        if (!bindings->is_object()) {
            WarnWrongType(kHotkeysFileName, kBindingsKey, "an object");
            return;
        }

        struct Override {
            PoolString id;
            std::optional<KeyChord> chord;
        };
        eastl::vector<Override> overrides;
        for (const auto& [key, value] : bindings->items()) {
            const eastl::string id(key.c_str(), key.size());
            const PoolString commandId = PoolString::Intern(eastl::string_view(id.data(), id.size()));
            if (!value.is_string()) {
                LOG_WARNING(Format("Editor settings: the hotkey of '{}' is not a text; dropped", id));
                continue;
            }
            const std::string& text = value.get_ref<const std::string&>();
            const EditorCommand* command = FindEditorCommand(commandId);
            if (command == nullptr) {
                LOG_WARNING(Format("Editor settings: unknown command '{}' in {}; not applied, kept in the file", id, kHotkeysFileName));
                _unknownBindings[key] = value;
                continue;
            }
            if (!command->rebindable) {
                LOG_WARNING(Format("Editor settings: the hotkey of '{}' is fixed; dropped", id));
                continue;
            }
            if (text.empty()) {
                overrides.push_back({commandId, std::nullopt});
                continue;
            }
            const std::optional<KeyChord> chord = ParseKeyChord(eastl::string_view(text.data(), text.size()));
            if (!chord) {
                LOG_WARNING(Format("Editor settings: '{}' is not a chord ({}); dropped", text.c_str(), id));
                continue;
            }
            overrides.push_back({commandId, chord});
        }

        // Every overridden command lets go of its chord first, so that two commands can swap chords in the file
        for (const Override& entry : overrides) {
            _hotkeys.Unbind(entry.id);
        }
        for (const Override& entry : overrides) {
            if (!entry.chord) {
                continue;
            }
            const AssignResult result = _hotkeys.Assign(entry.id, *entry.chord);
            if (result.status == AssignStatus::Assigned) {
                continue;
            }
            const eastl::string chordText = ToString(*entry.chord);
            if (result.status == AssignStatus::Conflict) {
                LOG_WARNING(Format("Editor settings: {} for '{}' is already used by '{}'; dropped", chordText, entry.id.ToStringView(), result.otherCommandId.ToStringView()));
            } else {
                LOG_WARNING(Format("Editor settings: {} is not allowed for '{}'; dropped", chordText, entry.id.ToStringView()));
            }
            _hotkeys.ResetOne(entry.id);
        }
    }

    bool EditorSettings::Save(const std::filesystem::path& path, const Json& content, bool& unreadable) {
        _lastSaveError.clear();
        if (_directory.empty()) {
            _lastSaveError = "There is no folder for the editor settings";
            LOG_ERROR(Format("Editor settings: {}", _lastSaveError));
            return false;
        }

        // A file that did not parse is moved aside once, so the user's old text is not lost to the first change
        if (unreadable) {
            std::filesystem::path backup = path;
            backup += ToStdString(kBadFileSuffix);
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
                std::filesystem::rename(path, backup, ec);
                if (!ec) {
                    LOG_WARNING(Format("Editor settings: the unreadable '{}' was kept as '{}'", path.string(), backup.string()));
                }
            }
            unreadable = false;
        }

        const std::string text = content.dump(kJsonIndent, ' ', false, Json::error_handler_t::replace) + "\n";
        const std::expected<void, eastl::string> written = WriteTextFileAtomically(path, eastl::string_view(text.data(), text.size()));
        if (!written) {
            _lastSaveError = written.error();
            LOG_ERROR(Format("Editor settings: {}", _lastSaveError));
            return false;
        }
        return true;
    }

    bool EditorSettings::SavePreferences() {
        Json content = _extraPreferences;
        content[ToStdString(kVersionKey)] = kFileVersion;
        const eastl::string_view theme = EnumUtils<EditorTheme>::ToString(_preferences.theme);
        content[ToStdString(kThemeKey)] = ToStdString(theme);
        content[ToStdString(kFontScaleKey)] = _preferences.fontScale;
        content[ToStdString(kViewportGridKey)] = _preferences.viewportGrid;
        content[ToStdString(kViewportGridStepKey)] = _preferences.viewportGridStep;
        return Save(_directory / ToStdString(kPreferencesFileName), content, _preferencesUnreadable);
    }

    bool EditorSettings::SaveHotkeys() {
        Json bindings = _unknownBindings;
        for (const EditorCommand& command : GetEditorCommands()) {
            if (!command.rebindable || _hotkeys.IsDefault(command.id)) {
                continue;
            }
            const std::optional<KeyChord> chord = _hotkeys.GetChord(command.id);
            const eastl::string text = chord ? ToString(*chord) : eastl::string();
            bindings[ToStdString(command.id.ToStringView())] = std::string(text.c_str(), text.size());
        }
        Json content = _extraHotkeys;
        content[ToStdString(kVersionKey)] = kFileVersion;
        content[ToStdString(kBindingsKey)] = std::move(bindings);
        return Save(_directory / ToStdString(kHotkeysFileName), content, _hotkeysUnreadable);
    }

}  // namespace BECore
