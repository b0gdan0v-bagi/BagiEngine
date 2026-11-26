#include "ISceneEditorHost.h"

namespace BECore {

    SceneEditorHostHandle::~SceneEditorHostHandle() {
        Reset();
    }

    SceneEditorHostHandle::SceneEditorHostHandle(SceneEditorHostHandle&& other) noexcept : _token(other._token) {
        other._token = 0;
    }

    SceneEditorHostHandle& SceneEditorHostHandle::operator=(SceneEditorHostHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            _token = other._token;
            other._token = 0;
        }
        return *this;
    }

    void SceneEditorHostHandle::Reset() {
        if (_token != 0) {
            // The registry may be gone already (a test destroys it); there is then nothing to unregister
            if (SceneEditorHostRegistry::IsInstanceCreated()) {
                SceneEditorHostRegistry::GetInstance().Unregister(_token);
            }
            _token = 0;
        }
    }

    SceneEditorHostHandle SceneEditorHostRegistry::Register(ISceneEditorHost& host) {
        if (_host != nullptr && _host != &host) {
            LOG_WARNING("[SceneEditorHost] a host is registered again, the new one replaces the old one");
        }
        _host = &host;
        _token = _nextToken++;
        return SceneEditorHostHandle(_token);
    }

    void SceneEditorHostRegistry::Unregister(uint64_t token) {
        if (token == _token) {
            _host = nullptr;
            _token = 0;
        }
    }

}  // namespace BECore
