#pragma once

#include <Widgets/EditorLayout/EditorHost.h>

namespace BECore::Tests {

    /// Gives the editor another host for the life of the object and the previous one back after it
    class ScopedEditorHost {
    public:
        explicit ScopedEditorHost(EditorHostKind host) : _previous(GetEditorHost()) {
            SetEditorHost(host);
        }

        ~ScopedEditorHost() {
            SetEditorHost(_previous);
        }

        ScopedEditorHost(const ScopedEditorHost&) = delete;
        ScopedEditorHost& operator=(const ScopedEditorHost&) = delete;

    private:
        EditorHostKind _previous;
    };

}  // namespace BECore::Tests
