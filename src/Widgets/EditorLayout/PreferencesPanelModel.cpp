#include "PreferencesPanelModel.h"

#include <Generated/PreferencesPanelModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorHost.h>
#include <Widgets/EditorLayout/IEditorLayoutHost.h>
#include <imgui.h>

namespace BECore {

    namespace {

        /// The keys a chord can name, read in the frame the user presses one
        Optional<KeyChord> ReadImGuiChord() {
            for (int index = ImGuiKey_NamedKey_BEGIN; index < ImGuiKey_NamedKey_END; ++index) {
                const ImGuiKey key = static_cast<ImGuiKey>(index);
                if (!IsNamedKey(key) || !ImGui::IsKeyPressed(key, false)) {
                    continue;
                }
                const ImGuiIO& io = ImGui::GetIO();
                uint8_t mods = 0;
                mods |= io.KeyCtrl ? kChordModCtrl : 0;
                mods |= io.KeyAlt ? kChordModAlt : 0;
                mods |= io.KeyShift ? kChordModShift : 0;
                return KeyChord{key, mods};
            }
            return std::nullopt;
        }

    }  // namespace

    PreferencesPanelModel::PreferencesPanelModel() : _readChord(ReadImGuiChord) {
        _view.externalEditorSupported = GetEditorHost() == EditorHostKind::Desktop;
    }

    void PreferencesPanelModel::SetChordReader(ChordReader reader) {
        _readChord = std::move(reader);
    }

    TypedRef PreferencesPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    IEditorLayoutHost* PreferencesPanelModel::FindHost() {
        IEditorLayoutHost* host = EditorLayoutHostRegistry::GetInstance().Get();
        const bool usable = host != nullptr && host->GetSettings() != nullptr;
        _view.noFolder = !usable;
        if (!usable) {
            _loaded = false;
            return nullptr;
        }
        return host;
    }

    void PreferencesPanelModel::OnOpenChanged(bool open) {
        _loaded = false;
        _hotkeys.Reset();
        _skipCapture = false;
        _view.error.clear();
        _view.message.clear();
        _view.hasError = false;
        if (open) {
            if (IEditorLayoutHost* host = FindHost()) {
                LoadFromSettings(host->GetSettings()->GetPreferences());
                LoadHotkeys(*host->GetSettings());
                return;
            }
        }
        SyncHotkeyView();
    }

    void PreferencesPanelModel::LoadFromSettings(const EditorPreferences& preferences) {
        _view.fontScale = preferences.fontScale;
        _view.viewportGrid = preferences.viewportGrid;
        _view.viewportGridStep = preferences.viewportGridStep;
        _view.externalEditor = preferences.externalEditor;
        _applied = {_view.fontScale, _view.viewportGrid, _view.viewportGridStep, _view.externalEditor};
        _loaded = true;
    }

    void PreferencesPanelModel::Update() {
        IEditorLayoutHost* host = FindHost();
        if (host == nullptr) {
            return;
        }
        if (!_loaded) {
            LoadFromSettings(host->GetSettings()->GetPreferences());
            LoadHotkeys(*host->GetSettings());
            return;
        }
        ApplyView(*host);
        UpdateCapture(*host->GetSettings());
    }

    void PreferencesPanelModel::LoadHotkeys(EditorSettings& settings) {
        _hotkeys.Rebuild(settings.GetHotkeys());
        SyncHotkeyView();
    }

    void PreferencesPanelModel::SyncHotkeyView() {
        _view.hotkeyRows = _hotkeys.GetRows();
        _view.rebindOpen = _hotkeys.GetStage() != RebindStage::Closed;
        _view.inConflict = _hotkeys.GetStage() == RebindStage::Conflict;
        _view.rebindTitle = _hotkeys.GetTitle();
        _view.rebindText = _hotkeys.GetPrompt();
        _view.refusal = _hotkeys.GetRefusal();
        _view.hasRefusal = !_view.refusal.empty();
        _view.conflictText = _hotkeys.GetConflictText();
        _view.hotkeysMessage = _hotkeys.GetMessage();
        _view.hasHotkeysMessage = !_view.hotkeysMessage.empty();
    }

    void PreferencesPanelModel::UpdateCapture(EditorSettings& settings) {
        if (_hotkeys.GetStage() != RebindStage::Capturing) {
            return;
        }
        if (_skipCapture) {
            _skipCapture = false;
            return;
        }
        const Optional<KeyChord> pressed = _readChord();
        if (!pressed) {
            return;
        }
        _hotkeys.Capture(settings, *pressed);
        SyncHotkeyView();
    }

    void PreferencesPanelModel::ApplyView(IEditorLayoutHost& host) {
        const bool gridChanged = _view.viewportGrid != _applied.viewportGrid;
        const bool changed = gridChanged || _view.fontScale != _applied.fontScale || _view.viewportGridStep != _applied.viewportGridStep || _view.externalEditor != _applied.externalEditor;
        if (!changed) {
            return;
        }

        EditorSettings& settings = *host.GetSettings();
        EditorPreferences preferences = settings.GetPreferences();
        preferences.fontScale = _view.fontScale;
        preferences.viewportGrid = _view.viewportGrid;
        preferences.viewportGridStep = _view.viewportGridStep;
        preferences.externalEditor = _view.externalEditor;
        settings.SetPreferences(preferences);
        host.NotifyPreferencesChanged();

        // The settings clamp what they take: show the value that holds
        LoadFromSettings(settings.GetPreferences());
        if (gridChanged) {
            Save(settings);
        }
    }

    void PreferencesPanelModel::Save(EditorSettings& settings) {
        if (settings.SavePreferences()) {
            _view.error.clear();
            _view.message.clear();
            _view.hasError = false;
            return;
        }
        _view.error = settings.GetLastSaveError();
        _view.message = Format("Not saved: {}", _view.error);
        _view.hasError = true;
    }

    bool PreferencesPanelModel::RunCommand(StringView id, StringView argument) {
        IEditorLayoutHost* host = FindHost();
        EditorSettings* settings = host != nullptr ? host->GetSettings() : nullptr;
        if (id == EditorCommandIds::kPreferencesCommit.View()) {
            // The release of a control can come in the frame of its last change, before the model's Update has read it
            if (host != nullptr && _loaded) {
                ApplyView(*host);
                Save(*settings);
            }
            return true;
        }
        return RunHotkeyCommand(id, argument, settings);
    }

    bool PreferencesPanelModel::RunHotkeyCommand(StringView id, StringView argument, EditorSettings* settings) {
        using namespace EditorCommandIds;
        const auto is = [id](const auto& command) { return id == command.View(); };
        const bool known = is(kPreferencesRebind) || is(kPreferencesClear) || is(kPreferencesReset) || is(kPreferencesResetAll) || is(kPreferencesSwap) || is(kPreferencesCancelRebind);
        if (!known) {
            return false;
        }
        if (settings == nullptr) {
            return true;
        }
        if (is(kPreferencesRebind)) {
            _skipCapture = _hotkeys.StartRebind(argument);
        } else if (is(kPreferencesClear)) {
            _hotkeys.Clear(*settings, argument);
        } else if (is(kPreferencesReset)) {
            _hotkeys.ResetCommand(*settings, argument);
        } else if (is(kPreferencesResetAll)) {
            _hotkeys.ResetAll(*settings);
        } else if (is(kPreferencesSwap)) {
            _hotkeys.Swap(*settings);
        } else {
            _hotkeys.Cancel();
        }
        SyncHotkeyView();
        return true;
    }

}  // namespace BECore
