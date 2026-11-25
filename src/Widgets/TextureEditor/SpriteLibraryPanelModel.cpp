#include "SpriteLibraryPanelModel.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Generated/SpriteLibraryPanelModel.gen.hpp>
#include <Widgets/EditorLayout/EditorDocumentRows.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>

namespace BECore {

    namespace {

        constexpr StringView kSpriteLibraryName = "sprites";
        /// The texture row of an entry with no texture
        constexpr StringView kNoTexture = "(none)";
        constexpr StringView kNewSpriteName = "new_sprite";
        constexpr StringView kTextureExtensions[] = {".png", ".jpg", ".jpeg", ".bmp"};

        StringView TextureText(PoolString path) {
            return path.Empty() ? kNoTexture : path.ToStringView();
        }

        bool SameRect(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

    }  // namespace

    void SpriteLibraryPanelModel::Initialize() {
        Subscribe<EditorCommandEvent, &SpriteLibraryPanelModel::OnCommand>(this);
        _documents = CoreManager::GetEditorDocuments().Add([this](Vector<EditorDocumentState>& out) { AppendDocuments(out); });
    }

    TypedRef SpriteLibraryPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void SpriteLibraryPanelModel::UseLibrary(TextureLibrary* library, LibrarySave save) {
        _library = library;
        _save = library != nullptr ? std::move(save) : LibrarySave{};
        _key.built = false;
    }

    void SpriteLibraryPanelModel::UseTextureMount(PoolString mountPoint) {
        _textureMount = mountPoint;
        RefreshTextureList();
    }

    TextureLibrary& SpriteLibraryPanelModel::Library() const {
        return _library != nullptr ? *_library : CoreManager::GetTextureLibrary();
    }

    const SpriteEntry* SpriteLibraryPanelModel::Selected() const {
        const Vector<SpriteEntry>& entries = Library().GetAll();
        if (_selectedIndex < 0 || static_cast<size_t>(_selectedIndex) >= entries.size()) {
            return nullptr;
        }
        return &entries[static_cast<size_t>(_selectedIndex)];
    }

    void SpriteLibraryPanelModel::AppendDocuments(Vector<EditorDocumentState>& out) {
        out.push_back(MakeConfigDocument(CoreManager::GetConfigManager(), EditorDocumentKind::Library, kSpriteLibraryName, _dirty, ConfigNames::textureLibrary));
    }

