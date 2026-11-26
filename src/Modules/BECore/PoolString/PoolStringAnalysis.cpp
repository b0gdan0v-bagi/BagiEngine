#include "PoolStringAnalysis.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore::PoolStringAnalysis {

    namespace {

        char ToLowerAscii(char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool ContainsIgnoringCase(StringView text, StringView part) {
            return part.empty() || eastl::search(text.begin(), text.end(), part.begin(), part.end(), [](char left, char right) { return ToLowerAscii(left) == ToLowerAscii(right); }) != text.end();
        }

        template <typename T>
        int Compare(const T& left, const T& right) {
            return left < right ? -1 : (right < left ? 1 : 0);
        }

        StringView BlockSourceOf(const PoolStringSnapshot& snapshot, const PoolStringRow& row) {
            return row.block == kNoPoolBlock ? StringView() : StringView(snapshot.blocks[row.block].source);
        }

        int CompareByKey(const PoolStringSnapshot& snapshot, PoolStringSortKey key, const PoolStringRow& left, const PoolStringRow& right) {
            switch (key) {
                case PoolStringSortKey::Text:
                    return Compare(left.text.ToStringView(), right.text.ToStringView());
                case PoolStringSortKey::Length:
                    return Compare(left.text.Length(), right.text.Length());
                case PoolStringSortKey::Hash:
                    return Compare(left.text.HashValue(), right.text.HashValue());
                case PoolStringSortKey::Source: {
                    const int byOrigin = Compare(+left.origin, +right.origin);
                    return byOrigin != 0 ? byOrigin : Compare(BlockSourceOf(snapshot, left), BlockSourceOf(snapshot, right));
                }
            }
            return 0;
        }

        /// Every row, equal keys side by side: `before(left, right)` orders two rows, ties keep the order of the rows
        template <typename TBefore>
        Vector<uint32_t> SortedRows(const PoolStringSnapshot& snapshot, TBefore before) {
            Vector<uint32_t> rows(snapshot.strings.size());
            for (uint32_t i = 0; i < rows.size(); ++i) {
                rows[i] = i;
            }
            eastl::sort(rows.begin(), rows.end(), [&](uint32_t left, uint32_t right) {
                const PoolStringRow& a = snapshot.strings[left];
                const PoolStringRow& b = snapshot.strings[right];
                if (before(a, b)) {
                    return true;
                }
                return !before(b, a) && left < right;
            });
            return rows;
        }

    }  // namespace

    void FindHashCollisions(const PoolStringSnapshot& snapshot, Vector<RowPair>& out) {
        out.clear();
        const Vector<uint32_t> rows = SortedRows(snapshot, [](const PoolStringRow& left, const PoolStringRow& right) {
            const int byHash = Compare(left.text.HashValue(), right.text.HashValue());
            return byHash != 0 ? byHash < 0 : left.text.ToStringView() < right.text.ToStringView();
        });
        size_t first = 0;
        for (size_t i = 1; i < rows.size(); ++i) {
            const PoolString text = snapshot.strings[rows[i]].text;
            if (text.HashValue() != snapshot.strings[rows[first]].text.HashValue()) {
                first = i;
            } else if (text.ToStringView() != snapshot.strings[rows[i - 1]].text.ToStringView()) {
                out.push_back({rows[first], rows[i]});
            }
        }
    }

    void FindDuplicateTexts(const PoolStringSnapshot& snapshot, Vector<RowPair>& out) {
        out.clear();
        const Vector<uint32_t> rows = SortedRows(snapshot, [](const PoolStringRow& left, const PoolStringRow& right) { return left.text.ToStringView() < right.text.ToStringView(); });
        size_t first = 0;
        for (size_t i = 1; i < rows.size(); ++i) {
            if (snapshot.strings[rows[i]].text.ToStringView() != snapshot.strings[rows[first]].text.ToStringView()) {
                first = i;
            } else {
                out.push_back({rows[first], rows[i]});
            }
        }
    }

    void Search(const PoolStringSnapshot& snapshot, const PoolStringQuery& query, Vector<uint32_t>& out) {
        out.clear();
        for (uint32_t i = 0; i < snapshot.strings.size(); ++i) {
            const PoolStringRow& row = snapshot.strings[i];
            if (query.origin && row.origin != *query.origin) {
                continue;
            }
            if (!query.blockSource.empty() && BlockSourceOf(snapshot, row) != query.blockSource) {
                continue;
            }
            if (ContainsIgnoringCase(row.text.ToStringView(), query.text)) {
                out.push_back(i);
            }
        }
        eastl::sort(out.begin(), out.end(), [&](uint32_t left, uint32_t right) {
            const PoolStringRow& a = snapshot.strings[left];
            const PoolStringRow& b = snapshot.strings[right];
            if (const int byKey = CompareByKey(snapshot, query.sortBy, a, b); byKey != 0) {
                return query.descending ? byKey > 0 : byKey < 0;
            }
            if (const int byText = Compare(a.text.ToStringView(), b.text.ToStringView()); byText != 0) {
                return byText < 0;
            }
            return left < right;
        });
    }

}  // namespace BECore::PoolStringAnalysis
