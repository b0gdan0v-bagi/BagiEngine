#include "SpriteLibraryView.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <bit>

namespace BECore {

    namespace {

        constexpr size_t kBitsPerByte = 8;
        constexpr uint64_t kByteMask = 0xFF;

        PoolString GroupNameOf(const SpriteEntry& entry) {
            return entry.atlas.has_value() ? entry.atlas->name : PoolString();
        }

    }  // namespace

    SpriteAtlasState AtlasStateOf(const SpriteEntry& entry) {
        if (IsPackedSprite(entry)) {
            return SpriteAtlasState::Packed;
        }
        return GroupNameOf(entry).Empty() ? SpriteAtlasState::Single : SpriteAtlasState::Grouped;
    }

    Vector<AtlasGroupInfo> ListAtlasGroups(const TextureLibrary& library) {
        Vector<AtlasGroupInfo> groups;
        for (const SpriteEntry& entry : library.GetAll()) {
            const PoolString name = GroupNameOf(entry);
            if (name.Empty()) {
                continue;
            }
            auto found = eastl::find_if(groups.begin(), groups.end(), [name](const AtlasGroupInfo& group) { return group.name == name; });
            if (found == groups.end()) {
                AtlasGroupInfo info;
                info.name = name;
                groups.push_back(info);
                found = groups.end() - 1;
            }
            ++found->members;
            if (IsPackedSprite(entry)) {
                ++found->packed;
            }
        }
        eastl::sort(groups.begin(), groups.end(), [](const AtlasGroupInfo& a, const AtlasGroupInfo& b) { return a.name.ToStringView() < b.name.ToStringView(); });
        return groups;
    }

    bool MatchesFilter(const SpriteEntry& entry, const AtlasFilter& filter) {
        switch (filter.kind) {
            case AtlasFilterKind::All:
                return true;
            case AtlasFilterKind::NoAtlas:
                return GroupNameOf(entry).Empty();
            case AtlasFilterKind::Group:
                return GroupNameOf(entry) == filter.group;
        }
        return false;
    }

    Vector<uint32_t> FilterSprites(const TextureLibrary& library, const AtlasFilter& filter) {
        Vector<uint32_t> indices;
        const Vector<SpriteEntry>& all = library.GetAll();
        indices.reserve(all.size());
        for (size_t i = 0; i < all.size(); ++i) {
            if (MatchesFilter(all[i], filter)) {
                indices.push_back(static_cast<uint32_t>(i));
            }
        }
        return indices;
    }

    AtlasFilter NormalizeFilter(const Vector<AtlasGroupInfo>& groups, const AtlasFilter& filter) {
        if (filter.kind != AtlasFilterKind::Group) {
            return filter;
        }
        const bool known = eastl::any_of(groups.begin(), groups.end(), [&filter](const AtlasGroupInfo& group) { return group.name == filter.group; });
        return known ? filter : AtlasFilter{};
    }

    void KeyHasher::AddBytes(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            _value ^= bytes[i];
            _value *= StringUtils::hashPrime;
        }
    }

    void KeyHasher::AddNumber(uint64_t value) {
        for (size_t shift = 0; shift < sizeof(value) * kBitsPerByte; shift += kBitsPerByte) {
            _value ^= (value >> shift) & kByteMask;
            _value *= StringUtils::hashPrime;
        }
    }

    void KeyHasher::AddString(StringView text) {
        AddNumber(text.size());
        AddBytes(text.data(), text.size());
    }

    void KeyHasher::AddRect(const Rect& rect) {
        AddNumber(std::bit_cast<uint32_t>(rect.x));
        AddNumber(std::bit_cast<uint32_t>(rect.y));
        AddNumber(std::bit_cast<uint32_t>(rect.w));
        AddNumber(std::bit_cast<uint32_t>(rect.h));
    }

    uint64_t LibraryListKey(const TextureLibrary& library) {
        KeyHasher hasher;
        for (const SpriteEntry& entry : library.GetAll()) {
            hasher.AddString(entry.name.ToStringView());
            hasher.AddString(entry.texturePath.ToStringView());
            hasher.AddString(GroupNameOf(entry).ToStringView());
            hasher.AddNumber(IsPackedSprite(entry) ? 1 : 0);
        }
        return hasher.Value();
    }

    StringView TextureFileName(PoolString texturePath) {
        const StringView path = texturePath.ToStringView();
        const size_t slash = path.rfind('/');
        return slash == StringView::npos ? path : path.substr(slash + 1);
    }

    ThumbnailUv ComputeThumbnailUv(const Rect& srcRect, float textureWidth, float textureHeight) {
        if (srcRect.IsEmpty() || textureWidth <= 0.0f || textureHeight <= 0.0f) {
            return {};
        }
        ThumbnailUv uv;
        uv.u0 = std::clamp(srcRect.x / textureWidth, 0.0f, 1.0f);
        uv.v0 = std::clamp(srcRect.y / textureHeight, 0.0f, 1.0f);
        uv.u1 = std::clamp((srcRect.x + srcRect.w) / textureWidth, 0.0f, 1.0f);
        uv.v1 = std::clamp((srcRect.y + srcRect.h) / textureHeight, 0.0f, 1.0f);
        return uv;
    }

    FitSize FitInside(float width, float height, float maxWidth, float maxHeight, bool allowUpscale) {
        if (width <= 0.0f || height <= 0.0f || maxWidth <= 0.0f || maxHeight <= 0.0f) {
            return {};
        }
        float scale = std::min(maxWidth / width, maxHeight / height);
        if (!allowUpscale) {
            scale = std::min(scale, 1.0f);
        }
        return {width * scale, height * scale};
    }

}  // namespace BECore
