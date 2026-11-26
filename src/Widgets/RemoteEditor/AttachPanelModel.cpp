#include "AttachPanelModel.h"

#include <Generated/AttachPanelModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/RemoteEditor/RemoteSessionRegistry.h>

namespace BECore {

    namespace {

        constexpr StringView kNoSessionStatus = "This editor cannot attach: its widget list has no RemoteSessionWidget";

    }  // namespace

    TypedRef AttachPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void AttachPanelModel::Update() {
        const RemoteSession* session = RemoteSessionRegistry::GetInstance().Get();
        if (session == nullptr) {
            if (_shownState || _view.status.empty()) {
                _shownState.reset();
                _view.status.assign(kNoSessionStatus.data(), kNoSessionStatus.size());
                _view.troubled = true;
                _view.canAttach = false;
                _view.canDetach = false;
            }
            return;
        }
        if (_shownState == session->GetState() && _shownStalled == session->IsStalled() && StringView(_shownAddress) == session->GetAddress() && StringView(_shownReason) == session->GetLossReason()) {
            return;
        }
        Describe(*session);
    }

    bool AttachPanelModel::RunCommand(StringView id, StringView /*argument*/) {
        if (id == EditorCommandIds::kRemoteAttach.View()) {
            Attach();
            return true;
        }
        if (id == EditorCommandIds::kRemoteDetach.View()) {
            if (RemoteSession* session = RemoteSessionRegistry::GetInstance().Get()) {
                session->Detach();
            }
            SetError({});
            return true;
        }
        return false;
    }

    void AttachPanelModel::Attach() {
        RemoteSession* session = RemoteSessionRegistry::GetInstance().Get();
        if (session == nullptr) {
            return;
        }
        const auto attached = session->Attach(_view.address);
        SetError(attached ? StringView{} : StringView(attached.error()));
    }

    void AttachPanelModel::SetError(StringView error) {
        _view.error.assign(error.data(), error.size());
        _view.hasError = !error.empty();
    }

    void AttachPanelModel::Describe(const RemoteSession& session) {
        _shownState = session.GetState();
        _shownStalled = session.IsStalled();
        _shownAddress = String(session.GetAddress());
        _shownReason = String(session.GetLossReason());
        switch (*_shownState) {
            case RemoteSessionState::Detached:
                _view.status = "Not attached";
                break;
            case RemoteSessionState::Connecting:
                _view.status = Format("Connecting to {}", _shownAddress);
                break;
            case RemoteSessionState::Attached:
                _view.status = Format("Attached to {}", _shownAddress);
                break;
            case RemoteSessionState::Lost:
                _view.status = Format("Lost the link to {}: {}", _shownAddress, _shownReason);
                break;
        }
        if (_shownStalled) {
            _view.status += " (the program does not answer)";
        }
        _view.troubled = _shownStalled || *_shownState == RemoteSessionState::Lost;
        _view.canAttach = true;
        _view.canDetach = *_shownState != RemoteSessionState::Detached;
    }

}  // namespace BECore
