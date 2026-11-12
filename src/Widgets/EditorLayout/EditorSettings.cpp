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
        constexpr eastl::string_view kFontScaleKey = "fontScale";
        constexpr eastl::string_view kViewportGridKey = "viewportGrid";
        constexpr eastl::string_view kViewportGridStepKey = "viewportGridStep";
        constexpr eastl::string_view kBindingsKey = "bindings";
        constexpr eastl::string_view kViewportPreviewKey = "viewportPreview";

        // The members of the "viewportPreview" object
        constexpr eastl::string_view kPreviewAspectKey = "aspect";
        constexpr eastl::string_view kPreviewDeviceKey = "device";
        constexpr eastl::string_view kPreviewCustomKey = "custom";
        constexpr eastl::string_view kPreviewCustomWidthKey = "customWidth";
        constexpr eastl::string_view kPreviewCustomHeightKey = "customHeight";
        constexpr eastl::string_view kPreviewRotatedKey = "rotated";
        constexpr eastl::string_view kPreviewSafeAreaKey = "safeArea";
        constexpr eastl::string_view kOverlaySafeAreaKey = "overlaySafeArea";
        constexpr eastl::string_view kOverlayViewBoundsKey = "overlayViewBounds";
        constexpr eastl::string_view kOverlayCanvasBoundsKey = "overlayCanvasBounds";
        constexpr eastl::string_view kOverlayTouchTargetsKey = "overlayTouchTargets";
        constexpr eastl::string_view kOverlayRegionsKey = "overlayRegions";

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

        // The index of the device called @p name in the table of this build, -1 when there is none
        int32_t FindDeviceByName(eastl::string_view name) {
            const auto devices = UIPreview::GetDevices();
            for (size_t i = 0; i < devices.size(); ++i) {
                if (devices[i].name == name) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        // A bool member of @p object: left in @p value when absent, a warning and left when of another type
        void ReadBool(const Json& object, eastl::string_view key, bool& value) {
            if (!object.contains(ToStdString(key))) {
                return;
            }
            if (const std::optional<bool> read = GetJsonBool(object, key)) {
                value = *read;
            } else {
                WarnWrongType(kPreferencesFileName, key, "true or false");
            }
        }

        // A side of the custom size: clamped into its range like the grid step
        void ReadSide(const Json& object, eastl::string_view key, uint32_t& value) {
            if (!object.contains(ToStdString(key))) {
                return;
            }
            if (const std::optional<int64_t> read = GetJsonInt(object, key)) {
                value = static_cast<uint32_t>(std::clamp<int64_t>(*read, kMinPreviewSide, kMaxPreviewSide));
            } else {
                WarnWrongType(kPreferencesFileName, key, "an integer");
            }
        }

        // An enum member of @p object by name
        template <typename TEnum>
        void ReadEnum(const Json& object, eastl::string_view key, TEnum& value) {
            if (!object.contains(ToStdString(key))) {
                return;
            }
            const std::optional<eastl::string> name = GetJsonString(object, key);
            const std::optional<TEnum> parsed = name ? EnumUtils<TEnum>::Cast(eastl::string_view(name->data(), name->size())) : std::nullopt;
            if (parsed) {
                value = *parsed;
            } else {
                WarnWrongType(kPreferencesFileName, key, "a known name");
            }
        }

        // Reads the "viewportPreview" object into @p preview and gives the members it does not know to @p extra
        void ReadViewportPreview(const Json& object, PreviewSettings& preview, Json& extra) {
            extra = UnknownMembers(object, {kPreviewAspectKey, kPreviewDeviceKey, kPreviewCustomKey, kPreviewCustomWidthKey, kPreviewCustomHeightKey, kPreviewRotatedKey, kPreviewSafeAreaKey,
                                            kOverlaySafeAreaKey, kOverlayViewBoundsKey, kOverlayCanvasBoundsKey, kOverlayTouchTargetsKey, kOverlayRegionsKey});
            PreviewShape& shape = preview.shape;
            ReadEnum(object, kPreviewAspectKey, shape.aspect);
            ReadBool(object, kPreviewCustomKey, shape.custom);
            ReadSide(object, kPreviewCustomWidthKey, shape.customWidth);
            ReadSide(object, kPreviewCustomHeightKey, shape.customHeight);
            ReadBool(object, kPreviewRotatedKey, shape.rotated);
            ReadEnum(object, kPreviewSafeAreaKey, preview.safeArea);
            ReadBool(object, kOverlaySafeAreaKey, preview.overlays.safeArea);
            ReadBool(object, kOverlayViewBoundsKey, preview.overlays.viewBounds);
            ReadBool(object, kOverlayCanvasBoundsKey, preview.overlays.canvasBounds);
            ReadBool(object, kOverlayTouchTargetsKey, preview.overlays.touchTargets);
            ReadBool(object, kOverlayRegionsKey, preview.overlays.regions);

            // The device is stored by name, so a table that changed between builds never gives another device
            if (object.contains(ToStdString(kPreviewDeviceKey))) {
                const std::optional<eastl::string> name = GetJsonString(object, kPreviewDeviceKey);
                if (!name) {
                    WarnWrongType(kPreferencesFileName, kPreviewDeviceKey, "a device name");
                } else if (!name->empty()) {
                    shape.device = FindDeviceByName(eastl::string_view(name->data(), name->size()));
                    if (shape.device < 0) {
                        LOG_WARNING(Format("Editor settings: the preview device '{}' in {} is not known to this build; using the Free shape", *name, kPreferencesFileName));
                        if (!shape.custom) {
                            shape.aspect = ViewportAspect::Free;
                        }
                        if (preview.safeArea == SafeAreaSimulation::Device) {
                            preview.safeArea = SafeAreaSimulation::None;
                        }
                    }
                }
            }
        }

        Json ViewportPreviewToJson(const PreviewSettings& preview, const Json& extra) {
            Json object = extra;
            const PreviewShape& shape = preview.shape;
            const auto devices = UIPreview::GetDevices();
            const bool hasDevice = shape.device >= 0 && static_cast<size_t>(shape.device) < devices.size();
            object[ToStdString(kPreviewAspectKey)] = ToStdString(EnumUtils<ViewportAspect>::ToString(shape.aspect));
            object[ToStdString(kPreviewDeviceKey)] = hasDevice ? ToStdString(devices[static_cast<size_t>(shape.device)].name) : std::string();
            object[ToStdString(kPreviewCustomKey)] = shape.custom;
            object[ToStdString(kPreviewCustomWidthKey)] = shape.customWidth;
            object[ToStdString(kPreviewCustomHeightKey)] = shape.customHeight;
            object[ToStdString(kPreviewRotatedKey)] = shape.rotated;
            object[ToStdString(kPreviewSafeAreaKey)] = ToStdString(EnumUtils<SafeAreaSimulation>::ToString(preview.safeArea));
            object[ToStdString(kOverlaySafeAreaKey)] = preview.overlays.safeArea;
            object[ToStdString(kOverlayViewBoundsKey)] = preview.overlays.viewBounds;
            object[ToStdString(kOverlayCanvasBoundsKey)] = preview.overlays.canvasBounds;
            object[ToStdString(kOverlayTouchTargetsKey)] = preview.overlays.touchTargets;
            object[ToStdString(kOverlayRegionsKey)] = preview.overlays.regions;
            return object;
        }

    }  // namespace

    EditorPreferences ClampPreferences(EditorPreferences preferences) {
        preferences.fontScale = std::clamp(preferences.fontScale, kMinFontScale, kMaxFontScale);
        preferences.viewportGridStep = std::clamp(preferences.viewportGridStep, kMinViewportGridStep, kMaxViewportGridStep);
        PreviewShape& shape = preferences.viewportPreview.shape;
        shape.customWidth = std::clamp(shape.customWidth, kMinPreviewSide, kMaxPreviewSide);
        shape.customHeight = std::clamp(shape.customHeight, kMinPreviewSide, kMaxPreviewSide);
        if (shape.device < 0 || static_cast<size_t>(shape.device) >= UIPreview::GetDevices().size()) {
            shape.device = -1;
        }
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
        _extraViewportPreview = Json::object();

        Json content;
        if (!ReadJsonObject(_directory / ToStdString(kPreferencesFileName), content, _preferencesUnreadable)) {
            return;
        }
        _extraPreferences = UnknownMembers(content, {kVersionKey, kFontScaleKey, kViewportGridKey, kViewportGridStepKey, kViewportPreviewKey});

        EditorPreferences preferences;
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
        if (content.contains(ToStdString(kViewportPreviewKey))) {
            const Json* preview = FindJsonMember(content, kViewportPreviewKey);
            if (preview != nullptr && preview->is_object()) {
                ReadViewportPreview(*preview, preferences.viewportPreview, _extraViewportPreview);
            } else {
                WarnWrongType(kPreferencesFileName, kViewportPreviewKey, "an object");
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
        content[ToStdString(kFontScaleKey)] = _preferences.fontScale;
        content[ToStdString(kViewportGridKey)] = _preferences.viewportGrid;
        content[ToStdString(kViewportGridStepKey)] = _preferences.viewportGridStep;
        content[ToStdString(kViewportPreviewKey)] = ViewportPreviewToJson(_preferences.viewportPreview, _extraViewportPreview);
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
