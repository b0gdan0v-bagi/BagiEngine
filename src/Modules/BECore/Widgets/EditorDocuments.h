#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <cstdint>

namespace BECore {

    /// What an editor document is: the active scene, a node file the pending UI edits patch, a selected config, a library (the Sprite Library)
    CORE_ENUM(EditorDocumentKind, uint8_t, Scene, NodeFile, Config, Library)

    /// One document of an editor panel as `editor_state` reports it (`.claude/rules/control-editor-api.md`)
    struct EditorDocumentState {
        String id;  ///< Unique among the documents, `<kind>:<name>`; the order of the answer
        EditorDocumentKind kind = EditorDocumentKind::Scene;
        String name;
        bool dirty = false;
        String path;  ///< The virtual path a save names
        String file;  ///< The real file a save writes (the overlay's with `--content-overlay`)
    };

    class EditorDocuments;

    /**
     * @brief Keeps one source of an EditorDocuments registered: the source is removed when the handle is destroyed or Reset().
     *
     * It must not outlive the EditorDocuments (CoreManager's one outlives every widget). Main thread only.
     */
    class EditorDocumentsHandle {
    public:
        EditorDocumentsHandle() = default;
        ~EditorDocumentsHandle();

        EditorDocumentsHandle(const EditorDocumentsHandle&) = delete;
        EditorDocumentsHandle& operator=(const EditorDocumentsHandle&) = delete;
        EditorDocumentsHandle(EditorDocumentsHandle&& other) noexcept;
        EditorDocumentsHandle& operator=(EditorDocumentsHandle&& other) noexcept;

        /// Removes the source now
        void Reset();

    private:
        friend class EditorDocuments;

        EditorDocumentsHandle(EditorDocuments* owner, uint64_t token) : _owner(owner), _token(token) {}

        EditorDocuments* _owner = nullptr;
        uint64_t _token = 0;
    };

    /**
     * @brief The documents the editor panels have open, for the control method `editor_state`. `CoreManager::GetEditorDocuments()`.
     *
     * A panel adds a source that appends the state of its documents when asked: the data is the panel's own flags, not drawn state, so a closed panel
     * keeps reporting. Main thread only.
     */
    class EditorDocuments {
    public:
        /// Appends the documents of one panel to the vector
        using Source = MoveOnlyFunction<void(Vector<EditorDocumentState>&)>;

        EditorDocuments() = default;
        EditorDocuments(const EditorDocuments&) = delete;
        EditorDocuments& operator=(const EditorDocuments&) = delete;

        /// Registers @p source until the handle ends. Sources must not add or remove sources while they run
        [[nodiscard]] EditorDocumentsHandle Add(Source source);

        /// The documents of every source sorted by `id`; of two with the same `id` the one of the earlier source stays
        [[nodiscard]] Vector<EditorDocumentState> Collect() const;

    private:
        friend class EditorDocumentsHandle;

        struct Entry {
            uint64_t token = 0;
            Source source;
        };

        void Remove(uint64_t token);

        Vector<Entry> _sources;
        uint64_t _nextToken = 1;
    };

}  // namespace BECore
