#include "EditorSettings.h"

#include <BECore/Control/ControlParams.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/File/File.h>
#include <SDL3/SDL_filesystem.h>

namespace BECore {

    namespace {

        constexpr int kFileVersion = 1;
        constexpr const char* kEditorOrganization = "BagiEngine";
        constexpr const char* kEditorApplication = "Editor";
        constexpr int kJsonIndent = 2;
        constexpr StringView kBadFileSuffix = ".bad";

        constexpr StringView kVersionKey = "version";
        constexpr StringView kFontScaleKey = "fontScale";
        constexpr StringView kViewportGridKey = "viewportGrid";
        constexpr StringView kViewportGridStepKey = "viewportGridStep";
        constexpr StringView kBindingsKey = "bindings";
        constexpr StringView kViewportPreviewKey = "viewportPreview";
        constexpr StringView kViewportInputModeKey = "viewportInputMode";
        constexpr StringView kInputModePlayName = "play";
        constexpr StringView kInputModeEditName = "edit";
        constexpr StringView kExternalEditorKey = "externalEditor";
        constexpr StringView kConsoleHistoryKey = "consoleHistory";
        constexpr unsigned char kUtf8ContinuationMask = 0xC0;
        constexpr unsigned char kUtf8ContinuationBits = 0x80;

        // The members of the "viewportPreview" object
        constexpr StringView kPreviewAspectKey = "aspect";
        constexpr StringView kPreviewDeviceKey = "device";
        constexpr StringView kPreviewCustomKey = "custom";
        constexpr StringView kPreviewCustomWidthKey = "customWidth";
        constexpr StringView kPreviewCustomHeightKey = "customHeight";
        constexpr StringView kPreviewRotatedKey = "rotated";
        constexpr StringView kPreviewSafeAreaKey = "safeArea";
        constexpr StringView kOverlaySafeAreaKey = "overlaySafeArea";
        constexpr StringView kOverlayViewBoundsKey = "overlayViewBounds";
        constexpr StringView kOverlayCanvasBoundsKey = "overlayCanvasBounds";
        constexpr StringView kOverlayTouchTargetsKey = "overlayTouchTargets";
        constexpr StringView kOverlayRegionsKey = "overlayRegions";
        constexpr StringView kOverlayElementRectsKey = "overlayElementRects";
        constexpr StringView kOverlayAnchorsKey = "overlayAnchors";
        constexpr StringView kOverlayDesignRectKey = "overlayDesignRect";
        constexpr StringView kOverlayFindingsKey = "overlayFindings";

        // A UTF-8 byte order mark that Notepad writes: the tape refuses it, and a file the user edited by hand must not be moved to `.bad` for it
        constexpr StringView kUtf8Bom = "\xEF\xBB\xBF";

        // Reads the file at @p path into @p text and tapes it into @p tape (the tape borrows @p text, so both stay with the caller). Missing file:
        // false, no warning. A file that cannot be read or parsed, or whose root is not an object: false, one warning, @p unreadable set.
        bool ReadJsonObject(const Path& path, String& text, JsonTape& tape, bool& unreadable) {
            unreadable = false;
            if (!GetFileInfo(path)) {
                return false;
            }
            unreadable = true;
            std::expected<String, String> read = ReadTextFile(path);
            if (!read) {
                LOG_WARNING("Editor settings: {}; using the defaults", read.error());
                return false;
            }
            text = std::move(*read);
            StringView body(text.data(), text.size());
            if (body.starts_with(kUtf8Bom)) {
                body.remove_prefix(kUtf8Bom.size());
            }
            if (!tape.Parse(body) || !JsonView(tape, 0).IsObject()) {
                LOG_WARNING("Editor settings: '{}' is not a JSON object; using the defaults", path.View());
                return false;
            }
            unreadable = false;
            return true;
        }

        void WarnWrongType(StringView file, StringView key, StringView expected) {
            LOG_WARNING("Editor settings: '{}' in {} is not {}; using the default", key, file, expected);
        }

