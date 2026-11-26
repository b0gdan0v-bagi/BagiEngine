#pragma once

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/EditorDocuments.h>
#include <Events/ConfigEvents.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/SceneEditor/ComponentDefaults.h>
#include <Widgets/SceneEditor/FieldHelp.h>

namespace BECore {

    /// A field of the chosen component class as the cells of a row
    struct FieldHelpRow {
        BE_CLASS(FieldHelpRow)

        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD String type;
        BE_REFLECT_FIELD String range;
        BE_REFLECT_FIELD String defaultText;
        /// The start of the description's first line, so that the column stays narrow
        BE_REFLECT_FIELD String description;
        BE_REFLECT_FIELD String fullDescription;
        /// Where the description comes from: `engine`, `game`, `attribute` or `none`
        BE_REFLECT_FIELD String source;
        BE_REFLECT_FIELD bool selected = false;
    };

    /// What `FieldHelpPanel.xml` binds to
    struct FieldHelpPanelView {
        BE_CLASS(FieldHelpPanelView)

        /// The component classes the factory makes, by name
        BE_REFLECT_FIELD Vector<String> classes;
        BE_REFLECT_FIELD int32_t classIndex = 0;
        BE_REFLECT_FIELD Vector<FieldHelpRow> rows;
        BE_REFLECT_FIELD bool hasRows = false;
        BE_REFLECT_FIELD bool hasSelection = false;
        BE_REFLECT_FIELD String selectedField;
        /// The text of the multiline field: the description of the selected field, which the field itself writes
        BE_REFLECT_FIELD String description;
        BE_REFLECT_FIELD FieldHelpFile newEntryFile = FieldHelpFile::Engine;
        BE_REFLECT_FIELD bool dirty = false;
    };

    /**
     * @brief The model of the Field Help panel asset: shows and edits the descriptions of the component fields (`FieldHelp`, a table of its own)
     *
     * An edit is of the entry of exactly the chosen class and the selected field. It stays in the file whose entry answered when the field was
     * selected; with no such entry it goes to the file `newEntryFile` names. An emptied text removes the entry, so what is behind it (the other
     * file, a parent class, the field's attribute) answers again. `newEntryFile` starts at the game's file when `config` has a layer above the
     * engine's.
     *
     * Commands, which the widget passes only while the panel is open: `fieldHelp.select` (the argument is the name of a field of the chosen class; any
     * other text does nothing), `fieldHelp.save` writes the changed files and emits `FieldHelpSavedEvent` (a failure is an error toast and the change
     * waits), `fieldHelp.revert` reads the files again. A change of a help config, and an opening of the panel, read them again only while nothing is
     * unsaved. The rows are made again only when the class or the table's version moved, so a settled frame allocates nothing.
     */
    class FieldHelpPanelModel : public IPanelModel, public SubscriptionHolder {
        BE_CLASS(FieldHelpPanelModel)
    public:
        FieldHelpPanelModel() = default;

        /// Reads the files and registers the subscription and the `editor_state` source; called once by the widget that hosts the model
        void Initialize() override;
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;
        void OnOpenChanged(bool open) override;

        /// The files the descriptions are read from and saved to; `CoreManager`'s unless a test sets another before `Initialize`. It must outlive the model
        void SetFileSystem(const FileSystem& fileSystem);

        /// The controls of the asset write `classIndex`, `description` and `newEntryFile`; the model takes them in `Update` and before a command
        [[nodiscard]] FieldHelpPanelView& GetView() {
            return _view;
        }

    private:
        void BuildClasses();
        /// Takes what the controls wrote into the view since the last call
        void ApplyEdits();
        void ApplyDescription();
        void ChooseClass(int32_t index);
        void Select(StringView field);
        void ClearSelection();
        /// The description field and the file an edit of it goes to, as the table says now
        void LoadDraft();
        void Save();
        /// Drops what is unsaved and reads the files again
        void Reread();
        void RebuildRowsIfStale();
        void RebuildRows();
        [[nodiscard]] bool IsDirty() const;
        [[nodiscard]] StringView ChosenClass() const;
        void AppendDocuments(Vector<EditorDocumentState>& out) const;
        void OnConfigChanged(const ConfigEvents::ConfigChangedEvent& event);

        FieldHelp _help;
        ComponentDefaults _defaults;
        FieldHelpPanelView _view;
        Vector<ClassMeta> _classes;
        /// Views the reflection's static name; empty for no selection
        StringView _selected;
        /// The description as the model last gave it to the field or took it from there
        String _draft;
        /// The file whose entry answered for the selected field when it was selected
        Optional<FieldHelpFile> _heldBy;
        /// The file an entry was made in since, for a field that had none
        Optional<FieldHelpFile> _createdIn;
        /// The class the rows are of; none before the first `Update`
        Optional<int32_t> _shownClass;
        uint64_t _builtVersion = 0;
        bool _rowsStale = true;
        EditorDocumentsHandle _documents;  // the source of `editor_state`, ends with the model
    };

}  // namespace BECore
