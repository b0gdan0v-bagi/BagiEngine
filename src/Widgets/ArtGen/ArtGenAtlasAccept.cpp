#include "ArtGenAtlasAccept.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>

namespace BECore {

    namespace {

        /// What the accept wrote, for an error text: the PNG, or the sprites in general when no PNG is named
        String WrittenText(StringView pngForMessage) {
            return pngForMessage.empty() ? String("the sprites are in the game") : Format("the PNG is in '{}'", pngForMessage);
        }

    }  // namespace

    std::expected<SpriteEntry, String> AcceptedSpriteEntryInGroup(const TextureLibrary& library, PoolString sprite, PoolString texturePath, uint32_t width, uint32_t height, StringView group) {
        if (!IsValidSpriteName(group)) {
            return std::unexpected(Format("'{}' is not an atlas name (1 to {} of A-Z a-z 0-9 _ -)", group, kMaxSpriteNameLength));
        }
        const SpriteEntry* existing = library.GetSprite(sprite);
        const bool inGroup = existing != nullptr && existing->atlas.has_value() && existing->atlas->name.ToStringView() == group;
        if (inGroup) {
            return AcceptedSpriteEntry(library, sprite, texturePath, width, height);
        }
        if (existing != nullptr && IsPackedSprite(*existing)) {
            return std::unexpected(
                Format("'{}' is packed in atlas '{}'; Unpack it in the Sprite Library first, then accept it into '{}'", sprite.ToStringView(), existing->atlas->name.ToStringView(), group));
        }
        SpriteEntry entry;
        entry.name = sprite;
        entry.texturePath = texturePath;
        entry.srcRect = Rect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        SpriteAtlasLink link;
        link.name = PoolString::Intern(group);
        entry.atlas = link;
        return entry;
    }

    std::expected<AcceptStored, String> PackGroupAfterAccept(TextureLibrary& library, StringView group, const AtlasIo& io, FunctionRef<bool()> save, StringView pngForMessage) {
        if (AtlasMembers(library, group).empty()) {
            return std::unexpected(Format("{}, but atlas '{}' has no sprite to pack", WrittenText(pngForMessage), group));
        }
        auto repacked = RepackGroupAndSave(library, group, io, save);
        if (!repacked) {
            if (!repacked.error().atlasWritten) {
                return std::unexpected(
                    Format("{}, the library points to it, but atlas '{}' could not be packed: {}; press Pack in the Sprite Library", WrittenText(pngForMessage), group, repacked.error().reason));
            }
            return std::unexpected(Format("{} and atlas '{}' was rewritten, but TextureLibrary.xml could not be saved; press Pack in the Sprite Library", WrittenText(pngForMessage), group));
        }
        return std::move(*repacked);
    }

    std::expected<AcceptStored, String> AcceptIntoGroup(TextureLibrary& library, const ArtGenCopied& copied, StringView sprite, StringView group, bool deferPack, const AtlasIo& io,
                                                        FunctionRef<bool()> save) {
        const PoolString name = PoolString::Intern(sprite);
        const PoolString texturePath = PoolString::Intern(copied.texturePath);
        const String png = Format("{}", copied.pngFile.generic_string());

        auto entry = AcceptedSpriteEntryInGroup(library, name, texturePath, copied.width, copied.height, group);
        if (!entry) {
            return std::unexpected(Format("the PNG is in '{}', but {}", png, entry.error()));
        }
        auto stored = StoreSpriteEntry(library, *entry, save);
        if (!stored) {
            return std::unexpected(Format("the PNG is in '{}', but {}", png, stored.error()));
        }
        AcceptStored result;
        result.saved = *stored;
        if (deferPack) {
            return result;
        }
        auto packed = PackGroupAfterAccept(library, group, io, save, StringView(png));
        if (!packed) {
            return std::unexpected(packed.error());
        }
        packed->saved = packed->saved || result.saved;
        return packed;
    }

    void AddAtlasKeysToReply(ArtGenAcceptAnswer& reply, StringView group, const Optional<AtlasWritten>& repacked) {
        if (!group.empty()) {
            reply.atlasGroup = String(group);
        }
        if (repacked) {
            reply.atlas = MakeArtGenAtlasAnswer(*repacked);
        }
    }

    std::expected<ArtGenAtlasAnswer, ControlError> PackAtlasForControl(StringView group) {
        auto fail = [](const String& message) { return std::unexpected(ControlError{String("artgen_accept_failed"), message}); };
        auto& files = CoreManager::GetFileSystem();
        const Path assetsDir = files.GetMountedPath(MountPoints::assets);
        if (assetsDir.Empty()) {
            return fail(String("the game has no assets folder"));
        }
        auto& library = CoreManager::GetTextureLibrary();
        auto resolve = [&files](StringView path) { return files.ResolvePath(path); };
        const AtlasIo io{assetsDir, resolve};
        auto packed = PackGroupAfterAccept(library, group, io, [] { return SaveGameLibraryForAccept(); }, StringView());
        if (!packed) {
            return fail(packed.error());
        }
        NoteAtlasRewrittenForAccept();
        // The old pictures of the atlas path may be cached: drop them so the reload reads the new file
        CoreManager::GetResourceManager().Evict(packed->repacked->texturePath.ToStringView());
        if (Scene* scene = CoreManager::GetSceneManager().GetActiveScene()) {
            for (const PoolString member : packed->repacked->changed) {
                ReloadSpritesUsing(scene->GetRootNode(), member, PoolString{});
            }
        }
        return MakeArtGenAtlasAnswer(*packed->repacked);
    }

}  // namespace BECore
