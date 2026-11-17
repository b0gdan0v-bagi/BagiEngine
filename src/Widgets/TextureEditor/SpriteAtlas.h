#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <Widgets/TextureEditor/SpriteAtlasPacker.h>
#include <Widgets/TextureEditor/SpriteImage.h>
#include <cstdint>
#include <expected>
#include <filesystem>

namespace BECore {

    /// The atlas file of a game is `assets/<kAtlasAssetsFolder>/<atlas>.png`
    inline constexpr StringView kAtlasAssetsFolder = "atlases";

    /// The virtual path of the file of atlas @p atlasName: what a packed sprite's `texturePath` is
    [[nodiscard]] PoolString AtlasTexturePath(StringView atlasName);

    /**
     * @brief Puts the unpacked sprite @p spriteName into the atlas group @p atlasName; an empty name takes it out of its group.
     *
     * The group name must pass `IsValidSpriteName` (it is also the file name). A packed sprite, an unknown sprite and a bad name are
     * errors with a text for the status line and change nothing. @return false when the sprite already was as asked.
     */
    [[nodiscard]] std::expected<bool, String> SetSpriteAtlas(TextureLibrary& library, PoolString spriteName, StringView atlasName);

    /// The sprites of group @p atlasName sorted by the bytes of their names, so the same set gives the same atlas whatever the library's order
    [[nodiscard]] Vector<PoolString> AtlasMembers(const TextureLibrary& library, StringView atlasName);

    /// A packed sprite's texture and rect point into its atlas, so they are not edited until Unpack
    [[nodiscard]] inline bool IsPackedSprite(const SpriteEntry& entry) {
        return entry.atlas.has_value() && entry.atlas->source.has_value();
    }

    /// The settings the group was last packed with (the record of its first member by name that has one); the defaults for none or a field out of range
    [[nodiscard]] PackSettings PackSettingsOf(const TextureLibrary& library, StringView atlasName);

    /// Whether some member of the group has a pack record, i.e. `PackSettingsOf` is more than the defaults
    [[nodiscard]] bool HasPackRecord(const TextureLibrary& library, StringView atlasName);

    struct AtlasCounts {
        uint32_t members = 0;
        uint32_t packed = 0;
    };

    [[nodiscard]] AtlasCounts AtlasSummary(const TextureLibrary& library, StringView atlasName);

    /// A member's crop of its source image, in whole pixels
    struct AtlasCrop {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct ImageSize {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /// The size of the image at a virtual path; the error is a line for the status text. `PlanAtlas` asks once per distinct file.
    using AtlasSizeLookup = FunctionRef<std::expected<ImageSize, String>(PoolString path)>;

    struct PlannedMember {
        PoolString name;
        PoolString sourcePath;  // where the pixels come from: the `atlas.source` of a packed member, else its own texture
        Rect sourceRect;
        AtlasCrop crop;
        PackedRect at;
    };

    struct PlannedAtlas {
        uint32_t width = 0;
        uint32_t height = 0;
        Vector<PlannedMember> members;  // in member order (sorted by name)
    };

    /**
     * @brief The layout `BuildAtlas` writes, from the sources' sizes alone: no pixel is read and nothing changes.
     *
     * It is the one planning step of Pack and of the editor's preview, so a preview is never another layout than the file. Every error
     * names the sprite.
     */
    [[nodiscard]] std::expected<PlannedAtlas, String> PlanAtlas(const TextureLibrary& library, StringView atlasName, const PackSettings& settings, AtlasSizeLookup sizeOf);

    /// Reads the pixels of a virtual path; the error is a line for the status text
    using AtlasImageLoader = FunctionRef<std::expected<RgbaImage, String>(PoolString path)>;

    struct BuiltAtlas {
        RgbaImage image;
        Vector<SpriteEntry> entries;  // the members rewritten, in member order: the atlas file, the placement, `atlas.source` the old source
    };

    /**
     * @brief Composes the atlas of group @p atlasName in memory (the layout of `PlanAtlas`, then the pixels) and changes nothing.
     *
     * A member's pixels come from its `atlas.source` when it is packed, else from its own texture and rect, never from the old atlas,
     * so a repack after a source changed is a plain Pack. Each distinct source file is read once. A rect is cut at its edges rounded
     * to the nearest pixel (at least one pixel a side), so the fractional rects of a sheet do not take in a neighbour; the float rect
     * stays in `atlas.source`, so Unpack is exact. The rest of the atlas is transparent black. Every error names the sprite.
     */
    [[nodiscard]] std::expected<BuiltAtlas, String> BuildAtlas(const TextureLibrary& library, StringView atlasName, const PackSettings& settings, AtlasImageLoader loader);

    /// Where `WriteAtlas` reads and writes: `resolve` turns the virtual path of a source into a file, `assetsDir` is the top layer the atlas lands under
    struct AtlasIo {
        std::filesystem::path assetsDir;
        FunctionRef<std::filesystem::path(StringView virtualPath)> resolve;
    };

    struct AtlasWritten {
        PoolString name;  // the group
        PoolString texturePath;
        uint32_t width = 0;
        uint32_t height = 0;
        Vector<PoolString> changed;  // the members, in member order
    };

    /**
     * @brief `BuildAtlas`, the PNG replaced atomically at `<assetsDir>/atlases/<atlas>.png`, and only then the library's entries replaced in place.
     *
     * The order is the contract: on any error the library and the old atlas file are as they were.
     */
    [[nodiscard]] std::expected<AtlasWritten, String> WriteAtlas(TextureLibrary& library, StringView atlasName, const PackSettings& settings, const AtlasIo& io);

    struct AtlasUnpacked {
        Vector<PoolString> changed;      // the packed members that went back to their sources
        Vector<PoolString> oldTextures;  // the files they pointed to (the atlas), for the texture cache
    };

    /**
     * @brief Sends every packed member of group @p atlasName back to its source and clears the `source`; the group name stays, so a later Pack repacks the group.
     *
     * Refused, with nothing changed, when no member is packed or a source file is gone (@p fileExists). The atlas PNG stays on disk: the
     * engine does not delete a game's file, and nothing refers to it any more.
     */
    [[nodiscard]] std::expected<AtlasUnpacked, String> UnpackAtlas(TextureLibrary& library, StringView atlasName, FunctionRef<bool(PoolString path)> fileExists);

}  // namespace BECore
