#include "SpriteSheetGrid.h"

#include <Widgets/ArtGen/ArtGenAccept.h>

namespace BECore {

    namespace {

        bool IsSeparator(char c) {
            return c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        std::expected<void, SheetGridError> ComputeAxis(int size, int count, int margin, int spacing, Vector<Pair<int64_t, int64_t>>& out) {
            const int64_t inner = static_cast<int64_t>(size) - 2 * static_cast<int64_t>(margin);
            const int64_t available = inner - static_cast<int64_t>(spacing) * (count - 1);
            if (margin < 0 || spacing < 0 || available < count) {
                return std::unexpected(SheetGridError::CellTooSmall);
            }
            out.clear();
            // 64-bit so a huge margin from a text field cannot overflow before the check above; a cell ends where the next one's share
            // starts, and each gap adds its spacing, so the last cell ends exactly `margin` before the edge
            for (int64_t i = 0; i < count; ++i) {
                const int64_t start = margin + (i * available) / count + i * spacing;
                const int64_t end = margin + ((i + 1) * available) / count + i * spacing;
                out.push_back({start, end - start});
            }
            return {};
        }

        uint32_t DigitCount(uint32_t value) {
            uint32_t digits = 1;
            while (value >= 10) {
                value /= 10;
                ++digits;
            }
            return digits;
        }

    }  // namespace

    StringView SheetGridErrorText(SheetGridError error) {
        switch (error) {
            case SheetGridError::NoSheet:
                return "No texture to slice";
            case SheetGridError::BadCount:
                return "Columns and rows must be 1 to 64";
            case SheetGridError::TooManyCells:
                return "At most 1024 cells";
            case SheetGridError::CellTooSmall:
                return "Margin and spacing leave a cell under 1 pixel";
            case SheetGridError::DuplicateName:
                return "Two cells get the same name";
            case SheetGridError::BadName:
                return "A name is empty or not 1 to 64 of A-Z a-z 0-9 _ -";
        }
        return {};
    }

    std::expected<Vector<Rect>, SheetGridError> ComputeGridCells(int sheetWidth, int sheetHeight, const SheetGridSpec& spec) {
        if (sheetWidth <= 0 || sheetHeight <= 0) {
            return std::unexpected(SheetGridError::NoSheet);
        }
        if (spec.columns < 1 || spec.columns > kMaxSheetGridAxis || spec.rows < 1 || spec.rows > kMaxSheetGridAxis) {
            return std::unexpected(SheetGridError::BadCount);
        }
        if (spec.columns * spec.rows > kMaxSheetGridCells) {
            return std::unexpected(SheetGridError::TooManyCells);
        }

        // (start, length) of each column and each row
        Vector<Pair<int64_t, int64_t>> xs;
        Vector<Pair<int64_t, int64_t>> ys;
        if (auto columns = ComputeAxis(sheetWidth, spec.columns, spec.margin, spec.spacing, xs); !columns) {
            return std::unexpected(columns.error());
        }
        if (auto rows = ComputeAxis(sheetHeight, spec.rows, spec.margin, spec.spacing, ys); !rows) {
            return std::unexpected(rows.error());
        }

        Vector<Rect> cells;
        cells.reserve(xs.size() * ys.size());
        for (const auto& row : ys) {
            for (const auto& column : xs) {
                cells.push_back(Rect(static_cast<float>(column.first), static_cast<float>(row.first), static_cast<float>(column.second), static_cast<float>(row.second)));
            }
        }
        return cells;
    }

    Vector<String> ParseSheetNames(StringView text) {
        Vector<String> names;
        size_t pos = 0;
        while (pos < text.size()) {
            if (IsSeparator(text[pos])) {
                ++pos;
                continue;
            }
            const size_t begin = pos;
            while (pos < text.size() && !IsSeparator(text[pos])) {
                ++pos;
            }
            names.push_back(String(text.substr(begin, pos - begin)));
        }
        return names;
    }

    std::expected<GridSlicePlan, SheetGridError> PlanGridSlice(const TextureLibrary& library, PoolString texturePath, int sheetWidth, int sheetHeight, const SheetGridSpec& spec,
                                                               const SheetGridNaming& naming, SheetConflictPolicy policy) {
        if (texturePath.Empty()) {
            return std::unexpected(SheetGridError::NoSheet);
        }
        auto cells = ComputeGridCells(sheetWidth, sheetHeight, spec);
        if (!cells) {
            return std::unexpected(cells.error());
        }

        const uint32_t count = static_cast<uint32_t>(cells->size());
        const uint32_t padWidth = DigitCount(naming.startIndex + count - 1);

        Vector<String> names;
        names.reserve(count);
        HashSet<String> seen;
        for (uint32_t k = 0; k < count; ++k) {
            String name;
            if (k < naming.names.size() && !naming.names[k].empty()) {
                name = naming.names[k];
            } else if (!naming.prefix.empty()) {
                const String digits = ToString(naming.startIndex + k);
                name = naming.prefix;
                name.push_back('_');
                name.append(padWidth - static_cast<uint32_t>(digits.size()), '0');
                name.append(digits);
            }
            if (!IsValidSpriteName(name)) {
                return std::unexpected(SheetGridError::BadName);
            }
            if (!seen.insert(name).second) {
                return std::unexpected(SheetGridError::DuplicateName);
            }
            names.push_back(eastl::move(name));
        }

        GridSlicePlan plan;
        for (uint32_t k = 0; k < count; ++k) {
            const PoolString name = PoolString::Intern(StringView(names[k]));
            const bool exists = library.GetSprite(name) != nullptr;
            if (exists && policy == SheetConflictPolicy::Skip) {
                ++plan.skipped;
                continue;
            }
            if (exists) {
                plan.replaced.push_back(name);
            }
            SpriteEntry entry;
            entry.name = name;
            entry.texturePath = texturePath;
            entry.srcRect = (*cells)[k];
            plan.entries.push_back(eastl::move(entry));
        }
        return plan;
    }

    void ApplyGridSlice(TextureLibrary& library, const GridSlicePlan& plan) {
        for (const SpriteEntry& entry : plan.entries) {
            library.AddOrUpdate(entry);
        }
    }

}  // namespace BECore
