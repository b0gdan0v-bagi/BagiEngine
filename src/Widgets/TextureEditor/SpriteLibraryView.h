#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/StringUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// What a sprite's atlas link says: no group, in a group (pixels still on their own texture), or packed into the group's atlas file
    CORE_ENUM(SpriteAtlasState, uint8_t, Single, Grouped, Packed)

    /// `Packed` when `IsPackedSprite`, else `Grouped` when the link names a group, else `Single`
    [[nodiscard]] SpriteAtlasState AtlasStateOf(const SpriteEntry& entry);

    struct AtlasGroupInfo {
        PoolString name;
        uint32_t members = 0;
        uint32_t packed = 0;
    };

    /// The non-empty group names of @p library with their counts, sorted by the bytes of the name (as `AtlasMembers`)
    [[nodiscard]] Vector<AtlasGroupInfo> ListAtlasGroups(const TextureLibrary& library);

    CORE_ENUM(AtlasFilterKind, uint8_t, All, NoAtlas, Group)

    struct AtlasFilter {
        AtlasFilterKind kind = AtlasFilterKind::All;
        PoolString group;  // meaningful for `Group` only

        [[nodiscard]] bool operator==(const AtlasFilter&) const = default;
    };

    /// `NoAtlas` is the empty group name whatever the state; `Group` is the group name equal to `filter.group`
    [[nodiscard]] bool MatchesFilter(const SpriteEntry& entry, const AtlasFilter& filter);

    /// Indices into `library.GetAll()` of the sprites that match, in library order
    [[nodiscard]] Vector<uint32_t> FilterSprites(const TextureLibrary& library, const AtlasFilter& filter);

    /// A `Group` filter whose group is not in @p groups (its last member left or was renamed) becomes `All`
    [[nodiscard]] AtlasFilter NormalizeFilter(const Vector<AtlasGroupInfo>& groups, const AtlasFilter& filter);

    /// FNV-1a over fixed bytes, never `std::hash`: a key compared across frames and pinned by a test must not move with the standard library
    class KeyHasher {
    public:
        void AddBytes(const void* data, size_t size);
        /// Byte by byte, low first, so the key does not depend on the machine's byte order
        void AddNumber(uint64_t value);
        /// The length goes first, so "ab" + "c" and "a" + "bc" differ
        void AddString(StringView text);
        /// The bits of the four floats
        void AddRect(const Rect& rect);

        [[nodiscard]] uint64_t Value() const {
            return _value;
        }

    private:
        uint64_t _value = StringUtils::hashOffset;
    };

    /**
     * @brief A hash of what the left list shows, in order: per sprite the name, the texture path, the group name and the packed flag.
     *
     * Not the rects: an edited rect changes neither the list nor the groups, so the panel compares this every frame and rebuilds only when it moved.
     */
    [[nodiscard]] uint64_t LibraryListKey(const TextureLibrary& library);

    /// The part of @p texturePath after the last '/' (all of it without one); empty for an empty path
    [[nodiscard]] StringView TextureFileName(PoolString texturePath);

    struct ThumbnailUv {
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
    };

    /// @p srcRect over the texture's size, clamped to 0..1. An empty rect is the whole texture, as the renderer draws it; so is a size that is not positive.
    [[nodiscard]] ThumbnailUv ComputeThumbnailUv(const Rect& srcRect, float textureWidth, float textureHeight);

    struct FitSize {
        float w = 0.0f;
        float h = 0.0f;
    };

    /// The size scaled into the box with its aspect kept; without @p allowUpscale never above 1:1. Zero or negative input gives 0x0.
    [[nodiscard]] FitSize FitInside(float width, float height, float maxWidth, float maxHeight, bool allowUpscale);

}  // namespace BECore
