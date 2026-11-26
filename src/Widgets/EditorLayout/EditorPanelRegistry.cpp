#include "EditorPanelRegistry.h"

namespace BECore {

    EditorPanelHandle::~EditorPanelHandle() {
        Reset();
    }

    EditorPanelHandle::EditorPanelHandle(EditorPanelHandle&& other) noexcept : _token(other._token) {
        other._token = 0;
    }

    EditorPanelHandle& EditorPanelHandle::operator=(EditorPanelHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            _token = other._token;
            other._token = 0;
        }
        return *this;
    }

    void EditorPanelHandle::Reset() {
        if (_token != 0) {
            // The registry may be gone already (a test destroys it); there is then nothing to unregister
            if (EditorPanelRegistry::IsInstanceCreated()) {
                EditorPanelRegistry::GetInstance().Unregister(_token);
            }
            _token = 0;
        }
    }

    EditorPanelHandle EditorPanelRegistry::Register(PoolString id, IEditorPanel& panel) {
        const uint64_t token = _nextToken++;
        _entries.push_back(Entry{id, &panel, token});
        return EditorPanelHandle(token);
    }

    IEditorPanel* EditorPanelRegistry::Find(PoolString id) const {
        for (const Entry& entry : _entries) {
            if (entry.id == id) {
                return entry.panel;
            }
        }
        return nullptr;
    }

    void EditorPanelRegistry::Unregister(uint64_t token) {
        _entries.erase(eastl::remove_if(_entries.begin(), _entries.end(), [token](const Entry& entry) { return entry.token == token; }), _entries.end());
    }

}  // namespace BECore