        // The index of the device called @p name in the table of this build, -1 when there is none
        int32_t FindDeviceByName(StringView name) {
            const auto devices = UIPreview::GetDevices();
            for (size_t i = 0; i < devices.size(); ++i) {
                if (devices[i].name == name) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        // A bool member of @p object: left in @p value when absent, a warning and left when of another type
        void ReadBool(JsonView object, StringView key, bool& value) {
            if (!FindParam(object, key)) {
                return;
            }
            if (const Optional<bool> read = GetParamBool(object, key)) {
                value = *read;
            } else {
                WarnWrongType(kPreferencesFileName, key, "true or false");
            }
        }

        // A side of the custom size: clamped into its range like the grid step
        void ReadSide(JsonView object, StringView key, uint32_t& value) {
            if (!FindParam(object, key)) {
                return;
            }
            if (const Optional<int64_t> read = GetParamInt(object, key)) {
                value = static_cast<uint32_t>(std::clamp<int64_t>(*read, kMinPreviewSide, kMaxPreviewSide));
            } else {
                WarnWrongType(kPreferencesFileName, key, "an integer");
            }
        }

        // An enum member of @p object by name
        template <typename TEnum>
        void ReadEnum(JsonView object, StringView key, TEnum& value) {
            if (!FindParam(object, key)) {
                return;
            }
            const Optional<String> name = GetParamString(object, key);
            const Optional<TEnum> parsed = name ? EnumUtils<TEnum>::Cast(StringView(name->data(), name->size())) : std::nullopt;
            if (parsed) {
                value = *parsed;
            } else {
                WarnWrongType(kPreferencesFileName, key, "a known name");
            }
        }

        // Reads the "viewportPreview" object into @p preview and gives the members it does not know to @p extra
        void ReadViewportPreview(JsonView object, PreviewSettings& preview, JsonMemberMap& extra) {
            extra.Assign(object, {kPreviewAspectKey, kPreviewDeviceKey, kPreviewCustomKey, kPreviewCustomWidthKey, kPreviewCustomHeightKey, kPreviewRotatedKey, kPreviewSafeAreaKey,
                                  kOverlaySafeAreaKey, kOverlayViewBoundsKey, kOverlayCanvasBoundsKey, kOverlayTouchTargetsKey, kOverlayRegionsKey, kOverlayElementRectsKey, kOverlayAnchorsKey,
                                  kOverlayDesignRectKey, kOverlayFindingsKey});
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
            ReadBool(object, kOverlayElementRectsKey, preview.overlays.elementRects);
            ReadBool(object, kOverlayAnchorsKey, preview.overlays.anchors);
            ReadBool(object, kOverlayDesignRectKey, preview.overlays.designRect);
            ReadBool(object, kOverlayFindingsKey, preview.overlays.findings);

            // The device is stored by name, so a table that changed between builds never gives another device
            if (FindParam(object, kPreviewDeviceKey)) {
                const Optional<String> name = GetParamString(object, kPreviewDeviceKey);
                if (!name) {
                    WarnWrongType(kPreferencesFileName, kPreviewDeviceKey, "a device name");
                } else if (!name->empty()) {
                    shape.device = FindDeviceByName(StringView(name->data(), name->size()));
                    if (shape.device < 0) {
                        LOG_WARNING("Editor settings: the preview device '{}' in {} is not known to this build; using the Free shape", *name, kPreferencesFileName);
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

        // The input mode as it is written in the file: a lower-case word, so the file reads like a setting and not like a C++ name
        StringView InputModeName(ViewportInputMode mode) {
            return mode == ViewportInputMode::Edit ? kInputModeEditName : kInputModePlayName;
        }

        // The input mode of the file: left in @p mode when absent, a warning and left when the text is not one of the two names
        void ReadInputMode(JsonView object, StringView key, ViewportInputMode& mode) {
            if (!FindParam(object, key)) {
                return;
            }
            const Optional<String> name = GetParamString(object, key);
            const StringView text = name ? StringView(name->data(), name->size()) : StringView();
            if (text == kInputModePlayName) {
                mode = ViewportInputMode::Play;
            } else if (text == kInputModeEditName) {
                mode = ViewportInputMode::Edit;
            } else {
                WarnWrongType(kPreferencesFileName, key, "play or edit");
            }
        }

        // The string member @p key: left in @p value when absent, a warning and left when of another type
        void ReadString(JsonView object, StringView key, String& value) {
            if (!FindParam(object, key)) {
                return;
            }
            if (Optional<String> read = GetParamString(object, key)) {
                value = std::move(*read);
            } else {
                WarnWrongType(kPreferencesFileName, key, "a text");
            }
        }

        // The array of strings @p key: left in @p value when absent, a warning and left when it is not an array or holds a non-string
        void ReadStringArray(JsonView object, StringView key, Vector<String>& value) {
            const Optional<JsonView> array = FindParam(object, key);
            if (!array) {
                return;
            }
            bool onlyStrings = array->IsArray();
            for (const JsonView item : array->Items()) {
                onlyStrings = onlyStrings && item.IsString();
            }
            if (!onlyStrings) {
                WarnWrongType(kPreferencesFileName, key, "a list of texts");
                return;
            }
            value.clear();
            for (const JsonView item : array->Items()) {
                String text;
                item.ReadText(text);
                value.push_back(std::move(text));
            }
        }

        // Shortens @p text to at most @p limit bytes without cutting a UTF-8 sequence in the middle
        void CutToBytes(String& text, size_t limit) {
            if (text.size() <= limit) {
                return;
            }
            size_t end = limit;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & kUtf8ContinuationMask) == kUtf8ContinuationBits) {
                --end;
            }
            text.resize(end);
        }

        template <typename T>
        void SetValue(JsonMemberMap& map, StringView key, T value) {
            map.SetWith(key, [&](JsonWriter& writer) { writer.Value(value); });
        }

        void SetStrings(JsonMemberMap& map, StringView key, const Vector<String>& strings) {
            map.SetWith(key, [&](JsonWriter& writer) {
                writer.BeginArray();
                for (const String& text : strings) {
                    writer.Value(StringView(text.data(), text.size()));
                }
                writer.EndArray();
            });
        }

        // The "viewportPreview" object: the members this build does not know, then every member it writes
        JsonMemberMap ViewportPreviewMembers(const PreviewSettings& preview, const JsonMemberMap& extra) {
            JsonMemberMap object = extra;
            const PreviewShape& shape = preview.shape;
            const auto devices = UIPreview::GetDevices();
            const bool hasDevice = shape.device >= 0 && static_cast<size_t>(shape.device) < devices.size();
            SetValue(object, kPreviewAspectKey, EnumUtils<ViewportAspect>::ToString(shape.aspect));
            SetValue(object, kPreviewDeviceKey, hasDevice ? StringView(devices[static_cast<size_t>(shape.device)].name) : StringView());
            SetValue(object, kPreviewCustomKey, shape.custom);
            SetValue(object, kPreviewCustomWidthKey, shape.customWidth);
            SetValue(object, kPreviewCustomHeightKey, shape.customHeight);
            SetValue(object, kPreviewRotatedKey, shape.rotated);
            SetValue(object, kPreviewSafeAreaKey, EnumUtils<SafeAreaSimulation>::ToString(preview.safeArea));
            SetValue(object, kOverlaySafeAreaKey, preview.overlays.safeArea);
            SetValue(object, kOverlayViewBoundsKey, preview.overlays.viewBounds);
            SetValue(object, kOverlayCanvasBoundsKey, preview.overlays.canvasBounds);
            SetValue(object, kOverlayTouchTargetsKey, preview.overlays.touchTargets);
            SetValue(object, kOverlayRegionsKey, preview.overlays.regions);
            SetValue(object, kOverlayElementRectsKey, preview.overlays.elementRects);
            SetValue(object, kOverlayAnchorsKey, preview.overlays.anchors);
            SetValue(object, kOverlayDesignRectKey, preview.overlays.designRect);
            SetValue(object, kOverlayFindingsKey, preview.overlays.findings);
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
        CutToBytes(preferences.externalEditor, kMaxExternalEditorLength);
        Vector<String>& history = preferences.consoleHistory;
        history.erase(eastl::remove_if(history.begin(), history.end(), [](const String& command) { return command.empty() || command.size() > kMaxConsoleCommandLength; }), history.end());
        if (history.size() > kMaxConsoleHistory) {
            history.erase(history.begin(), history.end() - static_cast<ptrdiff_t>(kMaxConsoleHistory));
        }
        return preferences;
    }

    void AddConsoleHistory(EditorPreferences& preferences, StringView command) {
        if (command.empty() || command.size() > kMaxConsoleCommandLength) {
            return;
        }
        Vector<String>& history = preferences.consoleHistory;
        history.erase(eastl::remove_if(history.begin(), history.end(), [command](const String& entry) { return StringView(entry.data(), entry.size()) == command; }), history.end());
        history.push_back(String(command.data(), command.size()));
        if (history.size() > kMaxConsoleHistory) {
            history.erase(history.begin(), history.end() - static_cast<ptrdiff_t>(kMaxConsoleHistory));
        }
    }

    Path EditorUserDirectory() {
        if (const char* overridden = std::getenv(kEditorDirectoryEnvironmentVariable); overridden != nullptr && overridden[0] != '\0') {
            return Path(overridden);
        }
        char* prefPath = SDL_GetPrefPath(kEditorOrganization, kEditorApplication);
        if (prefPath == nullptr) {
            LOG_WARNING("Editor settings: no per-user folder: {}", SDL_GetError());
            return {};
        }
        Path directory(prefPath);
        SDL_free(prefPath);
        return directory;
    }

    EditorSettings::EditorSettings(Path directory) : _directory(std::move(directory)) {}

    void EditorSettings::SetPreferences(const EditorPreferences& preferences) {
        _preferences = ClampPreferences(preferences);
    }

    void EditorSettings::Load() {
        LoadPreferences();
        LoadHotkeys();
    }

    void EditorSettings::LoadPreferences() {
        _preferences = EditorPreferences{};
        _extraPreferences.Clear();
        _extraViewportPreview.Clear();

        String fileText;
        JsonTape tape;
        if (!ReadJsonObject(_directory / kPreferencesFileName, fileText, tape, _preferencesUnreadable)) {
            return;
        }
        const JsonView content(tape, 0);
        _extraPreferences.Assign(content, {kVersionKey, kFontScaleKey, kViewportGridKey, kViewportGridStepKey, kViewportPreviewKey, kViewportInputModeKey, kExternalEditorKey, kConsoleHistoryKey});

        EditorPreferences preferences;
        if (FindParam(content, kFontScaleKey)) {
            if (const Optional<double> scale = GetParamNumber(content, kFontScaleKey)) {
                preferences.fontScale = static_cast<float>(*scale);
            } else {
                WarnWrongType(kPreferencesFileName, kFontScaleKey, "a number");
            }
        }
        ReadBool(content, kViewportGridKey, preferences.viewportGrid);
        if (FindParam(content, kViewportGridStepKey)) {
            if (const Optional<int64_t> step = GetParamInt(content, kViewportGridStepKey)) {
                preferences.viewportGridStep = static_cast<int32_t>(std::clamp<int64_t>(*step, kMinViewportGridStep, kMaxViewportGridStep));
            } else {
                WarnWrongType(kPreferencesFileName, kViewportGridStepKey, "an integer");
            }
        }
        if (const Optional<JsonView> preview = FindParam(content, kViewportPreviewKey)) {
            if (preview->IsObject()) {
                ReadViewportPreview(*preview, preferences.viewportPreview, _extraViewportPreview);
            } else {
                WarnWrongType(kPreferencesFileName, kViewportPreviewKey, "an object");
            }
        }
        ReadInputMode(content, kViewportInputModeKey, preferences.viewportInputMode);
        ReadString(content, kExternalEditorKey, preferences.externalEditor);
        ReadStringArray(content, kConsoleHistoryKey, preferences.consoleHistory);
        _preferences = ClampPreferences(preferences);
    }

    void EditorSettings::LoadHotkeys() {
        _hotkeys = HotkeyMap();
        _extraHotkeys.Clear();
        _unknownBindings.Clear();

        String fileText;
        JsonTape tape;
        if (!ReadJsonObject(_directory / kHotkeysFileName, fileText, tape, _hotkeysUnreadable)) {
            return;
        }
        const JsonView content(tape, 0);
        _extraHotkeys.Assign(content, {kVersionKey, kBindingsKey});

        const Optional<JsonView> bindings = FindParam(content, kBindingsKey);
        if (!bindings) {
            return;
        }
        if (!bindings->IsObject()) {
            WarnWrongType(kHotkeysFileName, kBindingsKey, "an object");
            return;
        }

        struct Override {
            PoolString id;
            Optional<KeyChord> chord;
        };
        Vector<Override> overrides;
        String id;
        String text;
        for (const JsonMember member : bindings->Members()) {
            member.key.ReadText(id);
            // Of two equal keys the first counts, as for every read of the tape
            if (bindings->Find(StringView(id.data(), id.size()))->Index() != member.value.Index()) {
                continue;
            }
            const PoolString commandId = PoolString::Intern(StringView(id));
            if (!member.value.IsString()) {
                LOG_WARNING("Editor settings: the hotkey of '{}' is not a text; dropped", id);
                continue;
            }
            member.value.ReadText(text);
            const EditorCommand* command = FindEditorCommand(commandId);
            if (command == nullptr) {
                LOG_WARNING("Editor settings: unknown command '{}' in {}; not applied, kept in the file", id, kHotkeysFileName);
                _unknownBindings.Set(StringView(id.data(), id.size()), member.value.Raw());
                continue;
            }
            if (!command->rebindable) {
                LOG_WARNING("Editor settings: the hotkey of '{}' is fixed; dropped", id);
                continue;
            }
            if (text.empty()) {
                overrides.push_back({commandId, std::nullopt});
                continue;
            }
            const Optional<KeyChord> chord = ParseKeyChord(StringView(text.data(), text.size()));
            if (!chord) {
                LOG_WARNING("Editor settings: '{}' is not a chord ({}); dropped", text, id);
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
            const String chordText = ToString(*entry.chord);
            if (result.status == AssignStatus::Conflict) {
                LOG_WARNING("Editor settings: {} for '{}' is already used by '{}'; dropped", chordText, entry.id.ToStringView(), result.otherCommandId.ToStringView());
            } else {
                LOG_WARNING("Editor settings: {} is not allowed for '{}'; dropped", chordText, entry.id.ToStringView());
            }
            _hotkeys.ResetOne(entry.id);
        }
    }

    bool EditorSettings::Save(const Path& path, const JsonMemberMap& content, bool& unreadable) {
        _lastSaveError.clear();
        if (_directory.Empty()) {
            _lastSaveError = "There is no folder for the editor settings";
            LOG_ERROR("Editor settings: {}", _lastSaveError);
            return false;
        }

        // A file that did not parse is moved aside once, so the user's old text is not lost to the first change
        if (unreadable) {
            const Path backup(Format("{}{}", path.View(), kBadFileSuffix));
            if (GetFileInfo(path) && RenameFile(path, backup)) {
                LOG_WARNING("Editor settings: the unreadable '{}' was kept as '{}'", path.View(), backup.View());
            }
            unreadable = false;
        }

        String text;
        JsonWriter writer(text, kJsonIndent);
        content.Write(writer);
        text.push_back('\n');
        const std::expected<void, String> written = WriteFileAtomically(path, text);
        if (!written) {
            _lastSaveError = written.error();
            LOG_ERROR("Editor settings: {}", _lastSaveError);
            return false;
        }
        return true;
    }

    bool EditorSettings::SavePreferences() {
        JsonMemberMap content = _extraPreferences;
        SetValue(content, kVersionKey, kFileVersion);
        // The double of the float, the number the file has always held: 1.1f is 1.100000023841858 and reads back as the same float
        SetValue(content, kFontScaleKey, static_cast<double>(_preferences.fontScale));
        SetValue(content, kViewportGridKey, _preferences.viewportGrid);
        SetValue(content, kViewportGridStepKey, _preferences.viewportGridStep);
        const JsonMemberMap preview = ViewportPreviewMembers(_preferences.viewportPreview, _extraViewportPreview);
        content.SetWith(kViewportPreviewKey, [&](JsonWriter& writer) { preview.Write(writer); });
        SetValue(content, kViewportInputModeKey, InputModeName(_preferences.viewportInputMode));
        SetValue(content, kExternalEditorKey, StringView(_preferences.externalEditor.data(), _preferences.externalEditor.size()));
        SetStrings(content, kConsoleHistoryKey, _preferences.consoleHistory);
        return Save(_directory / kPreferencesFileName, content, _preferencesUnreadable);
    }

    bool EditorSettings::SaveHotkeys() {
        JsonMemberMap bindings = _unknownBindings;
        for (const EditorCommand& command : GetEditorCommands()) {
            if (!command.rebindable || _hotkeys.IsDefault(command.id)) {
                continue;
            }
            const Optional<KeyChord> chord = _hotkeys.GetChord(command.id);
            const String text = chord ? ToString(*chord) : String();
            SetValue(bindings, command.id.ToStringView(), StringView(text.data(), text.size()));
        }
        JsonMemberMap content = _extraHotkeys;
        SetValue(content, kVersionKey, kFileVersion);
        content.SetWith(kBindingsKey, [&](JsonWriter& writer) { bindings.Write(writer); });
        return Save(_directory / kHotkeysFileName, content, _hotkeysUnreadable);
    }

}  // namespace BECore
