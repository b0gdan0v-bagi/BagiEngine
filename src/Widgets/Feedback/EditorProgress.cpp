#include "EditorProgress.h"

#include <EASTL/algorithm.h>

namespace BECore {

    EditorProgressHandle::~EditorProgressHandle() {
        Reset();
    }

    EditorProgressHandle::EditorProgressHandle(EditorProgressHandle&& other) noexcept : _token(other._token) {
        other._token = 0;
    }

    EditorProgressHandle& EditorProgressHandle::operator=(EditorProgressHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            _token = other._token;
            other._token = 0;
        }
        return *this;
    }

    void EditorProgressHandle::Reset() {
        if (_token != 0) {
            // The registry may be gone already (a test destroys it); there is then nothing to unregister
            if (EditorProgressRegistry::IsInstanceCreated()) {
                EditorProgressRegistry::GetInstance().Unregister(_token);
            }
            _token = 0;
        }
    }

    EditorProgressHandle EditorProgressRegistry::Register(EditorProgressSource source) {
        const uint64_t token = _nextToken++;
        _entries.push_back({token, std::move(source)});
        return EditorProgressHandle(token);
    }

    EditorProgressSummary EditorProgressRegistry::Collect() {
        EditorProgressSummary summary;
        for (Entry& entry : _entries) {
            const Optional<EditorProgress> progress = entry.source();
            if (!progress) {
                continue;
            }
            if (summary.first) {
                ++summary.others;
            } else {
                summary.first = progress;
            }
        }
        return summary;
    }

    void EditorProgressRegistry::Unregister(uint64_t token) {
        const auto found = eastl::find_if(_entries.begin(), _entries.end(), [token](const Entry& entry) { return entry.token == token; });
        if (found != _entries.end()) {
            _entries.erase(found);
        }
    }

}  // namespace BECore
