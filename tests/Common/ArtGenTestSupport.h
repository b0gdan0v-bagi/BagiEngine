#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/Types/Types.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <Widgets/TextureEditor/SpriteImage.h>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <utility>

namespace BECore::Tests {

    constexpr uint8_t kOpaque = 255;
    constexpr uint8_t kLow = 40;
    constexpr uint8_t kHigh = 200;

    /// An opaque picture of one colour
    inline RgbaImage MakeSolid(uint32_t width, uint32_t height, uint8_t red, uint8_t green, uint8_t blue) {
        RgbaImage image;
        image.width = width;
        image.height = height;
        image.pixels.resize(static_cast<size_t>(width) * height * kBytesPerPixel);
        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
            image.pixels[i * kBytesPerPixel + 0] = red;
            image.pixels[i * kBytesPerPixel + 1] = green;
            image.pixels[i * kBytesPerPixel + 2] = blue;
            image.pixels[i * kBytesPerPixel + 3] = kOpaque;
        }
        return image;
    }

    /// True when every pixel of the rect @p at of @p atlas has the colour of a MakeSolid image
    inline bool RectIsSolid(const RgbaImage& atlas, const Rect& at, uint8_t red, uint8_t green, uint8_t blue) {
        for (uint32_t y = static_cast<uint32_t>(at.y); y < static_cast<uint32_t>(at.y + at.h); ++y) {
            for (uint32_t x = static_cast<uint32_t>(at.x); x < static_cast<uint32_t>(at.x + at.w); ++x) {
                const uint8_t* pixel = atlas.pixels.data() + (static_cast<size_t>(y) * atlas.width + x) * kBytesPerPixel;
                if (pixel[0] != red || pixel[1] != green || pixel[2] != blue || pixel[3] != kOpaque) {
                    return false;
                }
            }
        }
        return true;
    }

    /// The sprite "queen" / "rook" of @p library; a blank one and a failure of the case when the library has none
    inline const SpriteEntry& Queen(const TextureLibrary& library) {
        return Require(library.GetSprite("queen"_intern));
    }

    inline const SpriteEntry& Rook(const TextureLibrary& library) {
        return Require(library.GetSprite("rook"_intern));
    }

    /// The picture a packed @p entry is cut from; a blank one and a failure of the case for an entry that has no source
    inline const SpriteSource& SourceOf(const SpriteEntry& entry) {
        return Require(Require(entry.atlas).source);
    }

    /// A game folder with `assets/` in it, removed with the object; virtual paths resolve into it
    class ArtGenGameFolder {
    public:
        /// @p tempPrefix names the folder (`TestTempDirectory`), so two test files never share one
        explicit ArtGenGameFolder(const String& tempPrefix) : _root(TestTempDirectory(tempPrefix)), _resolver{_root} {
            MakeEmptyDirectory(_root / "assets" / "artgen");
        }
        ~ArtGenGameFolder() {
            RemoveDirectory(_root);
        }
        ArtGenGameFolder(const ArtGenGameFolder&) = delete;
        ArtGenGameFolder& operator=(const ArtGenGameFolder&) = delete;

        [[nodiscard]] std::filesystem::path Resolve(StringView virtualPath) const {
            return _resolver(virtualPath);
        }

        /// The `resolve` of the result is a view of this object, which must outlive the call it is given to
        [[nodiscard]] AtlasIo Io() const {
            return AtlasIo{_root / "assets", _resolver};
        }

        void SavePng(StringView virtualPath, const RgbaImage& image) const {
            ASSERT_TRUE(SaveRgbaPng(Resolve(virtualPath), image).has_value());
        }

        /// What CopyVariantIntoAssets would have returned for the picture just saved at @p virtualPath
        [[nodiscard]] ArtGenCopied Copied(StringView virtualPath, const RgbaImage& image) const {
            EXPECT_TRUE(SaveRgbaPng(Resolve(virtualPath), image).has_value());
            ArtGenCopied copied;
            copied.texturePath = String(virtualPath);
            copied.pngFile = Resolve(virtualPath);
            copied.width = image.width;
            copied.height = image.height;
            return copied;
        }

        [[nodiscard]] bool Exists(StringView virtualPath) const {
            return std::filesystem::exists(Resolve(virtualPath));
        }

        [[nodiscard]] RgbaImage Atlas(StringView virtualPath) const {
            auto loaded = LoadRgbaImage(Resolve(virtualPath));
            EXPECT_TRUE(loaded.has_value());
            return loaded ? std::move(*loaded) : RgbaImage{};
        }

    private:
        struct Resolver {
            std::filesystem::path root;

            std::filesystem::path operator()(StringView virtualPath) const {
                return root / ToStd(virtualPath);
            }
        };

        std::filesystem::path _root;
        Resolver _resolver;
    };

}  // namespace BECore::Tests
