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
            if (StringView(_view.status) != kNoSessionStatus) {
                _link.Reset();
                _view.status.assign(kNoSessionStatus.data(), kNoSessionStatus.size());
                _view.troubled = true;
                _view.canAttach = false;
                _view.canDetach = false;
            }
            return;
        }
        if (!_link.Update(*session)) {
            return;
        }
        _view.status = _link.GetText();
        _view.troubled = _link.IsTroubled();
        _view.canAttach = true;
        _view.canDetach = session->GetState() != RemoteSessionState::Detached;
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

}  // namespace BECore
