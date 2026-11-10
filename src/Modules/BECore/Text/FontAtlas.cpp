#include "FontAtlas.h"

#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Text/GlyphOutline.h>
#include <BECore/Text/ShelfPacker.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        constexpr uint32_t kMinPageSize = 64;
        constexpr uint32_t kBytesPerPixel = 4;
        constexpr uint8_t kWhite = 255;
        constexpr float kPackingSlack = 1.25f;  // the area of a page is this much more than the area of its glyphs

        uint32_t PaddedWidth(const GlyphBitmap& bitmap) {
            return bitmap.width + 2 * FontAtlas::kPadding;
        }
        uint32_t PaddedHeight(const GlyphBitmap& bitmap) {
            return bitmap.height + 2 * FontAtlas::kPadding;
        }

        uint32_t NextPowerOfTwo(uint32_t value) {
            uint32_t result = kMinPageSize;
            while (result < value) {
                result *= 2;
            }
            return result;
        }

    }  // namespace

    FontAtlas::FontAtlas(IntrusivePtr<Font> font, int pixelSize, uint32_t pageSize, int outlinePixels)
        : _font(std::move(font)), _pixelSize(pixelSize), _outlinePixels(std::clamp(outlinePixels, 0, kMaxOutlinePixels)), _pageSize(std::max(pageSize, kMinPageSize)) {}

    const AtlasGlyph* FontAtlas::Find(uint32_t glyph) const {
        const auto it = _glyphs.find(glyph);
        return it == _glyphs.end() ? nullptr : &it->second;
    }

    ITexture* FontAtlas::GetPage(int32_t page) const {
        if (page < 0 || static_cast<size_t>(page) >= _pages.size()) {
            return nullptr;
        }
        return _pages[static_cast<size_t>(page)].Get();
    }

    bool FontAtlas::PrepareCodePoints(IRenderer& renderer, eastl::span<const GlyphRange> ranges) {
        eastl::vector<uint32_t> glyphs;
        for (const GlyphRange& range : ranges) {
            for (char32_t codePoint = range.first; codePoint <= range.last; ++codePoint) {
                if (const uint32_t glyph = _font->FindGlyph(codePoint); glyph != 0) {
                    glyphs.push_back(glyph);
                }
            }
        }
        return Prepare(renderer, glyphs);
    }

    bool FontAtlas::Prepare(IRenderer& renderer, eastl::span<const uint32_t> glyphs) {
        eastl::vector<uint32_t> missing;
        for (const uint32_t glyph : glyphs) {
            if (_glyphs.find(glyph) == _glyphs.end()) {
                missing.push_back(glyph);
            }
        }
        if (missing.empty()) {
            return true;
        }
        eastl::sort(missing.begin(), missing.end());
        missing.erase(eastl::unique(missing.begin(), missing.end()), missing.end());

        bool ok = true;
        eastl::vector<Pending> pending;
        for (const uint32_t glyph : missing) {
            GlyphBitmap bitmap = _font->RasterizeGlyph(glyph, static_cast<float>(_pixelSize));
            if (_outlinePixels > 0) {
                bitmap = GlyphOutline::Dilate(bitmap, _outlinePixels);
            }
            if (bitmap.width == 0 || bitmap.height == 0) {
                _glyphs[glyph] = AtlasGlyph{};  // no ink: remembered, so it is not drawn again
            } else if (PaddedWidth(bitmap) > _pageSize || PaddedHeight(bitmap) > _pageSize) {
                LOG_ERROR("FontAtlas: a glyph of '{}' at {} px is {}x{}, larger than a {} px page"_format(_font->GetPath().ToStringView(), _pixelSize, bitmap.width, bitmap.height, _pageSize));
                _glyphs[glyph] = AtlasGlyph{};
                ok = false;
            } else {
                pending.push_back({glyph, std::move(bitmap)});
            }
        }

        // Tall glyphs first: shelves come out tight
        eastl::sort(pending.begin(), pending.end(), [](const Pending& a, const Pending& b) { return a.bitmap.height > b.bitmap.height; });
        BuildPages(renderer, pending, ok);
        return ok;
    }

    void FontAtlas::BuildPages(IRenderer& renderer, eastl::vector<Pending>& pending, bool& ok) {
        while (!pending.empty()) {
            // Aim at a page just big enough; grow it while the glyphs do not fit and it is smaller than a full page
            uint64_t area = 0;
            for (const Pending& item : pending) {
                area += static_cast<uint64_t>(PaddedWidth(item.bitmap)) * PaddedHeight(item.bitmap);
            }
            const auto wanted = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(area) * kPackingSlack)));
            uint32_t side = std::min(NextPowerOfTwo(wanted), _pageSize);

            eastl::vector<Placed> placed;
            eastl::vector<size_t> left;
            while (true) {
                placed.clear();
                left.clear();
                ShelfPacker packer(side, side);
                for (size_t i = 0; i < pending.size(); ++i) {
                    if (auto slot = packer.Pack(PaddedWidth(pending[i].bitmap), PaddedHeight(pending[i].bitmap))) {
                        placed.push_back({i, slot->x, slot->y});
                    } else {
                        left.push_back(i);
                    }
                }
                if (left.empty() || side >= _pageSize) {
                    break;
                }
                side = std::min(side * 2, _pageSize);
            }

            const uint32_t width = side;
            const uint32_t height = side;
            eastl::vector<uint8_t> pixels(static_cast<size_t>(width) * height * kBytesPerPixel, kWhite);
            for (size_t i = 3; i < pixels.size(); i += kBytesPerPixel) {
                pixels[i] = 0;  // white, fully transparent: a border that never tints its neighbour
            }
            for (const Placed& place : placed) {
                const GlyphBitmap& bitmap = pending[place.pending].bitmap;
                for (uint32_t row = 0; row < bitmap.height; ++row) {
                    for (uint32_t column = 0; column < bitmap.width; ++column) {
                        const size_t target = (static_cast<size_t>(place.y + kPadding + row) * width + place.x + kPadding + column) * kBytesPerPixel;
                        pixels[target + 3] = bitmap.coverage[static_cast<size_t>(row) * bitmap.width + column];
                    }
                }
            }

            IntrusivePtr<ITexture> texture = renderer.CreateTexture(pixels.data(), width, height);
            const auto pageIndex = static_cast<int32_t>(_pages.size());
            const bool uploaded = static_cast<bool>(texture);
            if (uploaded) {
                _pages.push_back(std::move(texture));
            } else {
                LOG_ERROR("FontAtlas: cannot make a {}x{} page for '{}' at {} px"_format(width, height, _font->GetPath().ToStringView(), _pixelSize));
                ok = false;
            }
            for (const Placed& place : placed) {
                const Pending& item = pending[place.pending];
                AtlasGlyph glyph;  // without a page when the upload failed: the glyph stays blank and is not retried
                if (uploaded) {
                    glyph.page = pageIndex;
                    glyph.source = Rect{static_cast<float>(place.x + kPadding), static_cast<float>(place.y + kPadding), static_cast<float>(item.bitmap.width), static_cast<float>(item.bitmap.height)};
                    glyph.offsetX = static_cast<float>(item.bitmap.offsetX);
                    glyph.offsetY = static_cast<float>(item.bitmap.offsetY);
                }
                _glyphs[item.glyph] = glyph;
            }

            eastl::vector<Pending> remainder;
            for (const size_t index : left) {
                remainder.push_back(std::move(pending[index]));
            }
            pending = std::move(remainder);
        }
    }

}  // namespace BECore
