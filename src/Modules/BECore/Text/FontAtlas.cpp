#include "FontAtlas.h"

#include <BECore/Memory/MemTag.h>
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

        uint32_t PaddedWidth(const GlyphBox& box) {
            return box.width + 2 * FontAtlas::kPadding;
        }
        uint32_t PaddedHeight(const GlyphBox& box) {
            return box.height + 2 * FontAtlas::kPadding;
        }

        /// The box of the outline picture of a glyph with @p ink: @p radius pixels more on every side
        GlyphBox Grown(const GlyphBox& ink, int radius) {
            const auto grow = static_cast<uint32_t>(radius);
            return {ink.width + 2 * grow, ink.height + 2 * grow, ink.offsetX - radius, ink.offsetY - radius};
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

    bool FontAtlas::PrepareCodePoints(IRenderer& renderer, Span<const GlyphRange> ranges) {
        const ScratchScope scratch;
        size_t count = 0;
        for (const GlyphRange& range : ranges) {
            count += range.last >= range.first ? static_cast<size_t>(range.last - range.first) + 1 : 0;
        }
        ArenaVector<uint32_t> glyphs;
        glyphs.reserve(count);
        for (const GlyphRange& range : ranges) {
            for (char32_t codePoint = range.first; codePoint <= range.last; ++codePoint) {
                if (const uint32_t glyph = _font->FindGlyph(codePoint); glyph != 0) {
                    glyphs.push_back(glyph);
                }
            }
        }
        return Prepare(renderer, Span<const uint32_t>(glyphs.data(), glyphs.size()));
    }

    bool FontAtlas::Prepare(IRenderer& renderer, Span<const uint32_t> glyphs) {
        BE_PROFILE_SCOPE("FontAtlas::Prepare");
        BE_MEM_TAG(Text);
        // The scope comes first, so the containers below die before it
        const ScratchScope scratch;
        ArenaVector<uint32_t> missing;
        missing.reserve(glyphs.size());
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
        _glyphs.reserve(_glyphs.size() + missing.size());

        bool ok = true;
        const auto fontSize = static_cast<float>(_pixelSize);
        ArenaVector<Pending> pending;
        pending.reserve(missing.size());
        for (const uint32_t glyph : missing) {
            const GlyphBox ink = _font->GetGlyphBox(glyph, fontSize);
            if (ink.Empty()) {
                _glyphs[glyph] = AtlasGlyph{};  // no ink: remembered, so it is not drawn again
                continue;
            }
            const GlyphBox picture = _outlinePixels > 0 ? Grown(ink, _outlinePixels) : ink;
            if (PaddedWidth(picture) > _pageSize || PaddedHeight(picture) > _pageSize) {
                LOG_ERROR("FontAtlas: a glyph of '{}' at {} px is {}x{}, larger than a {} px page", _font->GetPath().ToStringView(), _pixelSize, picture.width, picture.height, _pageSize);
                _glyphs[glyph] = AtlasGlyph{};
                ok = false;
            } else {
                pending.push_back({glyph, ink, picture});
            }
        }

        // Tall glyphs first: shelves come out tight
        eastl::sort(pending.begin(), pending.end(), [](const Pending& a, const Pending& b) { return a.picture.height > b.picture.height; });
        const GlyphOutline::Kernel kernel(_outlinePixels);
        BuildPages(renderer, pending, kernel, ok);
        return ok;
    }

    void FontAtlas::BuildPages(IRenderer& renderer, ArenaVector<Pending>& pending, const GlyphOutline::Kernel& kernel, bool& ok) {
        BE_PROFILE_SCOPE("FontAtlas::BuildPages");
        BE_MEM_TAG(Text);
        const auto fontSize = static_cast<float>(_pixelSize);
        while (!pending.empty()) {
            const ScratchScope pageScope;
            // Aim at a page just big enough; grow it while the glyphs do not fit and it is smaller than a full page
            uint64_t area = 0;
            for (const Pending& item : pending) {
                area += static_cast<uint64_t>(PaddedWidth(item.picture)) * PaddedHeight(item.picture);
            }
            const auto wanted = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(area) * kPackingSlack)));
            uint32_t side = std::min(NextPowerOfTwo(wanted), _pageSize);

            ArenaVector<Placed> placed;
            ArenaVector<size_t> left;
            placed.reserve(pending.size());
            left.reserve(pending.size());
            while (true) {
                placed.clear();
                left.clear();
                ShelfPacker packer(side, side);
                for (size_t i = 0; i < pending.size(); ++i) {
                    if (auto slot = packer.Pack(PaddedWidth(pending[i].picture), PaddedHeight(pending[i].picture))) {
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
            Vector<uint8_t> pixels(static_cast<size_t>(width) * height * kBytesPerPixel, kWhite);
            for (size_t i = 3; i < pixels.size(); i += kBytesPerPixel) {
                pixels[i] = 0;  // white, fully transparent: a border that never tints its neighbour
            }
            for (const Placed& place : placed) {
                const Pending& item = pending[place.pending];
                // One glyph at a time: the scratch arena never holds more than the pictures of one
                ScratchScope glyphScope;
                uint8_t* ink = glyphScope.Allocate<uint8_t>(item.ink.PixelCount());
                _font->RasterizeGlyphInto(item.glyph, fontSize, item.ink, Span<uint8_t>(ink, item.ink.PixelCount()));
                const uint8_t* coverage = ink;
                if (_outlinePixels > 0) {
                    uint8_t* grown = glyphScope.Allocate<uint8_t>(item.picture.PixelCount());
                    GlyphOutline::Dilate(Span<const uint8_t>(ink, item.ink.PixelCount()), item.ink.width, item.ink.height, kernel, Span<uint8_t>(grown, item.picture.PixelCount()));
                    coverage = grown;
                }
                for (uint32_t row = 0; row < item.picture.height; ++row) {
                    for (uint32_t column = 0; column < item.picture.width; ++column) {
                        const size_t target = (static_cast<size_t>(place.y + kPadding + row) * width + place.x + kPadding + column) * kBytesPerPixel;
                        pixels[target + 3] = coverage[static_cast<size_t>(row) * item.picture.width + column];
                    }
                }
            }

            IntrusivePtr<ITexture> texture = renderer.CreateTexture(pixels.data(), width, height);
            const auto pageIndex = static_cast<int32_t>(_pages.size());
            const bool uploaded = static_cast<bool>(texture);
            if (uploaded) {
                _pages.push_back(std::move(texture));
            } else {
                LOG_ERROR("FontAtlas: cannot make a {}x{} page for '{}' at {} px", width, height, _font->GetPath().ToStringView(), _pixelSize);
                ok = false;
            }
            for (const Placed& place : placed) {
                const Pending& item = pending[place.pending];
                AtlasGlyph glyph;  // without a page when the upload failed: the glyph stays blank and is not retried
                if (uploaded) {
                    glyph.page = pageIndex;
                    glyph.source =
                        Rect{static_cast<float>(place.x + kPadding), static_cast<float>(place.y + kPadding), static_cast<float>(item.picture.width), static_cast<float>(item.picture.height)};
                    glyph.offsetX = static_cast<float>(item.picture.offsetX);
                    glyph.offsetY = static_cast<float>(item.picture.offsetY);
                }
                _glyphs[item.glyph] = glyph;
            }

            // The glyphs that did not fit wait for the next page, in their order; an index is never below its new place
            for (size_t i = 0; i < left.size(); ++i) {
                pending[i] = pending[left[i]];
            }
            pending.resize(left.size());
        }
    }

}  // namespace BECore
