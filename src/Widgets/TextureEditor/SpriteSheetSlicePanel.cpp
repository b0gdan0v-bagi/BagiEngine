#include "SpriteSheetSlicePanel.h"

#include <BECore/Math/NumberUtils.h>
#include <BECore/Resource/TextureLibrary.h>
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
        /// A margin or spacing beyond this is a typo: no texture is that big
        constexpr int kMaxInsetPixels = 16384;
        constexpr int kMaxStartIndex = 100000;

        /// Which names the library holds, order included: the plan depends on them, and nothing else in the library
        uint64_t NamesFingerprint(const TextureLibrary& library) {
            uint64_t print = library.GetAll().size();
            for (const SpriteEntry& entry : library.GetAll()) {
                HashCombine(print, entry.name.HashValue());
            }
            return print;
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

        bool changed = false;
        changed |= NumberField("Columns", "##slice_columns", _spec.columns, kCountStep, kCountStepFast, 1, kMaxSheetGridAxis);
        ImGui::SameLine();
        changed |= NumberField("Rows", "##slice_rows", _spec.rows, kCountStep, kCountStepFast, 1, kMaxSheetGridAxis);
        changed |= NumberField("Margin", "##slice_margin", _spec.margin, kPixelStep, kPixelStepFast, 0, kMaxInsetPixels);
        ImGui::SameLine();
        changed |= NumberField("Spacing", "##slice_spacing", _spec.spacing, kPixelStep, kPixelStepFast, 0, kMaxInsetPixels);
        ItemTooltip("Pixels between neighbouring cells");

        ImGui::TextUnformatted("Name prefix");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(kPrefixFieldWidth);
        changed |= ImGui::InputText("##slice_prefix", _prefix, sizeof(_prefix));
        ImGui::SameLine();
        changed |= NumberField("from", "##slice_start", _startIndex, kCountStep, kCountStepFast, 0, kMaxStartIndex);
        ItemTooltip("The number of the first cell: <prefix>_<number>");

        ImGui::TextUnformatted("Names (optional, in cell order, separated by spaces or commas)");
        changed |= ImGui::InputTextMultiline("##slice_names", _names, sizeof(_names), ImVec2(-1.0f, kNamesFieldHeight));
        ItemTooltip("A cell without a name here is called <prefix>_<number>");

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

        const PlanKey key{texturePath, sheetWidth, sheetHeight, NamesFingerprint(library)};
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
        if (IconTextButton(ICON_LC_GRID_2X2_PLUS, "Add sprites", "Add one sprite per cell to the library; Save writes the file")) {
            firstAdded = _plan.entries.front().name;
            ApplyGridSlice(library, _plan);
            _inputsChanged = true;
        }
        ImGui::EndDisabled();
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

    void SpriteSheetSlicePanel::MakePlan(const TextureLibrary& library) {
        _cells.clear();
        _plan = GridSlicePlan{};
        _hasPlan = false;
        _hasError = false;

        SheetGridNaming naming;
        naming.prefix = String(StringView(_prefix));
        naming.startIndex = static_cast<uint32_t>(_startIndex);
        naming.names = ParseSheetNames(StringView(_names));

        auto cells = ComputeGridCells(_key.width, _key.height, _spec);
        if (cells) {
            _cells = eastl::move(*cells);
        }
        auto plan = PlanGridSlice(library, _key.texturePath, _key.width, _key.height, _spec, naming, _policy);
        if (!plan) {
            _hasError = true;
            const StringView text = SheetGridErrorText(plan.error());
            snprintf(_summary, sizeof(_summary), "%.*s", static_cast<int>(text.size()), text.data());
            return;
        }

        _plan = eastl::move(*plan);
        _hasPlan = true;
        const String summary = Format("{} cells: {} to add, {} to replace, {} skipped", _cells.size(), _plan.entries.size() - _plan.replaced.size(), _plan.replaced.size(), _plan.skipped);
        snprintf(_summary, sizeof(_summary), "%s", summary.c_str());
    }

}  // namespace BECore
