#include "SpriteLibraryPanelModel.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Generated/SpriteLibraryPanelModel.gen.hpp>
#include <Widgets/ArtGen/ArtGenAccept.h>
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
        _slots.Register();
    }

    TypedRef SpriteLibraryPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void SpriteLibraryPanelModel::UseLibrary(TextureLibrary* library, LibrarySave save) {
        _library = library;
        _save = library != nullptr ? std::move(save) : LibrarySave{};
        _key.built = false;
    }

    void SpriteLibraryPanelModel::UseAtlasFiles(const SpriteAtlasFiles* files) {
        _atlasPanel.UseFiles(files);
    }

    void SpriteLibraryPanelModel::UseTextureMount(PoolString mountPoint) {
        _textureMount = mountPoint;
        RefreshTextureList();
    }

    TextureLibrary& SpriteLibraryPanelModel::Library() const {
        return _library != nullptr ? *_library : CoreManager::GetTextureLibrary();
    }

    const TextureLibrary& SpriteLibraryPanelModel::GetLibrary() const {
        return Library();
    }

    const SpriteEntry* SpriteLibraryPanelModel::GetSelected() const {
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
        const SpriteEntry* entry = GetSelected();
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
        if (_view.dropOpen && StringView(_view.dropName) != StringView(_plannedDropName)) {
            PlanDropName();
        }
        const SpriteEntry* entry = GetSelected();
        // Edits are taken only against the view they were made in: a library that moved under it wins, and the view is made again
        if (entry != nullptr && _key.built && _key.index == _selectedIndex && _key.name == entry->name && _key.texturePath == entry->texturePath && SameRect(_key.rect, entry->srcRect)) {
            ApplyViewEdits(*entry);
        }
        SyncView();

        entry = GetSelected();
        _view.slice.canSlice = CanSlice(entry);
        if (_view.slice.canSlice) {
            _slicePanel.Sync(_view.slice, Library(), entry->texturePath, _sheetWidth, _sheetHeight);
        }
        SyncAtlas(entry);
    }

    void SpriteLibraryPanelModel::SyncAtlas(const SpriteEntry* entry) {
        if (entry != nullptr) {
            _atlasPanel.Sync(_view.atlas, Library(), *entry);
        }
        const PoolString group = entry != nullptr && entry->atlas.has_value() ? entry->atlas->name : PoolString{};
        SpriteAtlasPreviewView& preview = _view.preview;
        preview.hasGroup = !group.Empty();
        preview.anyPacked = preview.hasGroup && AtlasSummary(Library(), group.ToStringView()).packed > 0;
        if (preview.mode != _shownPreviewMode) {
            _previewChoice = preview.mode;
        }
        // Without a packed member there is no current atlas to show, so the plan is what is left
        _shownPreviewMode = preview.anyPacked ? _previewChoice : AtlasPreviewMode::Plan;
        preview.mode = _shownPreviewMode;
    }

    void SpriteLibraryPanelModel::AtlasChanged() {
        _textures.ForgetFailed();
        MarkDirty();
    }

    bool SpriteLibraryPanelModel::CanSlice(const SpriteEntry* entry) const {
        return entry != nullptr && !IsPackedSprite(*entry) && !entry->texturePath.Empty() && entry->texturePath == _sheetPath && _sheetWidth > 0 && _sheetHeight > 0;
    }

    void SpriteLibraryPanelModel::SliceAdd() {
        const PoolString firstAdded = _slicePanel.AddPlanned(Library());
        if (firstAdded.Empty()) {
            return;
        }
        _libraryPanel.ResetFilter();
        SelectByName(firstAdded);
        MarkDirty();
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
        const SpriteEntry* entry = GetSelected();
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

    void SpriteLibraryPanelModel::NoteListClick(int index) {
        _notedClick = index;
    }

    void SpriteLibraryPanelModel::NoteDrop(const SpriteLibraryDrop& drop) {
        _notedDrop = drop;
    }

    void SpriteLibraryPanelModel::NoteDraggedRect(const Rect& rect) {
        _notedRect = rect;
    }

    void SpriteLibraryPanelModel::NotePreviewClick(PoolString sprite) {
        _notedPreviewClick = sprite;
    }

    void SpriteLibraryPanelModel::NoteSheet(PoolString texturePath, int width, int height) {
        _sheetPath = texturePath;
        _sheetWidth = width;
        _sheetHeight = height;
    }

    void SpriteLibraryPanelModel::EndUpdate() {
        if (_notedClick >= 0) {
            Select(std::exchange(_notedClick, -1));
        }
        if (!_notedPreviewClick.Empty()) {
            SelectByName(std::exchange(_notedPreviewClick, PoolString{}));
        }
        if (_notedRect) {
            ApplyDraggedRect(*_notedRect);
            _notedRect.reset();
        }
        if (_notedDrop) {
            const SpriteLibraryDrop drop = *_notedDrop;
            _notedDrop.reset();
            ApplyDrop(drop);
        }
    }

    void SpriteLibraryPanelModel::ApplyDraggedRect(const Rect& rect) {
        const SpriteEntry* entry = GetSelected();
        if (entry == nullptr || IsPackedSprite(*entry)) {
            return;
        }
        SpriteEntry changed = *entry;
        changed.srcRect = rect;
        Library().AddOrUpdate(std::move(changed));
        MarkDirty();
    }

    void SpriteLibraryPanelModel::ApplyDrop(const SpriteLibraryDrop& drop) {
        const TextureLibrary& library = Library();
        const Vector<SpriteEntry>& entries = library.GetAll();
        if (drop.row < 0) {
            _pendingDrop = drop.payload;
            _view.dropName.assign(drop.payload.suggestedName);
            _view.dropOpen = true;
            PlanDropName();
            return;
        }
        if (static_cast<size_t>(drop.row) >= entries.size()) {
            return;
        }
        const ArtGenDropPlan plan = PlanArtGenDrop(drop.payload, entries[static_cast<size_t>(drop.row)].name.ToStringView(), library);
        if (plan.mode == ArtGenDropMode::Refused) {
            _view.dropMessage = plan.reason;
            _view.dropFailed = true;
            _view.hasDropMessage = !_view.dropMessage.empty();
            return;
        }
        AcceptDrop(drop.payload, plan.sprite);
    }

    void SpriteLibraryPanelModel::AcceptDrop(const ArtGenDragPayload& payload, StringView sprite) {
        ArtGenDropResult result;
        ArtGenVariantDropEvent::Emit(payload, sprite, &result);
        _view.dropFailed = !result.ok;
        _view.dropMessage = std::move(result.message);
        _view.hasDropMessage = !_view.dropMessage.empty();
        // The accept saved the library file itself, so the unsaved flag stays as it is
        if (result.ok) {
            _libraryPanel.ResetFilter();
            SelectByName(PoolString::Intern(sprite));
        }
    }

    void SpriteLibraryPanelModel::PlanDropName() {
        // The payload holds the name in a fixed buffer: a longer one is cut, as the old field could not hold more
        constexpr size_t kMaxDropName = kArtGenRowNameSize - 1;
        if (_view.dropName.size() > kMaxDropName) {
            _view.dropName.resize(kMaxDropName);
        }
        _plannedDropName = _view.dropName;
        const ArtGenDragPayload named = MakeArtGenDragPayload(_pendingDrop.kind, _pendingDrop.seed, _pendingDrop.set, _pendingDrop.job, _view.dropName);
        _dropPlan = PlanArtGenDrop(named, StringView{}, Library());
        _view.dropRefused = _dropPlan.mode == ArtGenDropMode::Refused;
        _view.dropReason = _view.dropRefused ? _dropPlan.reason : String{};
        _view.dropTaken = !_view.dropRefused && StringView(_dropPlan.sprite) != StringView(_view.dropName);
        _view.dropTakenText = _view.dropTaken ? Format("Taken: it will be added as '{}'", _dropPlan.sprite) : String{};
    }

    void SpriteLibraryPanelModel::CloseDropDialog() {
        _view.dropOpen = false;
        _plannedDropName.clear();
    }

    void SpriteLibraryPanelModel::Rename() {
        const SpriteEntry* entry = GetSelected();
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
        const SpriteEntry* entry = GetSelected();
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
        if (is(EditorCommandIds::kSpriteLibraryDropAccept)) {
            if (_view.dropOpen && _dropPlan.mode != ArtGenDropMode::Refused) {
                CloseDropDialog();
                AcceptDrop(_pendingDrop, _dropPlan.sprite);
            }
            return true;
        }
        if (is(EditorCommandIds::kSpriteLibraryDropCancel)) {
            CloseDropDialog();
            return true;
        }
        if (is(EditorCommandIds::kSpriteLibrarySliceFindIslands)) {
            const SpriteEntry* entry = GetSelected();
            if (CanSlice(entry)) {
                _slicePanel.FindIslands(entry->texturePath, _sheetWidth, _sheetHeight);
            }
            return true;
        }
        if (is(EditorCommandIds::kSpriteLibraryAtlasName)) {
            const SpriteEntry* entry = GetSelected();
            if (entry != nullptr && !IsPackedSprite(*entry)) {
                // The old field held the longest name the model accepts; a longer one is cut the same way, and the rules refuse what is left if bad
                if (_view.atlas.name.size() > kMaxSpriteNameLength) {
                    _view.atlas.name.resize(kMaxSpriteNameLength);
                }
                if (_atlasPanel.ApplyGroupName(Library(), *entry, _view.atlas.name)) {
                    AtlasChanged();
                }
            }
            return true;
        }
        if (is(EditorCommandIds::kSpriteLibraryPack) || is(EditorCommandIds::kSpriteLibraryUnpack)) {
            const SpriteEntry* entry = GetSelected();
            const PoolString group = entry != nullptr && entry->atlas.has_value() ? entry->atlas->name : PoolString{};
            if (group.Empty()) {
                return true;
            }
            const bool changed = is(EditorCommandIds::kSpriteLibraryPack) ? _atlasPanel.Pack(Library(), group) : _atlasPanel.Unpack(Library(), group);
            if (changed) {
                AtlasChanged();
            }
            return true;
        }
        if (is(EditorCommandIds::kSpriteLibrarySliceAdd)) {
            if (CanSlice(GetSelected())) {
                SliceAdd();
            }
            return true;
        }
        return false;
    }

}  // namespace BECore
