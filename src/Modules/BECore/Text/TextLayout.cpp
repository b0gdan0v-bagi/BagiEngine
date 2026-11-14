#include "TextLayout.h"

#include <BECore/Text/Font.h>
#include <BECore/Text/Utf8.h>

namespace BECore {

    namespace {

        constexpr float kEpsilon = 0.01f;
        constexpr int kTabWidthInSpaces = 4;
        constexpr char32_t kEllipsis = 0x2026;
        constexpr int kDotsInAnEllipsis = 3;

        struct Item {
            char32_t codePoint = 0;
            uint32_t glyph = 0;
            float advance = 0.0f;
            bool space = false;
            bool newline = false;
        };

        struct Line {
            Vector<Item> items;  ///< what is drawn: no trailing spaces
            float width = 0.0f;
        };

        float SumAdvances(const Vector<Item>& items) {
            float width = 0.0f;
            for (const Item& item : items) {
                width += item.advance;
            }
            return width;
        }

        Vector<Item> BuildItems(const Font& font, StringView text, float fontSize) {
            Vector<Item> items;
            const uint32_t spaceGlyph = font.FindGlyph(U' ');
            const float spaceAdvance = font.GetAdvance(spaceGlyph, fontSize);
            size_t pos = 0;
            while (pos < text.size()) {
                const char32_t codePoint = Utf8::DecodeNext(text, pos);
                if (codePoint == U'\r') {
                    continue;
                }
                Item item;
                item.codePoint = codePoint;
                if (codePoint == U'\n') {
                    item.newline = true;
                } else if (codePoint == U'\t') {
                    item.space = true;
                    item.glyph = spaceGlyph;
                    item.advance = spaceAdvance * static_cast<float>(kTabWidthInSpaces);
                } else {
                    item.space = codePoint == U' ';
                    item.glyph = font.ResolveGlyph(codePoint);
                    item.advance = font.GetAdvance(item.glyph, fontSize);
                }
                items.push_back(item);
            }
            // Kerning moves the second glyph of a pair, so it adds to the advance of the first
            for (size_t i = 0; i + 1 < items.size(); ++i) {
                if (!items[i].newline && !items[i + 1].newline) {
                    items[i].advance += font.GetKerning(items[i].glyph, items[i + 1].glyph, fontSize);
                }
            }
            return items;
        }

        /// Cuts the items into lines; spaces at a break and at the end of a line are not part of the line
        Vector<Line> BreakIntoLines(const Vector<Item>& items, float maxWidth, bool wrap) {
            Vector<Line> lines;
            const size_t count = items.size();
            if (count == 0) {
                return lines;
            }
            const bool limited = wrap && maxWidth > 0.0f;

            const auto finishLine = [&](size_t begin, size_t end) {
                while (end > begin && items[end - 1].space) {
                    --end;
                }
                Line line;
                line.items.assign(items.begin() + static_cast<ptrdiff_t>(begin), items.begin() + static_cast<ptrdiff_t>(end));
                line.width = SumAdvances(line.items);
                lines.push_back(std::move(line));
            };

            size_t begin = 0;
            while (true) {
                float x = 0.0f;
                bool seenInk = false;
                size_t lastSpace = count;  // a space after some ink on this line: a place where the line may break
                size_t pos = begin;
                bool hardBreak = false;
                bool softBreak = false;
                size_t next = count;

                for (; pos < count; ++pos) {
                    const Item& item = items[pos];
                    if (item.newline) {
                        hardBreak = true;
                        next = pos + 1;
                        break;
                    }
                    if (limited && !item.space && seenInk && x + item.advance > maxWidth + kEpsilon) {
                        softBreak = true;
                        if (lastSpace != count) {
                            next = lastSpace + 1;
                            while (next < count && items[next].space) {
                                ++next;
                            }
                            pos = lastSpace;
                        } else {
                            next = pos;  // one word that is too wide: break inside it
                        }
                        break;
                    }
                    if (item.space) {
                        if (seenInk) {
                            lastSpace = pos;
                        }
                    } else {
                        seenInk = true;
                    }
                    x += item.advance;
                }

                finishLine(begin, pos);
                if (hardBreak) {
                    begin = next;
                    if (begin >= count) {
                        finishLine(count, count);  // the text ends with a newline: an empty last line
                        break;
                    }
                } else if (softBreak) {
                    begin = next;
                } else {
                    break;
                }
            }
            return lines;
        }

        /// The glyphs of an ellipsis: U+2026, or three dots when the font has no such character
        Vector<Item> BuildEllipsis(const Font& font, float fontSize) {
            Vector<Item> ellipsis;
            Item item;
            if (const uint32_t glyph = font.FindGlyph(kEllipsis); glyph != 0) {
                item.codePoint = kEllipsis;
                item.glyph = glyph;
                item.advance = font.GetAdvance(glyph, fontSize);
                ellipsis.push_back(item);
                return ellipsis;
            }
            item.codePoint = U'.';
            item.glyph = font.ResolveGlyph(U'.');
            item.advance = font.GetAdvance(item.glyph, fontSize);
            for (int i = 0; i < kDotsInAnEllipsis; ++i) {
                ellipsis.push_back(item);
            }
            return ellipsis;
        }

        /// Takes characters off the end of the line until the ellipsis fits behind it
        void EndWithEllipsis(Line& line, const Vector<Item>& ellipsis, float maxWidth) {
            const float ellipsisWidth = SumAdvances(ellipsis);
            while (!line.items.empty() && (line.width + ellipsisWidth > maxWidth + kEpsilon || line.items.back().space)) {
                line.width -= line.items.back().advance;
                line.items.pop_back();
            }
            for (const Item& item : ellipsis) {
                line.items.push_back(item);
            }
            line.width = SumAdvances(line.items);
        }

