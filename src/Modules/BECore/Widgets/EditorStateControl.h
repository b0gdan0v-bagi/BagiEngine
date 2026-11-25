#pragma once

namespace BECore {

    class ControlService;
    class EditorDocuments;

    /**
     * @brief Registers `editor_state` on @p service: whether the editor panels hold unsaved changes (`.claude/rules/control-editor-api.md`).
     *
     * Answers `{"anyDirty", "documents": [{"id", "kind", "name", "dirty", "path", "file"}]}`, sorted by `id`. In the Game run mode
     * (`WidgetManager::GetMode()`, as `status.runMode`) it fails with `not_editor`: no panel exists there.
     *
     * @p documents must outlive the registration (CoreManager registers its own). Registering twice replaces the handler.
     */
    void RegisterEditorStateControl(ControlService& service, EditorDocuments& documents);

}  // namespace BECore
