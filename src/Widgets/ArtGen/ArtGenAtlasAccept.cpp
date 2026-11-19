#include "ArtGenAtlasAccept.h"

namespace BECore {

    namespace {

        bool IsSameRect(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

        bool IsSameLink(const Optional<SpriteAtlasLink>& a, const Optional<SpriteAtlasLink>& b) {
            if (a.has_value() != b.has_value()) {
                return false;
            }
            if (!a.has_value()) {
                return true;
            }
            if (a->name != b->name || a->source.has_value() != b->source.has_value() || a->pack.has_value() != b->pack.has_value()) {
                return false;
            }
            if (a->source && (a->source->texturePath != b->source->texturePath || !IsSameRect(a->source->rect, b->source->rect))) {
                return false;
            }
            return !a->pack || (a->pack->padding == b->pack->padding && a->pack->sizeMode == b->pack->sizeMode && a->pack->maxSide == b->pack->maxSide);
        }

        bool IsSameEntry(const SpriteEntry& a, const SpriteEntry& b) {
            return a.name == b.name && a.texturePath == b.texturePath && IsSameRect(a.srcRect, b.srcRect) && IsSameLink(a.atlas, b.atlas);
        }

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
        // The members as the saved library has them: what a failed save of the pack goes back to
        Vector<SpriteEntry> before;
        for (const PoolString member : AtlasMembers(library, group)) {
            before.push_back(*library.GetSprite(member));
        }
        if (before.empty()) {
            return std::unexpected(Format("{}, but atlas '{}' has no sprite to pack", WrittenText(pngForMessage), group));
        }
        auto written = WriteAtlas(library, group, PackSettingsOf(library, group), io);
        if (!written) {
            return std::unexpected(
                Format("{}, the library points to it, but atlas '{}' could not be packed: {}; press Pack in the Sprite Library", WrittenText(pngForMessage), group, written.error()));
        }
        AcceptStored result;
        const bool entriesChanged = std::any_of(before.begin(), before.end(), [&library](const SpriteEntry& old) { return !IsSameEntry(old, *library.GetSprite(old.name)); });
        if (entriesChanged) {
            if (!save()) {
                for (const SpriteEntry& old : before) {
                    library.AddOrUpdate(old);
                }
                return std::unexpected(Format("{} and atlas '{}' was rewritten, but TextureLibrary.xml could not be saved; press Pack in the Sprite Library", WrittenText(pngForMessage), group));
            }
            result.saved = true;
        }
        result.repacked = std::move(*written);
        return result;
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

}  // namespace BECore
