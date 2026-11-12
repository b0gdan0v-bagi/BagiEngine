#include "UIRectInspector.h"

#include <BECore/UI/UIAnchorPresets.h>
#include <BECore/UI/UILayout.h>
#include <BECore/UI/UIRectEdit.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kFieldWidth = 72.0f;
        constexpr float kPairWidth = 150.0f;
        constexpr float kPresetCell = 30.0f;
        constexpr float kPresetDotPad = 5.0f;  // a point anchor is drawn as a square of twice this
        constexpr float kPresetInset = 4.0f;
        constexpr float kAnchorDragSpeed = 0.005f;
        constexpr float kPercentDragSpeed = 0.1f;
        constexpr float kUnitDragSpeed = 1.0f;
        constexpr float kFlexDragSpeed = 0.05f;
        constexpr float kUnitSwitchSpacing = 2.0f;  // between a number and its px / % button
        constexpr size_t kRegionBufferSize = 128;
        constexpr ImU32 kCellColor = IM_COL32(60, 60, 66, 255);
        constexpr ImU32 kCellCurrentColor = IM_COL32(60, 90, 140, 255);
        constexpr ImU32 kCellHoverColor = IM_COL32(80, 80, 90, 255);
        constexpr ImU32 kCellFrameColor = IM_COL32(150, 150, 160, 255);
        constexpr ImU32 kCellAnchorColor = IM_COL32(240, 200, 80, 255);

        /// Drag fields in the mono font with their name after them in the UI font (ImGui draws the label of a drag field with
        /// the font of the field, so @p id is hidden: it starts with `##`). One to four floats; the width is the next item width.
        bool DragFloatsNamed(const char* name, const char* id, float* values, int count, float speed, const char* format) {
            bool changed = false;
            {
                const ScopedMonoFont mono;
                changed = ImGui::DragScalarN(id, ImGuiDataType_Float, values, count, speed, nullptr, nullptr, format);
            }
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextUnformatted(name);
            return changed;
        }

        const UIElement* FindParent(const UIElement& root, const UIElement& target) {
            for (const auto& child : root.children) {
                if (child.Get() == &target) {
                    return &root;
                }
                if (const UIElement* found = FindParent(*child.Get(), target)) {
                    return found;
                }
            }
            return nullptr;
        }

        bool Contains(const UIElement& root, const UIElement* target) {
            for (const auto& child : root.children) {
                if (child.Get() == target || Contains(*child.Get(), target)) {
                    return true;
                }
            }
            return false;
        }

        /// The box the element lays out in, as UITreeLayout picks it: the published region it names, the whole canvas for `ignoreSafeArea` under the root, else the parent
        Rect BoxOf(const UICanvasComponent& canvas, const UIElement& parent, const UIElement& element) {
            if (const Rect* region = canvas.GetRegions().Find(element.style.rect.region)) {
                return *region;
            }
            if (&parent == canvas.GetLayout().root.Get() && element.ignoreSafeArea) {
                return canvas.GetLayout().GetResolved().bounds;
            }
            return parent.rect;
        }

        void DrawPresetCell(ImDrawList& draw, const ImVec2& origin, const UIAnchorPreset& preset, bool current, bool hovered) {
            const ImVec2 end(origin.x + kPresetCell, origin.y + kPresetCell);
            draw.AddRectFilled(origin, end, current ? kCellCurrentColor : hovered ? kCellHoverColor : kCellColor);
            const float inner = kPresetCell - 2.0f * kPresetInset;
            const ImVec2 frameMin(origin.x + kPresetInset, origin.y + kPresetInset);
            draw.AddRect(frameMin, ImVec2(frameMin.x + inner, frameMin.y + inner), kCellFrameColor);

            // The anchors as a box: a point is a small square, a stretched axis a bar from min to max
            auto axis = [&](float lo, float hi, float start) {
                const float a = start + inner * lo;
                const float b = start + inner * hi;
                return lo == hi ? std::pair{a - kPresetDotPad * 0.5f, a + kPresetDotPad * 0.5f} : std::pair{a, b};
            };
            const auto [x0, x1] = axis(preset.anchorMin.x, preset.anchorMax.x, frameMin.x);
            const auto [y0, y1] = axis(preset.anchorMin.y, preset.anchorMax.y, frameMin.y);
            draw.AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kCellAnchorColor);
        }

        /// The grid of presets in the open popup. @return the preset clicked, or nullptr
        const UIAnchorPreset* DrawPresetGrid(const UIAnchorPreset* current) {
            ImGui::TextDisabled("Shift: also the pivot, Alt: also the position");
            const UIAnchorPreset* clicked = nullptr;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            for (size_t row = 0; row < UIAnchorPresets::kRows; ++row) {
                for (size_t column = 0; column < UIAnchorPresets::kColumns; ++column) {
                    const UIAnchorPreset& preset = UIAnchorPresets::At(row, column);
                    ImGui::PushID(static_cast<int>(row * UIAnchorPresets::kColumns + column));
                    const ImVec2 origin = ImGui::GetCursorScreenPos();
                    if (ImGui::InvisibleButton("##preset", ImVec2(kPresetCell, kPresetCell))) {
                        clicked = &preset;
                    }
                    DrawPresetCell(*draw, origin, preset, current == &preset, ImGui::IsItemHovered());
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", UIAnchorPresets::Name(preset));
                    }
                    ImGui::PopID();
                    if (column + 1 < UIAnchorPresets::kColumns) {
                        ImGui::SameLine();
                    }
                }
            }
            return clicked;
        }

        /// A number with its unit switch: px and % of the parent's extent on this axis. The switch converts the number so that the layout stays.
        bool InspectLength(const char* label, UILength& length, float parentExtent) {
            bool changed = false;
            ImGui::PushID(label);
            ImGui::SetNextItemWidth(kFieldWidth);
            const float speed = length.unit == UIUnit::Percent ? kPercentDragSpeed : kUnitDragSpeed;
            {
                const ScopedMonoFont mono;
                changed |= ImGui::DragFloat("##value", &length.value, speed, 0.0f, 0.0f, "%.2f");
            }
            ImGui::SameLine(0.0f, kUnitSwitchSpacing);
            const bool percent = length.unit == UIUnit::Percent;
            if (ImGui::SmallButton(percent ? "%" : "px")) {
                if (const auto converted = UIRectEdit::ConvertUnit(length, percent ? UIUnit::Pixels : UIUnit::Percent, parentExtent)) {
                    length = *converted;
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Switch px / %% (the layout stays); needs a parent with a size");
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(label);
            ImGui::PopID();
            return changed;
        }

        bool InspectVec2(const char* name, const char* id, Vec2& value) {
            float values[2] = {value.x, value.y};
            ImGui::SetNextItemWidth(kPairWidth);
            if (DragFloatsNamed(name, id, values, 2, kAnchorDragSpeed, "%.3f")) {
                value = Vec2(values[0], values[1]);
                return true;
            }
            return false;
        }

        /// @p spec with these anchors, kept where it is laid out now (UIRectEdit), written as the shorthand when they are one of the ten
        UIRectEditResult WithAnchors(const Rect& box, const UIRectSpec& spec, const Vec2& min, const Vec2& max, const Vec2& pivot) {
            const Rect wanted = UILayout::ResolveRect(box, spec);
            UIRectSpec candidate = spec;
            candidate.anchorMin = min;
            candidate.anchorMax = max;
            candidate.pivot = pivot;
            UIRectEditResult result = UIRectEdit::SpecForRect(box, candidate, wanted);
            UIAnchorPresets::Simplify(result.spec);
            return result;
        }

        bool InspectRegion(const UICanvasComponent& canvas, UIRectSpec& spec) {
            bool changed = false;
            const auto choices = UIRectEdit::RegionChoices(canvas.GetRegions(), spec.region);
            ImGui::SetNextItemWidth(kPairWidth);
            if (ImGui::BeginCombo("Region##region", spec.region.Empty() ? "(none)" : spec.region.CStr())) {
                if (ImGui::Selectable("(none)", spec.region.Empty())) {
                    spec.region = PoolString{};
                    changed = true;
                }
                for (const PoolString& name : choices) {
                    const bool published = canvas.GetRegions().Find(name) != nullptr;
                    const eastl::string label = Format("{}{}", name.CStr(), published ? "" : " (not published)");
                    if (ImGui::Selectable(label.c_str(), name == spec.region)) {
                        spec.region = name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }

            // A name typed by hand: applied on Enter or when the field loses focus, so the string pool does not fill with half-typed names
            static char buffer[kRegionBufferSize] = {};
            if (!ImGui::IsAnyItemActive()) {
                snprintf(buffer, sizeof(buffer), "%s", spec.region.CStr());
            }
            ImGui::SetNextItemWidth(kPairWidth);
            ImGui::InputText("Name##region_name", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const PoolString typed = PoolString::Intern(eastl::string_view(buffer));
                if (typed != spec.region) {
                    spec.region = typed;
                    changed = true;
                }
            }
            return changed;
        }

        size_t CountOverridingVariants(const UICanvasComponent& canvas, const UIElement& element) {
            if (element.name.Empty()) {
                return 0;
            }
            size_t count = 0;
            for (const UIVariant& variant : canvas.GetLayout().variants) {
                for (const UIElementOverride& entry : variant.overrides) {
                    if (entry.element == element.name) {
                        ++count;
                        break;
                    }
                }
            }
            return count;
        }

        void DrawElementNode(UIElement& element, IntrusivePtr<UIElement>& selected) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
            if (element.children.empty()) {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (selected.Get() == &element) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            const char* label = element.name.Empty() ? "(unnamed)" : element.name.CStr();
            const bool open = ImGui::TreeNodeEx(&element, flags, "%s", label);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                selected = IntrusivePtr<UIElement>(&element);
            }
            if (open) {
                for (const auto& child : element.children) {
                    DrawElementNode(*child.Get(), selected);
                }
                ImGui::TreePop();
            }
        }

    }  // namespace

    void DrawUIElementTree(UICanvasComponent& canvas, IntrusivePtr<UIElement>& selected) {
        UIElement& root = canvas.GetRoot();
        if (selected && !Contains(root, selected.Get())) {
            selected = nullptr;
        }
        if (root.children.empty()) {
            ImGui::TextDisabled("The canvas has no elements");
            return;
        }
        for (const auto& child : root.children) {
            DrawElementNode(*child.Get(), selected);
        }
    }

    bool InspectUIRect(UICanvasComponent& canvas, UIElement& element, IntrusivePtr<UIElement>& inexactElement) {
        const UIElement* parent = FindParent(canvas.GetRoot(), element);
        if (!parent) {
            ImGui::TextDisabled("The element is not in this canvas");
            return false;
        }
        if (!canvas.GetLayout().HasLayout()) {
            ImGui::TextDisabled("The canvas has not been laid out yet");
            return false;
        }

        UIRectSpec& spec = element.style.rect;
        bool changed = false;

        const size_t variantCount = CountOverridingVariants(canvas, element);
        if (variantCount > 0) {
            ImGui::TextColored(GetImGuiPalette().warning, "%zu variant(s) of this canvas override this element: the shape in effect may differ from this style", variantCount);
        }

        if (parent->layout == UILayoutKind::Stack) {
            ImGui::TextDisabled("The parent is a stack: the anchors, offsets and margin are ignored");
            changed |= InspectLength("width", spec.width, parent->rect.w);
            changed |= InspectLength("height", spec.height, parent->rect.h);
            changed |= DragFloatsNamed("flex", "##flex", &spec.flex, 1, kFlexDragSpeed, "%.2f");
            if (changed) {
                canvas.Invalidate();
            }
            return changed;
        }

        const Rect box = BoxOf(canvas, *parent, element);
        const bool hasBox = !box.IsEmpty();
        if (!hasBox) {
            ImGui::TextColored(GetImGuiPalette().warning, "The box of the element has no size now (a hidden parent?): the anchor presets and the unit switch are off");
        }

        // ---- Preset ----
        const UIAnchorPreset* current = UIAnchorPresets::Match(spec);
        ImGui::BeginDisabled(!hasBox);
        const eastl::string presetLabel = Format("Anchor: {}##preset_button", current ? UIAnchorPresets::Name(*current) : "Custom");
        if (ImGui::Button(presetLabel.c_str())) {
            ImGui::OpenPopup("##anchor_presets");
        }
        if (ImGui::BeginPopup("##anchor_presets")) {
            if (const UIAnchorPreset* clicked = DrawPresetGrid(current)) {
                const ImGuiIO& io = ImGui::GetIO();
                const UIAnchorApplyMode mode{.setPivot = io.KeyShift, .setPosition = io.KeyAlt};
                const UIRectEditResult result = UIAnchorPresets::Apply(box, spec, *clicked, mode);
                spec = result.spec;
                inexactElement = result.exact ? IntrusivePtr<UIElement>() : IntrusivePtr<UIElement>(&element);
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();

        // ---- Anchors and pivot ----
        const UIEffectiveAnchors anchors = UILayout::EffectiveAnchors(spec);
        Vec2 anchorMin = anchors.min;
        Vec2 anchorMax = anchors.max;
        Vec2 pivot = anchors.pivot;
        bool anchorsEdited = false;
        ImGui::BeginDisabled(!hasBox);
        anchorsEdited |= InspectVec2("Anchor min", "##anchor_min", anchorMin);
        anchorsEdited |= InspectVec2("Anchor max", "##anchor_max", anchorMax);
        anchorsEdited |= InspectVec2("Pivot", "##pivot", pivot);
        ImGui::EndDisabled();
        if (anchorsEdited) {
            const UIRectEditResult result = WithAnchors(box, spec, anchorMin, anchorMax, pivot);
            spec = result.spec;
            inexactElement = result.exact ? IntrusivePtr<UIElement>() : IntrusivePtr<UIElement>(&element);
            changed = true;
        }

        const bool anchorsChanged = changed;

        // ---- Offsets, sizes, margins: an axis that stretches ignores its offset and size, a point axis ignores its margin ----
        const UIEffectiveAnchors now = UILayout::EffectiveAnchors(spec);
        const bool stretchX = now.min.x < now.max.x;
        const bool stretchY = now.min.y < now.max.y;

        ImGui::BeginDisabled(stretchX);
        changed |= InspectLength("x", spec.x, box.w);
        changed |= InspectLength("width", spec.width, box.w);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(stretchY);
        changed |= InspectLength("y", spec.y, box.h);
        changed |= InspectLength("height", spec.height, box.h);
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!stretchX);
        float horizontal[2] = {spec.margin.left, spec.margin.right};
        ImGui::SetNextItemWidth(kPairWidth);
        if (DragFloatsNamed("Margin left, right", "##margin_x", horizontal, 2, kUnitDragSpeed, "%.1f")) {
            spec.margin.left = horizontal[0];
            spec.margin.right = horizontal[1];
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!stretchY);
        float vertical[2] = {spec.margin.top, spec.margin.bottom};
        ImGui::SetNextItemWidth(kPairWidth);
        if (DragFloatsNamed("Margin top, bottom", "##margin_y", vertical, 2, kUnitDragSpeed, "%.1f")) {
            spec.margin.top = vertical[0];
            spec.margin.bottom = vertical[1];
            changed = true;
        }
        ImGui::EndDisabled();

        changed |= InspectRegion(canvas, spec);
        if (changed && !anchorsChanged) {
            inexactElement = nullptr;
        }

        if (inexactElement.Get() == &element) {
            ImGui::TextColored(GetImGuiPalette().warning, "The limits or the aspect ratio keep the element from staying exactly where it was");
        }
        if (changed) {
            canvas.Invalidate();
        }
        return changed;
    }

}  // namespace BECore