    void SpriteLibraryPanelModel::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kTextureSave) {
            SaveLibrary();
        }
    }

    void SpriteLibraryPanelModel::SaveLibrary() {
        if (!_dirty) {
            return;
        }
        const bool saved = _save ? _save(Library()) : Library().Save();
        if (saved) {
            _dirty = false;
        }
    }

    void SpriteLibraryPanelModel::MarkDirty() {
        _dirty = true;
    }

    void SpriteLibraryPanelModel::Select(int index) {
        _selectedIndex = index;
        const SpriteEntry* entry = Selected();
        const StringView name = entry != nullptr ? entry->name.ToStringView() : StringView{};
        _view.name.assign(name.data(), name.size());
        _seenName = _view.name;
        _view.renameRefused = false;
        _view.renameRefusedText.clear();
        _key.built = false;
    }

    void SpriteLibraryPanelModel::SelectByName(PoolString name) {
        const Vector<SpriteEntry>& entries = Library().GetAll();
        int found = -1;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].name == name) {
                found = static_cast<int>(i);
                break;
            }
        }
        Select(found);
    }

    void SpriteLibraryPanelModel::RefreshTextureList() {
        _textureRows = CoreManager::GetFileSystem().EnumerateFiles(_textureMount.Empty() ? MountPoints::assets : _textureMount, Span<const StringView>(kTextureExtensions));
        _view.textures.clear();
        _view.textures.reserve(_textureRows.size() + 1);
        _view.textures.push_back(String(kNoTexture));
        for (const PoolString path : _textureRows) {
            _view.textures.push_back(String(path.ToStringView()));
        }
        _texturesListed = true;
    }

    void SpriteLibraryPanelModel::Update() {
        if (!_texturesListed) {
            RefreshTextureList();
        }
        if (StringView(_view.name) != StringView(_seenName)) {
            _seenName = _view.name;
            _view.renameRefused = false;
            _view.renameRefusedText.clear();
        }
        const SpriteEntry* entry = Selected();
        // Edits are taken only against the view they were made in: a library that moved under it wins, and the view is made again
        if (entry != nullptr && _key.built && _key.index == _selectedIndex && _key.name == entry->name && _key.texturePath == entry->texturePath && SameRect(_key.rect, entry->srcRect)) {
            ApplyViewEdits(*entry);
        }
        SyncView();
    }

    void SpriteLibraryPanelModel::ApplyViewEdits(const SpriteEntry& entry) {
        const bool textureEdited = StringView(_view.texture) != TextureText(entry.texturePath);
        const Rect viewRect(_view.rectX, _view.rectY, _view.rectW, _view.rectH);
        const bool rectEdited = !SameRect(viewRect, entry.srcRect);
        if (!textureEdited && !rectEdited) {
            return;
        }
        // A packed entry's texture and rect point into its atlas: the view goes back to them
        if (IsPackedSprite(entry)) {
            _key.built = false;
            return;
        }
        SpriteEntry changed = entry;
        if (textureEdited) {
            // Only a row of the list is a choice; any other text (the combo writes rows only) changes nothing
            Optional<PoolString> chosen;
            if (StringView(_view.texture) == kNoTexture) {
                chosen = PoolString{};
            }
            for (const PoolString path : _textureRows) {
                if (!chosen && StringView(_view.texture) == path.ToStringView()) {
                    chosen = path;
                }
            }
            if (chosen) {
                changed.texturePath = *chosen;
            } else {
                _key.built = false;
            }
        }
        if (rectEdited) {
            changed.srcRect = viewRect;
        }
        if (changed.texturePath == entry.texturePath && SameRect(changed.srcRect, entry.srcRect)) {
            return;
        }
        Library().AddOrUpdate(std::move(changed));
        MarkDirty();
    }

    void SpriteLibraryPanelModel::SyncView() {
        const SpriteEntry* entry = Selected();
        ViewKey key;
        key.built = true;
        key.index = entry != nullptr ? _selectedIndex : -1;
        key.dirty = _dirty;
        if (entry != nullptr) {
            key.name = entry->name;
            key.texturePath = entry->texturePath;
            key.rect = entry->srcRect;
            key.packed = IsPackedSprite(*entry);
        }
        if (_key.built && key.index == _key.index && key.name == _key.name && key.texturePath == _key.texturePath && SameRect(key.rect, _key.rect) && key.packed == _key.packed &&
            key.dirty == _key.dirty) {
            return;
        }
        // The name field follows the entry only when the entry is another one or was renamed, so a typed name survives a change of the rect
        if (entry != nullptr && (key.index != _key.index || key.name != _key.name)) {
            const StringView name = entry->name.ToStringView();
            _view.name.assign(name.data(), name.size());
            _seenName = _view.name;
        }
        _key = key;
        _view.hasSelection = entry != nullptr;
        _view.dirty = _dirty;
        _view.packed = key.packed;
        const StringView texture = TextureText(key.texturePath);
        _view.texture.assign(texture.data(), texture.size());
        _view.rectX = key.rect.x;
        _view.rectY = key.rect.y;
        _view.rectW = key.rect.w;
        _view.rectH = key.rect.h;
    }

    void SpriteLibraryPanelModel::Rename() {
        const SpriteEntry* entry = Selected();
        if (entry == nullptr) {
            return;
        }
        const PoolString oldName = entry->name;
        const PoolString newName = PoolString::Intern(StringView(_view.name));
        if (newName == oldName) {
            return;
        }
        if (Library().Rename(oldName, newName)) {
            SelectByName(newName);
            MarkDirty();
            return;
        }
        Select(_selectedIndex);
        // Rename refuses an empty name too; only a taken one is worth a warning
        if (!newName.Empty()) {
            _view.renameRefused = true;
            _view.renameRefusedText = Format("A sprite named '{}' already exists", newName.ToStringView());
        }
    }

    void SpriteLibraryPanelModel::AddEntry() {
        TextureLibrary& library = Library();
        SpriteEntry entry;
        entry.name = library.MakeUniqueName(kNewSpriteName);
        const PoolString added = entry.name;
        library.AddOrUpdate(std::move(entry));
        _libraryPanel.ResetFilter();
        SelectByName(added);
        MarkDirty();
    }

    void SpriteLibraryPanelModel::DeleteEntry() {
        const SpriteEntry* entry = Selected();
        if (entry == nullptr) {
            return;
        }
        TextureLibrary& library = Library();
        library.Remove(entry->name);
        Select(std::min(_selectedIndex, static_cast<int>(library.GetAll().size()) - 1));
        MarkDirty();
    }

    bool SpriteLibraryPanelModel::RunCommand(StringView id, StringView /*argument*/) {
        const auto is = [id](PoolString command) { return id == command.ToStringView(); };
        if (is(EditorCommandIds::kSpriteLibraryRename)) {
            Rename();
            return true;
        }
        if (is(EditorCommandIds::kSpriteLibraryRescan)) {
            _textures.ForgetFailed();
            RefreshTextureList();
            return true;
        }
        if (is(EditorCommandIds::kSpriteLibraryAdd)) {
            AddEntry();
            return true;
        }
        if (is(EditorCommandIds::kSpriteLibraryDelete)) {
            DeleteEntry();
            return true;
        }
        return false;
    }

}  // namespace BECore
