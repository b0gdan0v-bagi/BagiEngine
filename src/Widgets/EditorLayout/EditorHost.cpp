#include "EditorHost.h"

namespace BECore {

    namespace {

#ifdef __EMSCRIPTEN__
        constexpr EditorHostKind kBuildHost = EditorHostKind::Browser;
#else
        constexpr EditorHostKind kBuildHost = EditorHostKind::Desktop;
#endif

        /// What a browser cannot do: a command id, or with a trailing dot every command whose id starts so.
        /// Generate needs the art service and ComfyUI as processes, Export the build tools as processes, File changes a watch of the project's files, the
        /// Chrome trace a file to open, and a page opens no control link to attach with
        constexpr StringView kNotInBrowser[] = {"view.generate", "artgen.", "view.export", "export.", "view.fileChanges", "fileChanges.", "profiler.trace", "view.attach", "remote."};

        EditorHostKind& CurrentHost() {
            static EditorHostKind host = kBuildHost;
            return host;
        }

    }  // namespace

    EditorHostKind GetEditorHost() {
        return CurrentHost();
    }

    void SetEditorHost(EditorHostKind host) {
        CurrentHost() = host;
    }

    bool IsEditorCommandAvailable(StringView commandId) {
        if (GetEditorHost() != EditorHostKind::Browser) {
            return true;
        }
        return std::ranges::none_of(kNotInBrowser, [commandId](StringView entry) { return entry.ends_with('.') ? commandId.starts_with(entry) : commandId == entry; });
    }

    bool ReloadNotice::Take(EditorHostKind host) {
        if (_taken || host != EditorHostKind::Browser) {
            return false;
        }
        _taken = true;
        return true;
    }

    ReloadNotice& GetReloadNotice() {
        static ReloadNotice notice;
        return notice;
    }

}  // namespace BECore
