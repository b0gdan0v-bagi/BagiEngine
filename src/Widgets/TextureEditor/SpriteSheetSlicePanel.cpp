#include "SpriteSheetSlicePanel.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Math/NumberUtils.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Generated/SpriteSheetSlicePanel.gen.hpp>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>

namespace BECore {

    namespace {

        constexpr float kNumberFieldWidth = 90.0f;
        constexpr float kPrefixFieldWidth = 160.0f;
        constexpr float kNamesFieldHeight = 56.0f;
        constexpr float kOverlayThickness = 1.0f;
        constexpr int kCountStep = 1;
        constexpr int kCountStepFast = 4;
        constexpr int kPixelStep = 1;
        constexpr int kPixelStepFast = 8;
        constexpr int kAlphaStep = 1;
        constexpr int kAlphaStepFast = 16;
        const char* const kPressFindIslands = "Press Find islands";

        /// Which names the library holds, order included: the plan depends on them, and nothing else in the library
        uint64_t NamesFingerprint(const TextureLibrary& library) {
            uint64_t print = library.GetAll().size();
            for (const SpriteEntry& entry : library.GetAll()) {
                HashCombine(print, entry.name.HashValue());
            }
            return print;
        }

        constexpr unsigned char kUtf8ContinuationMask = 0xC0;
        constexpr unsigned char kUtf8Continuation = 0x80;

