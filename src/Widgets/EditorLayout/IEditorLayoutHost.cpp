#include "IEditorLayoutHost.h"

namespace BECore {

    EditorLayoutHostHandle::~EditorLayoutHostHandle() {
        Reset();
    }

    EditorLayoutHostHandle::EditorLayoutHostHandle(EditorLayoutHostHandle&& other) noexcept : _token(other._token) {
        other._token = 0;
    }

    EditorLayoutHostHandle& EditorLayoutHostHandle::operator=(EditorLayoutHostHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            _token = other._token;
            other._token = 0;
        }
        return *this;
    }

    void EditorLayoutHostHandle::Reset() {
        if (_token != 0) {
            // The registry may be gone already (a test destroys it); there is then nothing to unregister
            if (EditorLayoutHostRegistry::IsInstanceCreated()) {
                EditorLayoutHostRegistry::GetInstance().Unregister(_token);
            }
            _token = 0;
        }
    }

    EditorLayoutHostHandle EditorLayoutHostRegistry::Register(IEditorLayoutHost& host) {
        if (_host != nullptr && _host != &host) {
            LOG_WARNING("[EditorLayoutHost] a host is registered again, the new one replaces the old one");
        }
        _host = &host;
        _token = _nextToken++;
        return EditorLayoutHostHandle(_token);
    }

    void EditorLayoutHostRegistry::Unregister(uint64_t token) {
        if (token == _token) {
            _host = nullptr;
            _token = 0;
        }
    }

}  // namespace BECore
