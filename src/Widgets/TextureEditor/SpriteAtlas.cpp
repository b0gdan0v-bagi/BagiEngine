#include "SpriteAtlas.h"

#include <EASTL/sort.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr StringView kAtlasExtension = ".png";
        constexpr float kRectTolerance = 0.001f;
        constexpr float kHalfPixel = 0.5f;

        /// Where a member's pixels come from
        struct MemberSource {
            PoolString texturePath;
            Rect rect;
        };

        bool IsInGroup(const SpriteEntry& entry, StringView atlasName) {
            return !atlasName.empty() && entry.atlas.has_value() && entry.atlas->name.ToStringView() == atlasName;
        }

        MemberSource SourceOf(const SpriteEntry& entry) {
            if (IsPackedSprite(entry)) {
                return {entry.atlas->source->texturePath, entry.atlas->source->rect};
            }
            return {entry.texturePath, entry.srcRect};
        }

        std::expected<void, String> CheckAtlasName(StringView atlasName) {
            if (!IsValidSpriteName(atlasName)) {
                return std::unexpected(Format("'{}' is not an atlas name: use 1 to {} letters, digits, _ or -", atlasName, kMaxSpriteNameLength));
            }
            return {};
        }

        int64_t RoundEdge(float value) {
            return static_cast<int64_t>(std::floor(value + kHalfPixel));
        }

        std::expected<AtlasCrop, String> CropOf(PoolString spriteName, const Rect& rect, const ImageSize& image) {
            const float imageWidth = static_cast<float>(image.width);
            const float imageHeight = static_cast<float>(image.height);
            if (rect.x < -kRectTolerance || rect.y < -kRectTolerance || rect.x + rect.w > imageWidth + kRectTolerance || rect.y + rect.h > imageHeight + kRectTolerance) {
                return std::unexpected(Format("'{}': the rect {}, {}, {}, {} is not inside its {}x{} image", spriteName.ToStringView(), rect.x, rect.y, rect.w, rect.h, image.width, image.height));
            }

            const int64_t maxX = static_cast<int64_t>(image.width);
            const int64_t maxY = static_cast<int64_t>(image.height);
            // A side that rounds to nothing still takes one pixel, moved inside the image when the rect sits on its edge
            const int64_t left = std::clamp<int64_t>(RoundEdge(rect.x), 0, maxX - 1);
            const int64_t top = std::clamp<int64_t>(RoundEdge(rect.y), 0, maxY - 1);
            const int64_t right = std::clamp<int64_t>(RoundEdge(rect.x + rect.w), left + 1, maxX);
            const int64_t bottom = std::clamp<int64_t>(RoundEdge(rect.y + rect.h), top + 1, maxY);
            return AtlasCrop{static_cast<uint32_t>(left), static_cast<uint32_t>(top), static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top)};
        }

        void CopyCrop(const RgbaImage& source, const AtlasCrop& crop, RgbaImage& atlas, const PackedRect& at) {
            const size_t rowBytes = static_cast<size_t>(crop.width) * kBytesPerPixel;
            for (uint32_t row = 0; row < crop.height; ++row) {
                const size_t from = (static_cast<size_t>(crop.y + row) * source.width + crop.x) * kBytesPerPixel;
                const size_t to = (static_cast<size_t>(at.y + row) * atlas.width + at.x) * kBytesPerPixel;
                std::copy_n(source.pixels.data() + from, rowBytes, atlas.pixels.data() + to);
            }
        }

    }  // namespace

    PoolString AtlasTexturePath(StringView atlasName) {
        return PoolString::Intern(Format("assets/{}/{}{}", kAtlasAssetsFolder, atlasName, kAtlasExtension));
    }

    std::expected<bool, String> SetSpriteAtlas(TextureLibrary& library, PoolString spriteName, StringView atlasName) {
        const SpriteEntry* found = library.GetSprite(spriteName);
        if (found == nullptr) {
            return std::unexpected(Format("there is no sprite '{}'", spriteName.ToStringView()));
        }
        if (IsPackedSprite(*found)) {
            return std::unexpected(Format("'{}' is packed on '{}': unpack first", spriteName.ToStringView(), found->atlas->name.ToStringView()));
        }
        if (!atlasName.empty()) {
            if (auto valid = CheckAtlasName(atlasName); !valid) {
                return std::unexpected(valid.error());
            }
        }

        const bool inGroup = found->atlas.has_value();
        if (atlasName.empty() ? !inGroup : IsInGroup(*found, atlasName)) {
            return false;
        }

        SpriteEntry changed = *found;
        if (atlasName.empty()) {
            changed.atlas.reset();
        } else {
            SpriteAtlasLink link;
            link.name = PoolString::Intern(atlasName);
            changed.atlas = link;
        }
        library.AddOrUpdate(eastl::move(changed));
        return true;
    }

    Vector<PoolString> AtlasMembers(const TextureLibrary& library, StringView atlasName) {
        Vector<PoolString> members;
        for (const SpriteEntry& entry : library.GetAll()) {
            if (IsInGroup(entry, atlasName)) {
                members.push_back(entry.name);
            }
        }
        eastl::sort(members.begin(), members.end(), [](const PoolString& a, const PoolString& b) { return a.ToStringView() < b.ToStringView(); });
        return members;
    }

    AtlasCounts AtlasSummary(const TextureLibrary& library, StringView atlasName) {
        AtlasCounts counts;
        for (const SpriteEntry& entry : library.GetAll()) {
            if (IsInGroup(entry, atlasName)) {
                ++counts.members;
                if (IsPackedSprite(entry)) {
                    ++counts.packed;
                }
            }
        }
        return counts;
    }

    std::expected<PlannedAtlas, String> PlanAtlas(const TextureLibrary& library, StringView atlasName, const PackSettings& settings, AtlasSizeLookup sizeOf) {
        if (auto valid = CheckAtlasName(atlasName); !valid) {
            return std::unexpected(valid.error());
        }
        const Vector<PoolString> names = AtlasMembers(library, atlasName);
        if (names.empty()) {
            return std::unexpected(Format("no sprite belongs to the atlas '{}'", atlasName));
        }

        // Each source file is asked for its size once, however many members cut from it
        UnorderedPoolMap<ImageSize> sizeOfPath;
        PlannedAtlas plan;
        plan.members.reserve(names.size());
        Vector<PackSize> sizes;
        sizes.reserve(names.size());

        for (const PoolString name : names) {
            const MemberSource source = SourceOf(*library.GetSprite(name));
            if (source.texturePath.Empty()) {
                return std::unexpected(Format("'{}' has no texture", name.ToStringView()));
            }
            if (source.rect.IsEmpty()) {
                return std::unexpected(Format("'{}' has an empty rect", name.ToStringView()));
            }

            auto known = sizeOfPath.find(source.texturePath);
            if (known == sizeOfPath.end()) {
                auto looked = sizeOf(source.texturePath);
                if (!looked) {
                    return std::unexpected(Format("'{}': cannot read {}: {}", name.ToStringView(), source.texturePath.ToStringView(), looked.error()));
                }
                known = sizeOfPath.emplace(source.texturePath, *looked).first;
            }

            auto crop = CropOf(name, source.rect, known->second);
            if (!crop) {
                return std::unexpected(crop.error());
            }
            sizes.push_back(PackSize{crop->width, crop->height});
            PlannedMember member;
            member.name = name;
            member.sourcePath = source.texturePath;
            member.sourceRect = source.rect;
            member.crop = *crop;
            plan.members.push_back(member);
        }

        auto layout = PackSizes(sizes, settings);
        if (!layout) {
            return std::unexpected(String(AtlasPackErrorText(layout.error())));
        }
        plan.width = layout->width;
        plan.height = layout->height;
        for (size_t i = 0; i < plan.members.size(); ++i) {
            plan.members[i].at = layout->placements[i];
        }
        return plan;
    }

    std::expected<BuiltAtlas, String> BuildAtlas(const TextureLibrary& library, StringView atlasName, const PackSettings& settings, AtlasImageLoader loader) {
        // The plan asks for each distinct file once; the lookup keeps the image for the pixel copy
        Vector<RgbaImage> images;
        UnorderedPoolMap<size_t> imageOf;
        auto sizeOf = [&](PoolString path) -> std::expected<ImageSize, String> {
            auto loaded = loader(path);
            if (!loaded) {
                return std::unexpected(loaded.error());
            }
            const ImageSize size{loaded->width, loaded->height};
            imageOf[path] = images.size();
            images.push_back(eastl::move(*loaded));
            return size;
        };
        auto plan = PlanAtlas(library, atlasName, settings, sizeOf);
        if (!plan) {
            return std::unexpected(plan.error());
        }

        BuiltAtlas built;
        built.image.width = plan->width;
        built.image.height = plan->height;
        built.image.pixels.assign(static_cast<size_t>(plan->width) * plan->height * kBytesPerPixel, 0);
        built.entries.reserve(plan->members.size());

        const PoolString atlasPath = AtlasTexturePath(atlasName);
        for (const PlannedMember& member : plan->members) {
            CopyCrop(images[imageOf[member.sourcePath]], member.crop, built.image, member.at);

            SpriteEntry entry = *library.GetSprite(member.name);
            SpriteSource kept;
            kept.texturePath = member.sourcePath;
            kept.rect = member.sourceRect;
            entry.atlas->source = kept;
            entry.texturePath = atlasPath;
            entry.srcRect = Rect(static_cast<float>(member.at.x), static_cast<float>(member.at.y), static_cast<float>(member.crop.width), static_cast<float>(member.crop.height));
            built.entries.push_back(eastl::move(entry));
        }
        return built;
    }

    std::expected<AtlasWritten, String> WriteAtlas(TextureLibrary& library, StringView atlasName, const PackSettings& settings, const AtlasIo& io) {
        auto built = BuildAtlas(library, atlasName, settings, [&io](PoolString path) { return LoadRgbaImage(io.resolve(path.ToStringView())); });
        if (!built) {
            return std::unexpected(built.error());
        }

        const std::filesystem::path folder = io.assetsDir / ToStd(kAtlasAssetsFolder);
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (error) {
            return std::unexpected(Format("cannot create the folder {}: {}", folder.generic_string(), error.message()));
        }
        const String fileName = Format("{}{}", atlasName, kAtlasExtension);
        if (auto written = ReplaceRgbaPng(folder / ToStd(fileName), built->image); !written) {
            return std::unexpected(written.error());
        }

        // The file is on disk: from here the library follows it
        AtlasWritten result;
        result.texturePath = AtlasTexturePath(atlasName);
        result.width = built->image.width;
        result.height = built->image.height;
        for (SpriteEntry& entry : built->entries) {
            result.changed.push_back(entry.name);
            library.AddOrUpdate(eastl::move(entry));
        }
        return result;
    }

    std::expected<AtlasUnpacked, String> UnpackAtlas(TextureLibrary& library, StringView atlasName, FunctionRef<bool(PoolString path)> fileExists) {
        const Vector<PoolString> members = AtlasMembers(library, atlasName);
        Vector<SpriteEntry> restored;
        AtlasUnpacked result;
        for (const PoolString name : members) {
            SpriteEntry entry = *library.GetSprite(name);
            if (!IsPackedSprite(entry)) {
                continue;
            }
            const SpriteSource source = *entry.atlas->source;
            if (!fileExists(source.texturePath)) {
                return std::unexpected(Format("'{}': the source {} is gone", name.ToStringView(), source.texturePath.ToStringView()));
            }
            if (std::find(result.oldTextures.begin(), result.oldTextures.end(), entry.texturePath) == result.oldTextures.end()) {
                result.oldTextures.push_back(entry.texturePath);
            }
            entry.texturePath = source.texturePath;
            entry.srcRect = source.rect;
            entry.atlas->source.reset();
            result.changed.push_back(name);
            restored.push_back(eastl::move(entry));
        }
        if (restored.empty()) {
            return std::unexpected(Format("no sprite of the atlas '{}' is packed", atlasName));
        }

        for (SpriteEntry& entry : restored) {
            library.AddOrUpdate(eastl::move(entry));
        }
        return result;
    }

}  // namespace BECore
