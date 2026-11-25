#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Widgets/EditorDocuments.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/ArtGen/ArtGenDrag.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/TextureEditor/SpriteLibraryPanel.h>
#include <Widgets/TextureEditor/SpriteLibrarySlots.h>
#include <Widgets/TextureEditor/SpriteSheetSlicePanel.h>
#include <Widgets/TextureEditor/SpriteTextureCache.h>

namespace BECore {

    class TextureLibrary;
    struct SpriteEntry;

    /**
     * @brief What `SpriteLibraryPanel.xml` binds to: the selected entry's form, the Save row and the drop answer
     *
     * `name`, `texture`, the four `rect*` fields and `dropName` are written by the controls of the panel and read by the model in its next `Update`; the
     * rest is written by the model.
     */
    struct SpriteLibraryPanelView {
        BE_CLASS(SpriteLibraryPanelView)

        BE_REFLECT_FIELD bool hasSelection = false;
        /// The rename field: it follows the selection and is applied by `spriteLibrary.rename` (its `commitCommand`)
        BE_REFLECT_FIELD String name;
        /// A rename was refused because another sprite has the name; cleared by a new selection or a new typed name
        BE_REFLECT_FIELD bool renameRefused = false;
        BE_REFLECT_FIELD String renameRefusedText;
        /// The texture path of the entry, `(none)` for none; one of the rows of `textures`
        BE_REFLECT_FIELD String texture;
        /// `(none)`, then the images of the assets folder; read once and again only on `spriteLibrary.rescan`
        BE_REFLECT_FIELD Vector<String> textures;
        /// The entry is packed into an atlas: its texture and rect are read-only until Unpack
        BE_REFLECT_FIELD bool packed = false;
        BE_REFLECT_FIELD float rectX = 0.0f;
        BE_REFLECT_FIELD float rectY = 0.0f;
        BE_REFLECT_FIELD float rectW = 0.0f;
        BE_REFLECT_FIELD float rectH = 0.0f;
        /// The library has changes that are not saved
        BE_REFLECT_FIELD bool dirty = false;
        /// The answer to the last drop of a generated variant on the list
        BE_REFLECT_FIELD String dropMessage;
        BE_REFLECT_FIELD bool dropFailed = false;
        BE_REFLECT_FIELD bool hasDropMessage = false;
        /// The "Add sprite" dialog of a variant dropped on the free area of the list: open until Accept or Cancel
        BE_REFLECT_FIELD bool dropOpen = false;
        /// The name of the new sprite, checked live by the plan of the drop
        BE_REFLECT_FIELD String dropName;
        /// The name is refused (`dropReason` says why): Accept is off
        BE_REFLECT_FIELD bool dropRefused = false;
        BE_REFLECT_FIELD String dropReason;
        /// The name is taken: `dropTakenText` names the free one Accept writes
        BE_REFLECT_FIELD bool dropTaken = false;
        BE_REFLECT_FIELD String dropTakenText;
    };

    /**
     * @brief The model of the Sprite Library asset: the selection, the entry form, Save and the `editor_state` row of the library, with no ImGui
     *
     * Works on `CoreManager::GetTextureLibrary()` unless a test gives its own library (`UseLibrary`). `Update` reads the view: a changed `texture` or rect
     * is written into the selected entry and marks the library unsaved, only while the entry is not packed (a packed entry's fields go back to what the
     * library holds). The view is made again only when a key moved (the selection, the entry's name, path, rect and packed flag, the unsaved flag), so a
     * steady `Update` allocates nothing.
     *
     * `texture.save` is taken by a subscription of the model's own, not by `RunCommand`: an asset routes commands only while its panel is open, and the
     * library is saved by the command while the panel is closed too. Commands (`EditorCommandIds`, run only while the panel is open):
     * `spriteLibrary.rename` (the name field's `commitCommand`), `spriteLibrary.rescan`, `spriteLibrary.add`, `spriteLibrary.delete`,
     * `spriteLibrary.dropAccept` and `spriteLibrary.dropCancel` (the "Add sprite" dialog).
     *
     * `Initialize` registers the slots `spriteList` and `spritePreview` (`SpriteLibrarySlots`). They note a clicked row, a drop and a finished drag, and
     * `EndUpdate` applies them. A drop on a row replaces that sprite at once; a drop on the free area opens the dialog. The accept goes through
     * `ArtGenVariantDropEvent` (the Generate panel owns the accept), and its answer is the view's drop message.
     */
    class SpriteLibraryPanelModel : public IPanelModel, public SubscriptionHolder {
        BE_CLASS(SpriteLibraryPanelModel)
    public:
        /// Writes the library to its file; true on success
        using LibrarySave = MoveOnlyFunction<bool(TextureLibrary&)>;

