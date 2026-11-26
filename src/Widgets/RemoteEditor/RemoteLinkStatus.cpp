#include "RemoteLinkStatus.h"

namespace BECore {

    bool RemoteLinkStatus::Update(const RemoteSession& session) {
        if (_state == session.GetState() && _stalled == session.IsStalled() && StringView(_address) == session.GetAddress() && StringView(_reason) == session.GetLossReason()) {
            return false;
        }
        _state = session.GetState();
        _stalled = session.IsStalled();
        _address = String(session.GetAddress());
        _reason = String(session.GetLossReason());
        switch (*_state) {
            case RemoteSessionState::Detached:
                _text = "Not attached";
                break;
            case RemoteSessionState::Connecting:
                _text = Format("Connecting to {}", _address);
                break;
            case RemoteSessionState::Attached:
                _text = Format("Attached to {}", _address);
                break;
            case RemoteSessionState::Lost:
                _text = Format("Lost the link to {}: {}", _address, _reason);
                break;
        }
        if (_stalled) {
            _text += " (the program does not answer)";
        }
        _troubled = _stalled || *_state == RemoteSessionState::Lost;
        return true;
    }

}  // namespace BECore
