#include "SceneEditorPanelModel.h"

#include <Generated/SceneEditorPanelModel.gen.hpp>
#include <Widgets/SceneEditor/ISceneEditorHost.h>

namespace BECore {

    void SceneEditorPanelModel::Initialize() {
        Subscribe<SceneNodeSelectedEvent, &SceneEditorPanelModel::OnNodeSelected>(this);
    }

    TypedRef SceneEditorPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void SceneEditorPanelModel::Update() {
        ISceneEditorHost* const host = SceneEditorHostRegistry::GetInstance().Get();
        if (host != nullptr) {
            ApplyEdits(*host);
        }
        Refresh(host);
    }

    bool SceneEditorPanelModel::RunCommand(StringView /*id*/, StringView /*argument*/) {
        return false;
    }

    void SceneEditorPanelModel::OnNodeSelected(const SceneNodeSelectedEvent& /*event*/) {
        // The host already holds the new node when it announces it; the view is overwritten before an edit typed for the old node is applied
        Refresh(SceneEditorHostRegistry::GetInstance().Get());
    }

    void SceneEditorPanelModel::ApplyEdits(ISceneEditorHost& host) {
        if (!_view.hasSelection) {
            return;
        }
        const StringView typedName(_view.name.data(), _view.name.size());
        if (typedName != StringView(_shownName)) {
            host.RenameInspected(typedName);
        }
        if (_view.layer != _shownLayer) {
            host.SetInspectedLayer(_view.layer);
        }
    }

    void SceneEditorPanelModel::Refresh(const ISceneEditorHost* host) {
        const SceneInspectedNode inspected = host != nullptr ? host->GetInspected() : SceneInspectedNode{};
        _view.dirty = inspected.dirty;
        _view.hasSelection = inspected.hasSelection;
        _view.locked = inspected.locked;
        _view.nameReadOnly = inspected.locked || inspected.nameReadOnly;
        _view.layerReadOnly = inspected.locked || inspected.layerReadOnly;
        _view.structureReadOnly = inspected.locked || inspected.structureReadOnly;
        _view.multiSelection = inspected.selectionCount > 1;
        _view.isInstance = !inspected.sourceRef.Empty();

        // Assigned whenever the host differs from the view, not only from `_shownName`: a refresh drops an edit the host did not take
        const StringView name = inspected.name;
        if (StringView(_view.name.data(), _view.name.size()) != name) {
            _view.name.assign(name.data(), name.size());
        }
        if (StringView(_shownName) != name) {
            _shownName.assign(name.data(), name.size());
        }
        _view.layer = inspected.layer;
        _shownLayer = inspected.layer;

        if (inspected.selectionCount != _shownCount) {
            _shownCount = inspected.selectionCount;
            _view.multiText.clear();
            if (_view.multiSelection) {
                AppendFormat(_view.multiText, "{} nodes selected; showing the last one", _shownCount);
            }
        }
        if (inspected.sourceRef != _shownSourceRef) {
            _shownSourceRef = inspected.sourceRef;
            _view.instanceText.clear();
            if (_view.isInstance) {
                AppendFormat(_view.instanceText, "Instance of {}", _shownSourceRef.ToStringView());
            }
        }
        if (StringView(_shownRefusalCode) != inspected.refusalCode || StringView(_shownRefusalMessage) != inspected.refusalMessage) {
            _shownRefusalCode.assign(inspected.refusalCode.data(), inspected.refusalCode.size());
            _shownRefusalMessage.assign(inspected.refusalMessage.data(), inspected.refusalMessage.size());
            _view.hasRefusal = !_shownRefusalCode.empty() || !_shownRefusalMessage.empty();
            _view.refusalText.clear();
            if (_view.hasRefusal) {
                AppendFormat(_view.refusalText, "Refused: {} ({})", _shownRefusalMessage, _shownRefusalCode);
            }
        }
    }

}  // namespace BECore