        SpriteLibraryPanelModel() = default;

        /// Subscribes to `texture.save` and registers the `editor_state` source; called once by the widget that hosts the model
        void Initialize() override;
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        void EndUpdate() override;
        bool RunCommand(StringView id, StringView argument) override;

        /// Works on @p library and saves it through @p save instead of CoreManager's library and its `Save` (a test over a library of its own; @p library
        /// must outlive the model). Null goes back to CoreManager's
        void UseLibrary(TextureLibrary* library, LibrarySave save);

        /// Lists the images of @p mountPoint instead of `assets` for the texture rows (a test over a folder of its own); the list is made again now
        void UseTextureMount(PoolString mountPoint);

        /// The one place that changes the selection: the name field follows it and a refused rename is forgotten. An index outside the list selects nothing
        void Select(int index);
        void SelectByName(PoolString name);

        [[nodiscard]] int GetSelectedIndex() const {
            return _selectedIndex;
        }

        /// The selected entry, or null; valid until the library changes
        [[nodiscard]] const SpriteEntry* GetSelected() const;
        [[nodiscard]] const TextureLibrary& GetLibrary() const;

        /// The list drawer and the textures of its rows, for the slot that draws them
        [[nodiscard]] SpriteLibraryPanel& GetLibraryPanel() {
            return _libraryPanel;
        }
        [[nodiscard]] SpriteTextureCache& GetTextureCache() {
            return _textures;
        }
        [[nodiscard]] const SpriteSheetSlicePanel& GetSlicePanel() const {
            return _slicePanel;
        }

        /// What the slots saw this frame, applied by `EndUpdate`: a clicked row of the list, a variant released on it, a rect dragged on the preview (texture
        /// pixels) and the size of the loaded preview texture (0 when none is loaded)
        void NoteListClick(int index);
        void NoteDrop(const SpriteLibraryDrop& drop);
        void NoteDraggedRect(const Rect& rect);
        void NoteSheetSize(int width, int height);

        /// The library with its unsaved flag, for `editor_state` (`.claude/rules/control-editor-api.md`); a closed panel reports too
        void AppendDocuments(Vector<EditorDocumentState>& out);

        /// The one path of the Save button and `texture.save`: nothing to save while there are no unsaved changes
        void SaveLibrary();

        [[nodiscard]] const SpriteLibraryPanelView& GetView() const {
            return _view;
        }

    private:
        /// What the view was last made from: a move of any of it makes the view again
        struct ViewKey {
            int index = -1;
            PoolString name;
            PoolString texturePath;
            Rect rect{};
            bool packed = false;
            bool dirty = false;
            bool built = false;
        };

        [[nodiscard]] TextureLibrary& Library() const;
        void OnCommand(const EditorCommandEvent& event);
        void MarkDirty();
        void RefreshTextureList();
        /// Writes the view's texture and rect into the selected entry when the controls changed them
        void ApplyViewEdits(const SpriteEntry& entry);
        void SyncView();
        void Rename();
        void AddEntry();
        void DeleteEntry();
        void ApplyDraggedRect(const Rect& rect);
        void ApplyDrop(const SpriteLibraryDrop& drop);
        /// Asks the Generate panel to accept the variant as @p sprite and shows its answer
        void AcceptDrop(const ArtGenDragPayload& payload, StringView sprite);
        /// The plan of the dialog's name, made again when the typed name changes
        void PlanDropName();
        void CloseDropDialog();

        SpriteLibraryPanelView _view;
        ViewKey _key;
        TextureLibrary* _library = nullptr;  // null: CoreManager's
        LibrarySave _save;
        int _selectedIndex = -1;
        bool _dirty = false;
        PoolString _textureMount;         // empty: `assets`
        Vector<PoolString> _textureRows;  // the images of `textures` after its `(none)` row, in its order
        bool _texturesListed = false;
        /// The text of the name field as the model last saw it: a new typed name clears a refused rename
        String _seenName;

        int _notedClick = -1;
        Optional<Rect> _notedRect;
        Optional<SpriteLibraryDrop> _notedDrop;
        int _sheetWidth = 0;
        int _sheetHeight = 0;

        ArtGenDragPayload _pendingDrop;  // the variant the dialog names
        ArtGenDropPlan _dropPlan;
        String _plannedDropName;  // the dialog's name `_dropPlan` was made for

        SpriteTextureCache _textures;
        SpriteLibraryPanel _libraryPanel;
        SpriteSheetSlicePanel _slicePanel;

        // The registrations come last: they end first, while the members their callbacks read are alive
        SpriteLibrarySlots _slots{*this};
        EditorDocumentsHandle _documents;
    };

}  // namespace BECore
