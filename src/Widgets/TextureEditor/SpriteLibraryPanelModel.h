#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Widgets/EditorDocuments.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/TextureEditor/SpriteLibraryPanel.h>
#include <Widgets/TextureEditor/SpriteTextureCache.h>

namespace BECore {

    class TextureLibrary;
    struct SpriteEntry;

    /**
     * @brief What `SpriteLibraryPanel.xml` binds to: the selected entry's form, the Save row and the drop answer
     *
     * `name`, `texture` and the four `rect*` fields are written by the controls of the panel and read by the model in its next `Update`; the rest is
     * written by the model.
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
     * `spriteLibrary.rename` (the name field's `commitCommand`), `spriteLibrary.rescan`, `spriteLibrary.add`, `spriteLibrary.delete`.
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
        [[nodiscard]] const SpriteEntry* Selected() const;
        void OnCommand(const EditorCommandEvent& event);
        void MarkDirty();
        void RefreshTextureList();
        /// Writes the view's texture and rect into the selected entry when the controls changed them
        void ApplyViewEdits(const SpriteEntry& entry);
        void SyncView();
        void Rename();
        void AddEntry();
        void DeleteEntry();

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

        SpriteTextureCache _textures;
        SpriteLibraryPanel _libraryPanel;

        // The registration comes last: it ends first, while the members its callback reads are alive
        EditorDocumentsHandle _documents;
    };

}  // namespace BECore