        struct Laid {
            Vector<Line> lines;
            bool truncated = false;
        };

        Laid LayoutLines(const Font& font, StringView text, float fontSize, const TextLayoutOptions& options, bool ellipsize) {
            Laid laid;
            laid.lines = BreakIntoLines(BuildItems(font, text, fontSize), options.maxWidth, options.wrap);
            if (!ellipsize) {
                return laid;
            }

            const FontVerticalMetrics metrics = font.GetVerticalMetrics(fontSize);
            const float lineHeight = metrics.LineHeight() * options.lineSpacing;
            size_t maxLines = laid.lines.size();
            if (options.maxHeight > 0.0f && lineHeight > 0.0f) {
                maxLines = std::max<size_t>(1, static_cast<size_t>((options.maxHeight + kEpsilon) / lineHeight));
            }
            const bool droppedLines = laid.lines.size() > maxLines;
            if (droppedLines) {
                laid.lines.resize(maxLines);
            }

            const Vector<Item> ellipsis = BuildEllipsis(font, fontSize);
            for (size_t i = 0; i < laid.lines.size(); ++i) {
                Line& line = laid.lines[i];
                const bool tooWide = options.maxWidth > 0.0f && line.width > options.maxWidth + kEpsilon;
                const bool lastOfCutText = droppedLines && i + 1 == laid.lines.size();
                if (tooWide || lastOfCutText) {
                    EndWithEllipsis(line, ellipsis, options.maxWidth > 0.0f ? options.maxWidth : line.width + SumAdvances(ellipsis));
                    laid.truncated = true;
                }
            }
            return laid;
        }

        bool Fits(const Laid& laid, float lineHeight, const TextLayoutOptions& options) {
            for (const Line& line : laid.lines) {
                if (options.maxWidth > 0.0f && line.width > options.maxWidth + kEpsilon) {
                    return false;
                }
            }
            return options.maxHeight <= 0.0f || static_cast<float>(laid.lines.size()) * lineHeight <= options.maxHeight + kEpsilon;
        }

        TextLayoutResult Assemble(const Font& font, Laid laid, float fontSize, const TextLayoutOptions& options) {
            TextLayoutResult result;
            const FontVerticalMetrics metrics = font.GetVerticalMetrics(fontSize);
            result.fontSize = fontSize;
            result.lineHeight = metrics.LineHeight() * options.lineSpacing;
            result.truncated = laid.truncated;
            result.height = static_cast<float>(laid.lines.size()) * result.lineHeight;
            for (const Line& line : laid.lines) {
                result.width = std::max(result.width, line.width);
            }

            const float alignWidth = options.maxWidth > 0.0f ? options.maxWidth : result.width;
            float baseline = metrics.ascent;
            for (const Line& line : laid.lines) {
                TextLine textLine;
                textLine.firstGlyph = static_cast<uint32_t>(result.glyphs.size());
                textLine.width = line.width;
                textLine.baseline = baseline;
                switch (options.align) {
                    case TextAlign::Left:
                        textLine.x = 0.0f;
                        break;
                    case TextAlign::Center:
                        textLine.x = (alignWidth - line.width) * 0.5f;
                        break;
                    case TextAlign::Right:
                        textLine.x = alignWidth - line.width;
                        break;
                }
                float pen = textLine.x;
                for (const Item& item : line.items) {
                    result.glyphs.push_back({item.codePoint, item.glyph, pen, baseline});
                    pen += item.advance;
                }
                textLine.glyphCount = static_cast<uint32_t>(result.glyphs.size()) - textLine.firstGlyph;
                result.lines.push_back(textLine);
                baseline += result.lineHeight;
            }

            result.overflowed = (options.maxWidth > 0.0f && result.width > options.maxWidth + kEpsilon) || (options.maxHeight > 0.0f && result.height > options.maxHeight + kEpsilon);
            return result;
        }

    }  // namespace

    TextLayoutResult TextLayout::Layout(const Font& font, StringView utf8, const TextLayoutOptions& options) {
        BE_PROFILE_SCOPE("TextLayout::Layout");
        if (font.GetState() != ResourceState::Loaded || options.fontSize <= 0.0f) {
            return {};
        }

        float fontSize = options.fontSize;
        bool ellipsize = options.overflow == TextOverflow::Ellipsis;

        if (options.overflow == TextOverflow::AutoFit && (options.maxWidth > 0.0f || options.maxHeight > 0.0f)) {
            const auto fitsAt = [&](float size) {
                const float lineHeight = font.GetVerticalMetrics(size).LineHeight() * options.lineSpacing;
                return Fits(LayoutLines(font, utf8, size, options, false), lineHeight, options);
            };
            const float smallest = std::min(options.minFontSize, options.fontSize);
            if (!fitsAt(options.fontSize)) {
                // The largest whole size in [smallest, fontSize) that fits; the sizes are whole so that the atlas stays small
                int low = static_cast<int>(std::ceil(smallest));
                int high = static_cast<int>(std::ceil(options.fontSize)) - 1;
                int best = -1;
                while (low <= high) {
                    const int middle = low + (high - low) / 2;
                    if (fitsAt(static_cast<float>(middle))) {
                        best = middle;
                        low = middle + 1;
                    } else {
                        high = middle - 1;
                    }
                }
                if (best >= 0) {
                    fontSize = static_cast<float>(best);
                } else {
                    fontSize = smallest;
                    ellipsize = true;  // even the smallest size is too big: cut what is left
                }
            }
        }

        return Assemble(font, LayoutLines(font, utf8, fontSize, options, ellipsize), fontSize, options);
    }

}  // namespace BECore
