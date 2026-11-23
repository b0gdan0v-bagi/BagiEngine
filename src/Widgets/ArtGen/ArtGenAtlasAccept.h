#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <cstdint>
#include <expected>

/// Accept into an atlas group named by a preset or a set (`ArtGenImagePlan::atlas`). The library side, no files of its own beyond what
/// `WriteAtlas` writes and no services, so it is tested on a temporary game folder.
///
/// A sprite of such an accept becomes a member of the group and the group is packed, so the picture is drawn from the atlas. The loose
/// PNG and its `.artgen.json` stay the `atlas.source` of the packed entry (the rule of `WriteAtlas`), so a later Accept of one member, or
/// a switch of a live row of a set, packs the group again. `AcceptIntoLibrary` is the same for a sprite that is already packed; here the
/// group is named by the caller, so a sprite with no entry or no link joins it.
///
/// A set packs once, not per row: every row is stored with `deferPack` and `PackGroupAfterAccept` runs after the last one, inside the
/// accept batch (`ArtGenAccept.h`), so the one write of the library is the batch's.
namespace BECore {

    /**
     * @brief The entry Accept stores for @p sprite when it goes into atlas group @p group, once its picture is the whole PNG at
     * @p texturePath of @p width x @p height.
     *
     * - no entry, no link, or an unpacked member of another group: the whole image, linked to @p group with no pack record;
     * - a member of @p group: exactly `AcceptedSpriteEntry` (an unpacked one keeps its link and pack record, a packed one keeps its place
     *   in the atlas and only its `atlas.source` becomes the new PNG);
     * - a packed member of another group: an error that names the sprite and both groups. A packed sprite cannot change group without an
     *   Unpack (the person's action in the Sprite Library), and nothing is changed.
     *
     * A @p group that fails `IsValidSpriteName` (it is also the atlas file's name) is an error. Pure: the library is only read.
     */
    [[nodiscard]] std::expected<SpriteEntry, String> AcceptedSpriteEntryInGroup(const TextureLibrary& library, PoolString sprite, PoolString texturePath, uint32_t width, uint32_t height,
                                                                                StringView group);

    /**
     * @brief Packs group @p group with the settings it was last packed with (`PackSettingsOf`; the defaults for a new group) and saves
     * the library again when the pack moved a member.
     *
     * The order is `AcceptIntoLibrary`'s: `WriteAtlas` first (on its error the library and the old atlas file are as they were), then
     * the save; when that save fails the members go back to what the library held before the pack (the atlas file is already replaced,
     * and the error says so). The returned `AcceptStored` has `saved` set when this call saved and `repacked` set to the atlas written.
     * A group with no member is an error. @p pngForMessage names the file the accept wrote, for the error texts (empty: none, as when the
     * group is packed on its own); each error says that the sprites are in the game and that Pack in the Sprite Library finishes it.
     */
    std::expected<AcceptStored, String> PackGroupAfterAccept(TextureLibrary& library, StringView group, const AtlasIo& io, FunctionRef<bool()> save, StringView pngForMessage);

    /**
     * @brief The library side of Accept of the PNG @p copied as @p sprite into @p group: `AcceptedSpriteEntryInGroup`, `StoreSpriteEntry`,
     * and unless @p deferPack `PackGroupAfterAccept`.
     *
     * With @p deferPack the sprite is a loose member of the group (the entry is saved, `repacked` stays empty) and the caller packs the group
     * once after its last row. A failed pack leaves the new entry stored and saved, so the PNG, the entry and the old atlas file agree
     * (the error says Pack in the Sprite Library finishes it).
     */
    std::expected<AcceptStored, String> AcceptIntoGroup(TextureLibrary& library, const ArtGenCopied& copied, StringView sprite, StringView group, bool deferPack, const AtlasIo& io,
                                                        FunctionRef<bool()> save);

    /**
     * @brief The atlas members of the row answer of `artgen_accept`, set on @p reply: `atlasGroup` is @p group (absent for none) and `atlas`
     * is @p repacked (absent with no group and while a pack is deferred).
     */
    void AddAtlasKeysToReply(ArtGenAcceptAnswer& reply, StringView group, const Optional<AtlasWritten>& repacked);

    /**
     * @brief Packs group @p group of the game's library (`PackGroupAfterAccept`, which has the order and what a failure leaves), drops the
     * atlas file from the resource cache and reloads every `SpriteRendererComponent` of the active scene that names a member the pack moved.
     * The editor's way to pack once after the rows of a set. It saves through `SaveGameLibraryForAccept` and tells an open batch that an
     * atlas file was rewritten (`NoteAtlasRewrittenForAccept`), so with a batch open it never writes `TextureLibrary.xml` itself.
     *
     * Answers the atlas written; a failure is `artgen_accept_failed` with the reason.
     */
    std::expected<ArtGenAtlasAnswer, ControlError> PackAtlasForControl(StringView group);

}  // namespace BECore
