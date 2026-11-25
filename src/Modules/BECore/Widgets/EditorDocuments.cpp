#include "EditorDocuments.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore {

    EditorDocumentsHandle::~EditorDocumentsHandle() {
        Reset();
    }

    EditorDocumentsHandle::EditorDocumentsHandle(EditorDocumentsHandle&& other) noexcept : _owner(other._owner), _token(other._token) {
        other._owner = nullptr;
        other._token = 0;
    }

    EditorDocumentsHandle& EditorDocumentsHandle::operator=(EditorDocumentsHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            _owner = other._owner;
            _token = other._token;
            other._owner = nullptr;
            other._token = 0;
        }
        return *this;
    }

    void EditorDocumentsHandle::Reset() {
        if (_owner != nullptr) {
            _owner->Remove(_token);
            _owner = nullptr;
            _token = 0;
        }
    }

    EditorDocumentsHandle EditorDocuments::Add(Source source) {
        const uint64_t token = _nextToken++;
        _sources.push_back(Entry{token, std::move(source)});
        return EditorDocumentsHandle(this, token);
    }

    Vector<EditorDocumentState> EditorDocuments::Collect() const {
        Vector<EditorDocumentState> all;
        Vector<EditorDocumentState> fromSource;
        for (const Entry& entry : _sources) {
            fromSource.clear();
            entry.source(fromSource);
            for (EditorDocumentState& state : fromSource) {
                const bool known = eastl::any_of(all.begin(), all.end(), [&state](const EditorDocumentState& other) { return other.id == state.id; });
                if (!known) {
                    all.push_back(std::move(state));
                }
            }
        }
        eastl::sort(all.begin(), all.end(), [](const EditorDocumentState& a, const EditorDocumentState& b) { return a.id < b.id; });
        return all;
    }

    void EditorDocuments::Remove(uint64_t token) {
        const auto found = eastl::find_if(_sources.begin(), _sources.end(), [token](const Entry& entry) { return entry.token == token; });
        if (found != _sources.end()) {
            _sources.erase(found);
        }
    }

}  // namespace BECore
