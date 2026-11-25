#include "PreferencesPanelModel.h"

#include <Generated/PreferencesPanelModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/IEditorLayoutHost.h>

namespace BECore {

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
        if (!open) {
            return;
        }
        _view.error.clear();
        _view.message.clear();
        _view.hasError = false;
        if (IEditorLayoutHost* host = FindHost()) {
            LoadFromSettings(host->GetSettings()->GetPreferences());
        }
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
            return;
        }
        ApplyView(*host);
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

    bool PreferencesPanelModel::RunCommand(StringView id, StringView /*argument*/) {
        if (id != EditorCommandIds::kPreferencesCommit.View()) {
            return false;
        }
        // The release of a control can come in the frame of its last change, before the model's Update has read it
        IEditorLayoutHost* host = FindHost();
        if (host != nullptr && _loaded) {
            ApplyView(*host);
            Save(*host->GetSettings());
        }
        return true;
    }

}  // namespace BECore
