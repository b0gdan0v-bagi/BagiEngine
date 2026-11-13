#include "UiField.h"

#include <Stress/StressHash.h>
#include <cmath>

namespace BECore::Stress {

    namespace {

        constexpr uint64_t kSaltEnglish = 31;
        constexpr uint64_t kSaltRussian = 32;
        // Of every eight labels the fifth (even: English) and the eighth (odd: Russian) hold a variable
        constexpr uint32_t kEnglishVariableSlot = 4;
        constexpr uint32_t kRussianVariableSlot = 7;
        constexpr uint32_t kKeyBraces = 3;  // "{#" before the key and "}" after it
        constexpr uint32_t kVariableVariants = 2;

        constexpr eastl::array<eastl::string_view, kUiPoolSize> kEnglish = {"Hello, stress test",     "The quick brown fox",   "Frame time budget",    "Sixteen point six",
                                                                            "Draw calls per frame",   "Glyphs from an atlas",  "Text layout cache",    "Sprites and tweens",
                                                                            "Update and draw",        "Nothing is free",       "Measure, then change", "A label in a grid",
                                                                            "Wide cells, short text", "Pack my box with jugs", "Jumps over the dog",   "How many fit in a frame"};

        constexpr eastl::array<eastl::string_view, kUiPoolSize> kRussian = {"Привет, нагрузка",         "Съешь ещё мягких булок", "Бюджет времени кадра", "Шестнадцать и шесть",
                                                                            "Вызовы отрисовки за кадр", "Глифы из атласа",        "Кеш раскладки текста", "Спрайты и твины",
                                                                            "Обновление и отрисовка",   "Ничто не бесплатно",     "Сначала измерь",       "Подпись в сетке",
                                                                            "Широкие ячейки",           "Эй, жлоб, где туз?",     "Прыгает через пса",    "Сколько влезет в кадр"};

        constexpr eastl::array<eastl::string_view, kUiPoolSize> kKeys = {
            "{#stress.label.0}", "{#stress.label.1}", "{#stress.label.2}",  "{#stress.label.3}",  "{#stress.label.4}",  "{#stress.label.5}",  "{#stress.label.6}",  "{#stress.label.7}",
            "{#stress.label.8}", "{#stress.label.9}", "{#stress.label.10}", "{#stress.label.11}", "{#stress.label.12}", "{#stress.label.13}", "{#stress.label.14}", "{#stress.label.15}"};

        constexpr eastl::array<eastl::string_view, kVariableVariants> kEnglishVariable = {"Objects: {count}", "Last label: {last}"};
        constexpr eastl::array<eastl::string_view, kVariableVariants> kRussianVariable = {"Объектов: {count}", "Последняя: {last}"};

        uint32_t PoolIndex(uint32_t index, uint64_t salt) {
            return static_cast<uint32_t>(Hash(index, salt) % kUiPoolSize);
        }

        bool IsEven(uint32_t index) {
            return index % 2 == 0;
        }

        uint32_t KeyIndex(uint32_t index) {
            return (index / kUiKeyEvery) % kUiPoolSize;
        }

        /// Which of the two texts of a language's variable label: alternates by the eight the label is in
        uint32_t VariableVariant(uint32_t index) {
            return (index / kUiKeyEvery) % kVariableVariants;
        }

    }  // namespace

    UiGrid MakeGrid(uint32_t count, const FieldView& view) {
        UiGrid grid;
        if (count == 0) {
            grid.cellWidth = view.width;
            grid.cellHeight = view.height;
            return grid;
        }
        const double wanted = std::ceil(std::sqrt(static_cast<double>(count) * view.width / (view.height * kUiCellAspect)));
        grid.columns = std::clamp(static_cast<uint32_t>(wanted), 1u, count);
        grid.rows = (count + grid.columns - 1) / grid.columns;
        grid.cellWidth = view.width / static_cast<float>(grid.columns);
        grid.cellHeight = view.height / static_cast<float>(grid.rows);
        return grid;
    }

    UiCell CellOf(uint32_t index, const UiGrid& grid) {
        const uint32_t column = index % grid.columns;
        const uint32_t row = index / grid.columns;
        UiCell cell;
        cell.x = static_cast<float>(column) * grid.cellWidth;
        cell.y = static_cast<float>(row) * grid.cellHeight;
        cell.width = grid.cellWidth;
        cell.height = grid.cellHeight;
        cell.fontSize = kUiMinFontSize + kUiFontStep * static_cast<float>(row % kUiFontSizes);
        return cell;
    }

    UiLabelKind KindOf(uint32_t index) {
        if (index % kUiKeyEvery == 0) {
            return UiLabelKind::Key;
        }
        const uint32_t slot = index % kUiKeyEvery;
        if (slot == kEnglishVariableSlot || slot == kRussianVariableSlot) {
            return UiLabelKind::Variable;
        }
        return IsEven(index) ? UiLabelKind::English : UiLabelKind::Russian;
    }

    eastl::string_view TextOf(uint32_t index) {
        switch (KindOf(index)) {
            case UiLabelKind::Key:
                return kKeys[KeyIndex(index)];
            case UiLabelKind::Variable:
                return IsEven(index) ? kEnglishVariable[VariableVariant(index)] : kRussianVariable[VariableVariant(index)];
            case UiLabelKind::English:
                return kEnglish[PoolIndex(index, kSaltEnglish)];
            case UiLabelKind::Russian:
                return kRussian[PoolIndex(index, kSaltRussian)];
        }
        return kEnglish[0];
    }

    eastl::string_view KeyOf(uint32_t index) {
        const eastl::string_view text = kKeys[KeyIndex(index)];
        return text.substr(kKeyBraces - 1, text.size() - kKeyBraces);  // between "{#" and "}"
    }

}  // namespace BECore::Stress
