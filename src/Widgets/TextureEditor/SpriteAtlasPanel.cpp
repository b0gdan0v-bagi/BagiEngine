#include "SpriteAtlasPanel.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Generated/SpriteAtlasPanel.gen.hpp>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>

namespace BECore {

    namespace {

        PoolString GroupOf(const SpriteEntry& entry) {
            return entry.atlas.has_value() ? entry.atlas->name : PoolString{};
        }

        /// The sprites of the active scene that show these library entries show the new pixels from the next frame
        void ReloadInActiveScene(const Vector<PoolString>& sprites) {
            SceneManager* scenes = CoreManager::FindSceneManager();
            Scene* scene = scenes != nullptr ? scenes->GetActiveScene() : nullptr;
            if (scene == nullptr) {
                return;
            }
            for (const PoolString sprite : sprites) {
                ReloadSpritesUsing(scene->GetRootNode(), sprite, PoolString{});
            }
        }

    }  // namespace

    void SpriteAtlasPanel::Sync(SpriteAtlasView& view, const TextureLibrary& library, const SpriteEntry& entry) {
        const PoolString group = GroupOf(entry);
        if (SyncNameField(library, entry, group)) {
            const StringView name = group.ToStringView();
            view.name.assign(name.data(), name.size());
            view.padding = _settings.padding;
            view.sizeMode = _settings.sizeMode;
        }
        view.padding = std::clamp(view.padding, 0, kMaxAtlasPadding);
        _settings.padding = view.padding;
        _settings.sizeMode = view.sizeMode;

        const bool packed = IsPackedSprite(entry);
        const AtlasCounts counts = group.Empty() ? AtlasCounts{} : AtlasSummary(library, group.ToStringView());
        if (!_viewBuilt || group != _shownGroup || packed != _shownPacked || counts.members != _shownCounts.members || counts.packed != _shownCounts.packed) {
            _viewBuilt = true;
            _shownGroup = group;
            _shownPacked = packed;
            _shownCounts = counts;
            view.packed = packed;
            view.packedText = packed ? Format("Packed on '{}': Unpack to change", group.ToStringView()) : String{};
            view.hasGroup = !group.Empty();
            view.summary = group.Empty() ? String{} : Format("Atlas '{}': {} sprites, {} packed", group.ToStringView(), counts.members, counts.packed);
            view.canPack = counts.members > 0;
            view.canUnpack = counts.packed > 0;
        }
        if (_statusRevision != _shownStatusRevision) {
            _shownStatusRevision = _statusRevision;
            view.status.assign(_status);
            view.statusError = _hasError;
            view.hasStatus = _status[0] != '\0';
        }
    }

    bool SpriteAtlasPanel::SyncNameField(const TextureLibrary& library, const SpriteEntry& entry, PoolString group) {
        if (entry.name == _boundSprite && group == _boundGroup) {
            return false;
        }
        if (entry.name != _boundSprite) {
            SetStatus({}, false);
        }
        // A repack from here must make the file the group was packed as (and the file Accept makes), so the fields follow the group;
        // a group nobody packed yet keeps what the person set
        if (group != _boundGroup && !group.Empty() && HasPackRecord(library, group.ToStringView())) {
            _settings = PackSettingsOf(library, group.ToStringView());
        }
        _boundSprite = entry.name;
        _boundGroup = group;
        return true;
    }

    bool SpriteAtlasPanel::ApplyGroupName(TextureLibrary& library, const SpriteEntry& entry, StringView name) {
        auto result = SetSpriteAtlas(library, entry.name, name);
        if (!result) {
            SetStatus(StringView(result.error()), true);
            return false;
        }
        SetStatus({}, false);
        return *result;
    }

    bool SpriteAtlasPanel::Pack(TextureLibrary& library, PoolString group) {
        FileSystem& files = CoreManager::GetFileSystem();
        auto resolve = [&files](StringView path) { return files.ResolvePath(path); };
        const AtlasIo io = _files != nullptr ? _files->io : AtlasIo{files.GetMountedPath(MountPoints::assets), resolve};

        auto written = WriteAtlas(library, group.ToStringView(), _settings, io);
        if (!written) {
            SetStatus(StringView(written.error()), true);
            return false;
        }

        // The file under this path is new: a texture loaded from the old one is stale
        CoreManager::GetResourceManager().Evict(written->texturePath.ToStringView());
        ReloadInActiveScene(written->changed);
        const String text = Format("Packed {} sprites into {} ({}x{})", written->changed.size(), written->texturePath.ToStringView(), written->width, written->height);
        SetStatus(StringView(text), false);
        return true;
    }

    bool SpriteAtlasPanel::Unpack(TextureLibrary& library, PoolString group) {
        const FileSystem& files = CoreManager::GetFileSystem();
        auto inGame = [&files](PoolString path) { return files.Exists(path.ToStringView()); };
        const FunctionRef<bool(PoolString)> exists = _files != nullptr ? _files->exists : FunctionRef<bool(PoolString)>(inGame);

        auto unpacked = UnpackAtlas(library, group.ToStringView(), exists);
        if (!unpacked) {
            SetStatus(StringView(unpacked.error()), true);
            return false;
        }

        for (const PoolString texture : unpacked->oldTextures) {
            CoreManager::GetResourceManager().Evict(texture.ToStringView());
        }
        ReloadInActiveScene(unpacked->changed);
        const String text = Format("Unpacked {} sprites; {} stays on disk", unpacked->changed.size(), AtlasTexturePath(group.ToStringView()).ToStringView());
        SetStatus(StringView(text), false);
        return true;
    }

    void SpriteAtlasPanel::SetStatus(StringView text, bool isError) {
        ++_statusRevision;
        _hasError = isError;
        if (text.empty()) {
            _status[0] = '\0';
            return;
        }
        snprintf(_status, sizeof(_status), "%.*s", static_cast<int>(text.size()), text.data());
    }

}  // namespace BECore
