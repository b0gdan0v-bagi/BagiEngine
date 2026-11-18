#include "XmlLayoutMerge.h"

namespace BECore {

    namespace {

        /// A config file is a few hundred lines; the cap keeps a stray big file from costing a diff
        constexpr size_t kMaxLines = 20000;

        /// The diff keeps one snapshot per edit, so its memory grows with the square of the edit count
        constexpr int kMaxEditCount = 2000;

        constexpr char kLineBreak = '\n';

        struct Lines {
            Vector<StringView> lines;
            bool endsWithLineBreak = false;
        };

        Lines SplitLines(StringView text) {
            Lines result;
            size_t begin = 0;
            while (begin < text.size()) {
                const size_t end = text.find(kLineBreak, begin);
                if (end == StringView::npos) {
                    result.lines.push_back(text.substr(begin));
                    return result;
                }
                result.lines.push_back(text.substr(begin, end - begin));
                begin = end + 1;
            }
            result.endsWithLineBreak = !text.empty();
            return result;
        }

        StringView Trimmed(StringView line) {
            const size_t first = line.find_first_not_of(" \t");
            if (first == StringView::npos) {
                return {};
            }
            const size_t last = line.find_last_not_of(" \t");
            return line.substr(first, last - first + 1);
        }

        bool IsBlank(StringView line) {
            return Trimmed(line).empty();
        }

        /// The indexes of the lines that are not blank
        Vector<size_t> NonBlankIndexes(const Vector<StringView>& lines) {
            Vector<size_t> indexes;
            for (size_t i = 0; i < lines.size(); ++i) {
                if (!IsBlank(lines[i])) {
                    indexes.push_back(i);
                }
            }
            return indexes;
        }

        /// Blank lines before the first non-blank line, between each two and after the last: a gap per non-blank line plus one
        Vector<size_t> BlankRunLengths(const Vector<StringView>& lines, const Vector<size_t>& nonBlank) {
            Vector<size_t> runs;
            size_t previous = 0;
            for (const size_t index : nonBlank) {
                runs.push_back(index - previous);
                previous = index + 1;
            }
            runs.push_back(lines.size() - previous);
            return runs;
        }

        /// For each line of @p baseline the line of @p original it stands for, or nothing when they do not match
        ///
        /// Blank lines of @p baseline are those inside a comment or a text: the original has the same number of them in the
        /// same place, and the blank lines the person added between elements are the ones @p baseline lacks.
        Optional<Vector<size_t>> MapBaselineToOriginal(const Vector<StringView>& original, const Vector<StringView>& baseline) {
            const Vector<size_t> originalNonBlank = NonBlankIndexes(original);
            const Vector<size_t> baselineNonBlank = NonBlankIndexes(baseline);
            if (originalNonBlank.empty() || originalNonBlank.size() != baselineNonBlank.size()) {
                return std::nullopt;
            }
            for (size_t i = 0; i < baselineNonBlank.size(); ++i) {
                if (Trimmed(original[originalNonBlank[i]]) != Trimmed(baseline[baselineNonBlank[i]])) {
                    return std::nullopt;
                }
            }

            const Vector<size_t> originalRuns = BlankRunLengths(original, originalNonBlank);
            const Vector<size_t> baselineRuns = BlankRunLengths(baseline, baselineNonBlank);
            for (size_t i = 0; i < baselineRuns.size(); ++i) {
                if (baselineRuns[i] != 0 && baselineRuns[i] != originalRuns[i]) {
                    return std::nullopt;
                }
            }

            Vector<size_t> map;
            map.reserve(baseline.size());
            size_t nonBlankSeen = 0;
            size_t cursor = 0;
            for (size_t i = 0; i < baseline.size(); ++i) {
                if (!IsBlank(baseline[i])) {
                    cursor = originalNonBlank[nonBlankSeen];
                    ++nonBlankSeen;
                }
                map.push_back(cursor);
                ++cursor;
            }
            return map;
        }