        /// Cuts @p text to at most @p limit bytes, never inside a UTF-8 sequence; true when it was cut
        bool CutToBytes(String& text, size_t limit) {
            if (text.size() <= limit) {
                return false;
            }
            size_t end = limit;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & kUtf8ContinuationMask) == kUtf8Continuation) {
                --end;
            }
            text.resize(end);
            return true;
        }

        /// Clamps @p value to the range in place; true when it differs from @p current afterwards
        bool TakeNumber(int32_t& value, int minimum, int maximum, int& current) {
            value = std::clamp(value, minimum, maximum);
            if (value == current) {
                return false;
            }
            current = value;
            return true;
        }

        /// Copies @p text into the buffer @p target when it differs; true when it did
        template <size_t TSize>
        bool TakeText(const String& text, char (&target)[TSize]) {
            if (StringView(text) == StringView(target)) {
                return false;
            }
            const size_t length = std::min(text.size(), TSize - 1);
            memcpy(target, text.data(), length);
            target[length] = '\0';
            return true;
        }

        /// A number field in the mono font with its name before it; true when the value was edited (and is clamped to the range)
        bool NumberField(const char* label, const char* id, int& value, int step, int stepFast, int minimum, int maximum) {
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(kNumberFieldWidth);
            bool changed = false;
            {
                const ScopedMonoFont mono;
                changed = ImGui::InputInt(id, &value, step, stepFast);
            }
            value = eastl::clamp(value, minimum, maximum);
            return changed;
        }

    }  // namespace

    PoolString SpriteSheetSlicePanel::Render(TextureLibrary& library, PoolString texturePath, int sheetWidth, int sheetHeight) {
        _isOpen = ImGui::CollapsingHeader(ICON_LC_GRID_3X2 " Slice sheet");
        if (!_isOpen) {
            return {};
        }

        const PlanKey key{texturePath, sheetWidth, sheetHeight, NamesFingerprint(library)};
        if (key.texturePath != _key.texturePath || key.width != _key.width || key.height != _key.height) {
            ClearIslands();
        }

        bool changed = false;
        if (ImGui::RadioButton(ICON_LC_GRID_3X2 " Grid##slice_mode", _mode == SheetSliceMode::Grid)) {
            if (_mode != SheetSliceMode::Grid) {
                _mode = SheetSliceMode::Grid;
                ClearIslands();
                changed = true;
            }
        }
        ItemTooltip("Cut the sheet into equal cells");
        ImGui::SameLine();
        if (ImGui::RadioButton(ICON_LC_SCAN " Alpha islands##slice_mode", _mode == SheetSliceMode::Islands)) {
            if (_mode != SheetSliceMode::Islands) {
                _mode = SheetSliceMode::Islands;
                ClearIslands();
                changed = true;
            }
        }
        ItemTooltip("Cut out what is drawn: each separate opaque shape of the file becomes a sprite");

        if (_mode == SheetSliceMode::Grid) {
            changed |= NumberField("Columns", "##slice_columns", _spec.columns, kCountStep, kCountStepFast, 1, kMaxSheetGridAxis);
            ImGui::SameLine();
            changed |= NumberField("Rows", "##slice_rows", _spec.rows, kCountStep, kCountStepFast, 1, kMaxSheetGridAxis);
            changed |= NumberField("Margin", "##slice_margin", _spec.margin, kPixelStep, kPixelStepFast, 0, kMaxInsetPixels);
            ImGui::SameLine();
            changed |= NumberField("Spacing", "##slice_spacing", _spec.spacing, kPixelStep, kPixelStepFast, 0, kMaxInsetPixels);
            ItemTooltip("Pixels between neighbouring cells");
        } else {
            bool islandsEdited = false;
            islandsEdited |= NumberField("Alpha above", "##slice_alpha", _island.alphaThreshold, kAlphaStep, kAlphaStepFast, 0, kMaxIslandAlphaThreshold);
            ItemTooltip("A pixel is part of a shape when its alpha is above this (0 to 254)");
            ImGui::SameLine();
            islandsEdited |= NumberField("Min size", "##slice_min_size", _island.minSize, kPixelStep, kPixelStepFast, 1, kMaxIslandMinSize);
            ItemTooltip("Shapes narrower or lower than this many pixels are dropped as specks");
            islandsEdited |= NumberField("Padding", "##slice_padding", _island.padding, kPixelStep, kPixelStepFast, 0, kMaxIslandPadding);
            ItemTooltip("Pixels added around each shape; the boxes of neighbours may overlap");
            ImGui::SameLine();
            islandsEdited |= NumberField("Merge gap", "##slice_merge_gap", _island.mergeGap, kPixelStep, kPixelStepFast, 0, kMaxIslandMergeGap);
            ItemTooltip("Shapes at most this many empty pixels apart become one (the dot of an i)");
            if (islandsEdited) {
                ClearIslands();
                changed = true;
            }

            if (IconTextButton(ICON_LC_SCAN_SEARCH, "Find islands", "Read the file and find the shapes; they are drawn over the preview")) {
                FindIslands(texturePath, sheetWidth, sheetHeight);
                changed = true;
            }
        }

        ImGui::TextUnformatted("Name prefix");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(kPrefixFieldWidth);
        changed |= ImGui::InputText("##slice_prefix", _prefix, sizeof(_prefix));
        ImGui::SameLine();
        changed |= NumberField("from", "##slice_start", _startIndex, kCountStep, kCountStepFast, 0, kMaxStartIndex);
        ItemTooltip("The number of the first sprite: <prefix>_<number>");

        ImGui::TextUnformatted("Names (optional, in order, separated by spaces or commas)");
        changed |= ImGui::InputTextMultiline("##slice_names", _names, sizeof(_names), ImVec2(-1.0f, kNamesFieldHeight));
        ItemTooltip("A sprite without a name here is called <prefix>_<number>");

        ImGui::TextUnformatted("A name the library already has:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Overwrite##slice_policy", _policy == SheetConflictPolicy::Overwrite)) {
            _policy = SheetConflictPolicy::Overwrite;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Skip##slice_policy", _policy == SheetConflictPolicy::Skip)) {
            _policy = SheetConflictPolicy::Skip;
            changed = true;
        }

        if (changed || _inputsChanged || key != _key) {
            _key = key;
            _inputsChanged = false;
            MakePlan(library);
        }

        if (_hasError) {
            ImGui::TextColored(GetImGuiPalette().error, "%s", _summary);
        } else {
            ImGui::TextUnformatted(_summary);
        }

        PoolString firstAdded;
        ImGui::BeginDisabled(!_hasPlan || _plan.entries.empty());
        if (IconTextButton(ICON_LC_GRID_2X2_PLUS, "Add sprites", "Add one sprite per cell or island to the library; Save writes the file")) {
            firstAdded = AddPlanned(library);
        }
        ImGui::EndDisabled();
        return firstAdded;
    }

    void SpriteSheetSlicePanel::Sync(SpriteSliceView& view, const TextureLibrary& library, PoolString texturePath, int sheetWidth, int sheetHeight) {
        _isOpen = view.open;
        const PlanKey key{texturePath, sheetWidth, sheetHeight, NamesFingerprint(library)};
        if (key.texturePath != _key.texturePath || key.width != _key.width || key.height != _key.height) {
            ClearIslands();
        }

        bool changed = false;
        if (view.mode != _mode) {
            _mode = view.mode;
            ClearIslands();
            changed = true;
        }
        changed |= TakeNumber(view.columns, 1, kMaxSheetGridAxis, _spec.columns);
        changed |= TakeNumber(view.rows, 1, kMaxSheetGridAxis, _spec.rows);
        changed |= TakeNumber(view.margin, 0, kMaxInsetPixels, _spec.margin);
        changed |= TakeNumber(view.spacing, 0, kMaxInsetPixels, _spec.spacing);
        bool islandsEdited = false;
        islandsEdited |= TakeNumber(view.alphaThreshold, 0, kMaxIslandAlphaThreshold, _island.alphaThreshold);
        islandsEdited |= TakeNumber(view.minSize, 1, kMaxIslandMinSize, _island.minSize);
        islandsEdited |= TakeNumber(view.padding, 0, kMaxIslandPadding, _island.padding);
        islandsEdited |= TakeNumber(view.mergeGap, 0, kMaxIslandMergeGap, _island.mergeGap);
        if (islandsEdited) {
            ClearIslands();
            changed = true;
        }
        changed |= TakeNumber(view.startIndex, 0, kMaxStartIndex, _startIndex);
        CutToBytes(view.prefix, kPrefixSize - 1);
        changed |= TakeText(view.prefix, _prefix);
        CutToBytes(view.names, kNamesSize - 1);
        changed |= TakeText(view.names, _names);
        if (view.policy != _policy) {
            _policy = view.policy;
            changed = true;
        }

        if (!changed && !_inputsChanged && key == _key) {
            return;
        }
        _key = key;
        _inputsChanged = false;
        MakePlan(library);
        view.summary.assign(_summary);
        view.hasError = _hasError;
        view.canAdd = _hasPlan && !_plan.entries.empty();
    }

    PoolString SpriteSheetSlicePanel::AddPlanned(TextureLibrary& library) {
        if (!_hasPlan || _plan.entries.empty()) {
            return {};
        }
        const PoolString firstAdded = _plan.entries.front().name;
        ApplyGridSlice(library, _plan);
        _inputsChanged = true;
        return firstAdded;
    }

    void SpriteSheetSlicePanel::DrawOverlay(ImDrawList* drawList, PoolString texturePath, ImVec2 origin, float scaleX, float scaleY) const {
        if (!_isOpen || _cells.empty() || _key.texturePath != texturePath) {
            return;
        }
        const ImU32 color = ImGui::GetColorU32(GetImGuiPalette().accentHover);
        for (const Rect& cell : _cells) {
            const ImVec2 cellMin(origin.x + cell.x / scaleX, origin.y + cell.y / scaleY);
            const ImVec2 cellMax(cellMin.x + cell.w / scaleX, cellMin.y + cell.h / scaleY);
            drawList->AddRect(cellMin, cellMax, color, 0.0f, 0, kOverlayThickness);
        }
    }

    void SpriteSheetSlicePanel::ClearIslands() {
        if (_mode == SheetSliceMode::Islands) {
            _cells.clear();
        }
        _islandError.clear();
        _inputsChanged = true;
    }

    void SpriteSheetSlicePanel::FindIslands(PoolString texturePath, int width, int height) {
        _cells.clear();
        _islandError.clear();
        _inputsChanged = true;

        const Path file = CoreManager::GetFileSystem().ResolvePath(texturePath.ToStringView());
        // The pixels live only in this call: the panel keeps the rects, never the image
        auto image = LoadRgbaImage(file);
        if (!image) {
            _islandError = image.error();
            return;
        }
        if (image->width != static_cast<uint32_t>(width) || image->height != static_cast<uint32_t>(height)) {
            _islandError = "The file changed since it was loaded: its size differs from the preview";
            return;
        }
        auto found = FindAlphaIslands(*image, _island);
        if (!found) {
            _islandError = String(SheetIslandErrorText(found.error()));
            return;
        }
        _cells = eastl::move(*found);
    }

    void SpriteSheetSlicePanel::SetStatus(StringView text, bool isError) {
        _hasError = isError;
        snprintf(_summary, sizeof(_summary), "%.*s", static_cast<int>(text.size()), text.data());
    }

    void SpriteSheetSlicePanel::MakePlan(const TextureLibrary& library) {
        _plan = GridSlicePlan{};
        _hasPlan = false;
        _hasError = false;

        if (_mode == SheetSliceMode::Grid) {
            _cells.clear();
            auto cells = ComputeGridCells(_key.width, _key.height, _spec);
            if (!cells) {
                SetStatus(SheetGridErrorText(cells.error()), true);
                return;
            }
            _cells = eastl::move(*cells);
        } else if (!_islandError.empty()) {
            SetStatus(StringView(_islandError), true);
            return;
        } else if (_cells.empty()) {
            SetStatus(kPressFindIslands, false);
            return;
        }

        SheetGridNaming naming;
        naming.prefix = String(StringView(_prefix));
        naming.startIndex = static_cast<uint32_t>(_startIndex);
        naming.names = ParseSheetNames(StringView(_names));

        auto plan = PlanRectSlice(library, _key.texturePath, _cells, naming, _policy);
        if (!plan) {
            SetStatus(SheetGridErrorText(plan.error()), true);
            return;
        }

        _plan = eastl::move(*plan);
        _hasPlan = true;
        const char* const noun = _mode == SheetSliceMode::Grid ? "cells" : "islands";
        const String summary = Format("{} {}: {} to add, {} to replace, {} skipped", _cells.size(), noun, _plan.entries.size() - _plan.replaced.size(), _plan.replaced.size(), _plan.skipped);
        SetStatus(StringView(summary), false);
    }

}  // namespace BECore
