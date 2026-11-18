#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Types/Types.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <cstdint>
#include <expected>

namespace BECore {

    struct AtlasPreviewRect {
        PoolString name;
        Rect rect;             // the sprite's place in the atlas, in atlas pixels
        PoolString imagePath;  // which pixels fill `rect` when it is drawn: the atlas file itself, or the member's source
        Rect imageRect;        // the part of `imagePath` that fills it
    };

    struct AtlasPreview {
        PoolString texturePath;  // the atlas file
        uint32_t width = 0;
        uint32_t height = 0;
        Vector<AtlasPreviewRect> rects;  // in member order
        uint64_t usedPixels = 0;         // the sum of the rect areas; overlaps are not looked for, the packer makes none
        float fillRatio = 0.0f;          // `usedPixels` over the atlas area, 0..1; 0 for no area
        uint32_t outside = 0;            // rects that are not wholly inside the picture
        uint32_t hidden = 0;             // group members not shown (not packed, or packed to another file)
    };

    /// The packed members of group @p atlasName that sit on its atlas file, at their `srcRect`; the picture's size is the file's, given by the caller
    [[nodiscard]] AtlasPreview PreviewPackedAtlas(const TextureLibrary& library, StringView atlasName, float textureWidth, float textureHeight);

    /// What Pack would make with @p settings, from the sources: `PlanAtlas` in the shape of `PreviewPackedAtlas`, so one drawing serves both. No pixel is read; the errors are `BuildAtlas`'s.
    [[nodiscard]] std::expected<AtlasPreview, String> PreviewAtlasPlan(const TextureLibrary& library, StringView atlasName, const PackSettings& settings, AtlasSizeLookup sizeOf);

    /// The index of the last rect that holds the point (atlas pixels; the left and top edges are in, the right and bottom out), -1 for none
    [[nodiscard]] int HitTestPreview(const AtlasPreview& preview, float x, float y);

    /// A hash of what the preview of group @p atlasName shows: per member the name, texture, rect, packed flag and source. Another group's change does not move it. Allocates nothing, so the panel can
    /// ask every frame.
    [[nodiscard]] uint64_t AtlasPreviewKey(const TextureLibrary& library, StringView atlasName);

}  // namespace BECore