        /// Lines [baselineBegin, baselineEnd) of the baseline become lines [updatedBegin, updatedEnd) of the updated text
        struct Hunk {
            size_t baselineBegin = 0;
            size_t baselineEnd = 0;
            size_t updatedBegin = 0;
            size_t updatedEnd = 0;
        };

        /// Myers' shortest edit script between two line lists, as hunks in order; nothing when it needs too many edits
        Optional<Vector<Hunk>> DiffLines(const Vector<StringView>& a, const Vector<StringView>& b) {
            const int n = static_cast<int>(a.size());
            const int m = static_cast<int>(b.size());
            const int maxEdits = std::min(n + m, kMaxEditCount);

            // furthest[k + offset] is the largest x reached on diagonal k; the diagonals k = 1 and k = -1 are read at d = 0
            const int offset = maxEdits + 1;
            Vector<int> furthest(static_cast<size_t>(2 * offset + 1), 0);
            // trace[d] holds furthest after round d, for k in [-d, d]
            Vector<Vector<int>> trace;

            int editCount = -1;
            for (int d = 0; d <= maxEdits && editCount < 0; ++d) {
                for (int k = -d; k <= d; k += 2) {
                    int x = 0;
                    if (k == -d || (k != d && furthest[static_cast<size_t>(offset + k - 1)] < furthest[static_cast<size_t>(offset + k + 1)])) {
                        x = furthest[static_cast<size_t>(offset + k + 1)];
                    } else {
                        x = furthest[static_cast<size_t>(offset + k - 1)] + 1;
                    }
                    int y = x - k;
                    while (x < n && y < m && a[static_cast<size_t>(x)] == b[static_cast<size_t>(y)]) {
                        ++x;
                        ++y;
                    }
                    furthest[static_cast<size_t>(offset + k)] = x;
                    if (x >= n && y >= m) {
                        editCount = d;
                        break;
                    }
                }
                Vector<int>& snapshot = trace.emplace_back();
                snapshot.resize(static_cast<size_t>(2 * d + 1));
                for (int k = -d; k <= d; k += 2) {
                    snapshot[static_cast<size_t>(k + d)] = furthest[static_cast<size_t>(offset + k)];
                }
            }
            if (editCount < 0) {
                return std::nullopt;
            }

            Vector<bool> deleted(a.size(), false);
            Vector<bool> inserted(b.size(), false);
            int x = n;
            int y = m;
            for (int d = editCount; d > 0; --d) {
                const Vector<int>& before = trace[static_cast<size_t>(d - 1)];
                const auto valueAt = [&before, d](int k) { return before[static_cast<size_t>(k + d - 1)]; };
                const int k = x - y;
                const bool fromInsertion = (k == -d) || (k != d && valueAt(k - 1) < valueAt(k + 1));
                const int previousK = fromInsertion ? k + 1 : k - 1;
                const int previousX = valueAt(previousK);
                const int previousY = previousX - previousK;
                while (x > previousX && y > previousY) {
                    --x;
                    --y;
                }
                if (fromInsertion) {
                    inserted[static_cast<size_t>(previousY)] = true;
                } else {
                    deleted[static_cast<size_t>(previousX)] = true;
                }
                x = previousX;
                y = previousY;
            }

            Vector<Hunk> hunks;
            size_t i = 0;
            size_t j = 0;
            while (i < a.size() || j < b.size()) {
                if (i < a.size() && j < b.size() && !deleted[i] && !inserted[j]) {
                    ++i;
                    ++j;
                    continue;
                }
                Hunk hunk;
                hunk.baselineBegin = i;
                hunk.updatedBegin = j;
                while (i < a.size() && deleted[i]) {
                    ++i;
                }
                while (j < b.size() && inserted[j]) {
                    ++j;
                }
                hunk.baselineEnd = i;
                hunk.updatedEnd = j;
                hunks.push_back(hunk);
            }
            return hunks;
        }

