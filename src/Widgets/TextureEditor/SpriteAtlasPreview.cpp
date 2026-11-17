#include "SpriteAtlasPreview.h"

#include <Widgets/TextureEditor/SpriteLibraryView.h>
#include <cmath>

namespace BECore {

    namespace {

        uint64_t AreaOf(const Rect& rect) {
            return static_cast<uint64_t>(std::llround(std::max(rect.w, 0.0f))) * static_cast<uint64_t>(std::llround(std::max(rect.h, 0.0f)));
        }

        bool IsInside(const Rect& rect, float width, float height) {
            return rect.x >= 0.0f && rect.y >= 0.0f && rect.x + rect.w <= width && rect.y + rect.h <= height;
        }

        void Finish(AtlasPreview& preview) {
            const float width = static_cast<float>(preview.width);
            const float height = static_cast<float>(preview.height);
            for (const AtlasPreviewRect& item : preview.rects) {
                preview.usedPixels += AreaOf(item.rect);
                if (!IsInside(item.rect, width, height)) {
                    ++preview.outside;
                }
            }
            const uint64_t area = static_cast<uint64_t>(preview.width) * preview.height;
            preview.fillRatio = area == 0 ? 0.0f : std::clamp(static_cast<float>(static_cast<double>(preview.usedPixels) / static_cast<double>(area)), 0.0f, 1.0f);
        }

    }  // namespace

    AtlasPreview PreviewPackedAtlas(const TextureLibrary& library, StringView atlasName, float textureWidth, float textureHeight) {
        AtlasPreview preview;
        preview.texturePath = AtlasTexturePath(atlasName);
        preview.width = textureWidth > 0.0f ? static_cast<uint32_t>(std::llround(textureWidth)) : 0;
        preview.height = textureHeight > 0.0f ? static_cast<uint32_t>(std::llround(textureHeight)) : 0;
        for (const PoolString name : AtlasMembers(library, atlasName)) {
            const SpriteEntry& entry = *library.GetSprite(name);
            if (!IsPackedSprite(entry) || entry.texturePath != preview.texturePath) {
                ++preview.hidden;
                continue;
            }
            AtlasPreviewRect item;
            item.name = name;
            item.rect = entry.srcRect;
            item.imagePath = entry.texturePath;
            item.imageRect = entry.srcRect;
            preview.rects.push_back(item);
        }
        Finish(preview);
        return preview;
    }

    std::expected<AtlasPreview, String> PreviewAtlasPlan(const TextureLibrary& library, StringView atlasName, const PackSettings& settings, AtlasSizeLookup sizeOf) {
        auto plan = PlanAtlas(library, atlasName, settings, sizeOf);
        if (!plan) {
            return std::unexpected(plan.error());
        }
        AtlasPreview preview;
        preview.texturePath = AtlasTexturePath(atlasName);
        preview.width = plan->width;
        preview.height = plan->height;
        preview.rects.reserve(plan->members.size());
        for (const PlannedMember& member : plan->members) {
            AtlasPreviewRect item;
            item.name = member.name;
            item.rect = Rect(static_cast<float>(member.at.x), static_cast<float>(member.at.y), static_cast<float>(member.crop.width), static_cast<float>(member.crop.height));
            item.imagePath = member.sourcePath;
            item.imageRect = member.sourceRect;
            preview.rects.push_back(item);
        }
        Finish(preview);
        return preview;
    }

    int HitTestPreview(const AtlasPreview& preview, float x, float y) {
        for (int i = static_cast<int>(preview.rects.size()) - 1; i >= 0; --i) {
            const Rect& rect = preview.rects[static_cast<size_t>(i)].rect;
            if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) {
                return i;
            }
        }
        return -1;
    }

    uint64_t AtlasPreviewKey(const TextureLibrary& library, StringView atlasName) {
        KeyHasher hasher;
        const Vector<PoolString> members = AtlasMembers(library, atlasName);
        hasher.AddNumber(members.size());
        for (const PoolString name : members) {
            const SpriteEntry& entry = *library.GetSprite(name);
            hasher.AddString(name.ToStringView());
            hasher.AddString(entry.texturePath.ToStringView());
            hasher.AddRect(entry.srcRect);
            const bool packed = IsPackedSprite(entry);
            hasher.AddNumber(packed ? 1 : 0);
            if (packed) {
                hasher.AddString(entry.atlas->source->texturePath.ToStringView());
                hasher.AddRect(entry.atlas->source->rect);
            }
        }
        return hasher.Value();
    }

}  // namespace BECore
