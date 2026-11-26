#include "RemoteSessionRegistry.h"

#include <Widgets/RemoteEditor/RemoteSession.h>

namespace BECore {

    RemoteSessionHandle::~RemoteSessionHandle() {
        Reset();
    }

    RemoteSessionHandle::RemoteSessionHandle(RemoteSessionHandle&& other) noexcept : _token(other._token) {
        other._token = 0;
    }

    RemoteSessionHandle& RemoteSessionHandle::operator=(RemoteSessionHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            _token = other._token;
            other._token = 0;
        }
        return *this;
    }

    void RemoteSessionHandle::Reset() {
        if (_token != 0) {
            // The registry may be gone already (a test destroys it); there is then nothing to unregister
            if (RemoteSessionRegistry::IsInstanceCreated()) {
                RemoteSessionRegistry::GetInstance().Unregister(_token);
            }
            _token = 0;
        }
    }

    RemoteSessionHandle RemoteSessionRegistry::Register(RemoteSession& session) {
        if (_session != nullptr && _session != &session) {
            LOG_WARNING("[RemoteSession] a session is registered again, the new one replaces the old one");
        }
        _session = &session;
        _token = _nextToken++;
        return RemoteSessionHandle(_token);
    }

    RemoteSession* RemoteSessionRegistry::GetRemote() const {
        return _session != nullptr && _session->GetState() != RemoteSessionState::Detached ? _session : nullptr;
    }

    bool RemoteSessionRegistry::TakeStartAttach() {
        const bool first = !_startAttachTaken;
        _startAttachTaken = true;
        return first;
    }

    void RemoteSessionRegistry::Unregister(uint64_t token) {
        if (token == _token) {
            _session = nullptr;
            _token = 0;
        }
    }

}  // namespace BECore