        void AppendLine(String& out, StringView line) {
            out.append(line.data(), line.size());
            out.push_back(kLineBreak);
        }

        StringView Indentation(StringView line) {
            return line.substr(0, line.find_first_not_of(" \t"));
        }

        /// A line that replaces @p replaced, where @p baselineLine stood for @p replaced in the canonical text: when the
        /// edit did not move it to another depth it keeps the indentation the file has, whatever the file's indent is
        void AppendReplacement(String& out, StringView replaced, StringView baselineLine, StringView line) {
            const StringView indentation = Indentation(line);
            if (indentation != Indentation(baselineLine)) {
                AppendLine(out, line);
                return;
            }
            const StringView kept = Indentation(replaced);
            out.append(kept.data(), kept.size());
            AppendLine(out, line.substr(indentation.size()));
        }

    }  // namespace

    Optional<String> MergeXmlLayout(StringView original, StringView baseline, StringView updated) {
        if (original.empty() || original.find('\r') != StringView::npos || baseline.find('\r') != StringView::npos || updated.find('\r') != StringView::npos) {
            return std::nullopt;
        }
        const Lines originalLines = SplitLines(original);
        const Lines baselineLines = SplitLines(baseline);
        const Lines updatedLines = SplitLines(updated);
        if (originalLines.lines.size() > kMaxLines || baselineLines.lines.size() > kMaxLines || updatedLines.lines.size() > kMaxLines) {
            return std::nullopt;
        }

        const Optional<Vector<size_t>> map = MapBaselineToOriginal(originalLines.lines, baselineLines.lines);
        if (!map) {
            return std::nullopt;
        }
        if (baseline == updated) {
            return String(original);
        }
        const Optional<Vector<Hunk>> hunks = DiffLines(baselineLines.lines, updatedLines.lines);
        if (!hunks) {
            return std::nullopt;
        }

        String result;
        result.reserve(original.size() + updated.size());
        size_t next = 0;
        bool lastFromUpdated = false;
        for (const Hunk& hunk : *hunks) {
            size_t replaceBegin = 0;
            size_t replaceEnd = 0;
            if (hunk.baselineEnd > hunk.baselineBegin) {
                replaceBegin = (*map)[hunk.baselineBegin];
                replaceEnd = (*map)[hunk.baselineEnd - 1] + 1;
            } else {
                // A pure insertion goes directly before the line that follows it, or after the last line of the file
                replaceBegin = hunk.baselineBegin < map->size() ? (*map)[hunk.baselineBegin] : originalLines.lines.size();
                replaceEnd = replaceBegin;
            }
            for (; next < replaceBegin; ++next) {
                AppendLine(result, originalLines.lines[next]);
                lastFromUpdated = false;
            }
            const bool inPlace = hunk.baselineEnd - hunk.baselineBegin == hunk.updatedEnd - hunk.updatedBegin;
            for (size_t i = hunk.updatedBegin; i < hunk.updatedEnd; ++i) {
                if (inPlace) {
                    const size_t baselineIndex = hunk.baselineBegin + (i - hunk.updatedBegin);
                    AppendReplacement(result, originalLines.lines[(*map)[baselineIndex]], baselineLines.lines[baselineIndex], updatedLines.lines[i]);
                } else {
                    AppendLine(result, updatedLines.lines[i]);
                }
                lastFromUpdated = true;
            }
            next = replaceEnd;
        }
        for (; next < originalLines.lines.size(); ++next) {
            AppendLine(result, originalLines.lines[next]);
            lastFromUpdated = false;
        }

        const bool endsWithLineBreak = lastFromUpdated ? updatedLines.endsWithLineBreak : originalLines.endsWithLineBreak;
        if (!endsWithLineBreak && !result.empty()) {
            result.pop_back();
        }
        return result;
    }

}  // namespace BECore
