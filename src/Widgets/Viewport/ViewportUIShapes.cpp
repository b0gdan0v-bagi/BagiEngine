#include "ViewportUIShapes.h"

#include <BECore/UI/UIPreview.h>
#include <Widgets/Viewport/ViewportUIDebug.h>

namespace BECore {

    namespace {

        // One colour per device, in a cycle
        constexpr ImU32 kShapeColors[] = {IM_COL32(255, 170, 60, 255),  IM_COL32(80, 220, 160, 255), IM_COL32(255, 100, 140, 255),
                                          IM_COL32(120, 180, 255, 255), IM_COL32(220, 220, 90, 255), IM_COL32(190, 130, 255, 255)};
        constexpr float kOutlineThickness = 1.5f;
        constexpr float kLabelInset = 3.0f;  // screen units
        constexpr size_t kMaxDevices = 64;   // the width of the mask
        constexpr const char* kSectionTitle = "Selected element in other shapes (not stored)";
        constexpr const char* kLandscapeLabel = "Landscape##ViewportShapesLandscape";
        constexpr const char* kLandscapeTooltip = "Lay the shapes out in landscape";

        bool Contains(const ImVec2& min, const ImVec2& max, const ImVec2& point) {
            return point.x >= min.x && point.x < max.x && point.y >= min.y && point.y < max.y;
        }

    }  // namespace

    void ViewportUIShapes::DrawPopupItems() {
        ImGui::Separator();
        ImGui::TextDisabled("%s", kSectionTitle);
        ImGui::Checkbox(kLandscapeLabel, &_landscape);
        ImGui::SetItemTooltip("%s", kLandscapeTooltip);
        const auto devices = UIPreview::GetDevices();
        for (size_t index = 0; index < devices.size() && index < kMaxDevices; ++index) {
            const uint64_t bit = uint64_t{1} << index;
            bool checked = (_mask & bit) != 0;
            ImGui::PushID(static_cast<int>(index));
            // The name is a StringView of a literal, so it is drawn with its length rather than as a C string
            if (ImGui::Checkbox("##device", &checked)) {
                _mask = checked ? (_mask | bit) : (_mask & ~bit);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(devices[index].name.data(), devices[index].name.data() + devices[index].name.size());
            ImGui::PopID();
        }
    }

    void ViewportUIShapes::Release() {
        _cache.Reset();
        _drawn.clear();
    }

    void ViewportUIShapes::Draw(ImDrawList& drawList, const ViewportFrame& frame, const Vec2& outputSize, UICanvasLayout& layout, const UIElement& element) {
        _drawn.clear();
        if (_mask == 0) {
            return;
        }
        const auto devices = UIPreview::GetDevices();
        const UILayoutContext live = layout.GetContext();
        _contexts.clear();
        _devices.clear();
        for (size_t index = 0; index < devices.size() && index < kMaxDevices; ++index) {
            if ((_mask & (uint64_t{1} << index)) != 0) {
                _contexts.push_back(UIElementShapes::ContextFor(live, devices[index], _landscape));
                _devices.push_back(index);
            }
        }

        const UIElementShapesResult& result = _cache.Get(layout, element, {_contexts.data(), _contexts.size()});
        for (size_t slot = 0; slot < result.rects.size(); ++slot) {
            if (!result.rects[slot]) {
                continue;
            }
            const UILayoutContext& shape = _contexts[slot];
            const Optional<Rect> fitted = UIElementShapes::FitToImage(*result.rects[slot], shape, outputSize.x, outputSize.y);
            if (!fitted) {
                continue;
            }
            const size_t device = _devices[slot];
            const ImVec2 min = frame.ToScreen({fitted->x, fitted->y});
            const ImVec2 max = frame.ToScreen({fitted->x + fitted->w, fitted->y + fitted->h});
            const ImU32 color = kShapeColors[device % std::size(kShapeColors)];
            if (result.approximate) {
                AddDashedRect(drawList, min, max, color);
            } else {
                drawList.AddRect(min, max, color, 0.0f, 0, kOutlineThickness);
            }
            const StringView name = devices[device].name;
            drawList.AddText({min.x + kLabelInset, min.y + kLabelInset}, color, name.data(), name.data() + name.size());
            _drawn.push_back({device, min, max, *result.rects[slot], {shape.outputWidth, shape.outputHeight}, result.approximate});
        }
    }

    bool ViewportUIShapes::ShowTooltip(const ImVec2& mouse) {
        const Drawn* smallest = nullptr;
        float smallestArea = 0.0f;
        for (const Drawn& drawn : _drawn) {
            const float area = (drawn.max.x - drawn.min.x) * (drawn.max.y - drawn.min.y);
            if (Contains(drawn.min, drawn.max, mouse) && (!smallest || area < smallestArea)) {
                smallest = &drawn;
                smallestArea = area;
            }
        }
        if (!smallest) {
            return false;
        }
        const StringView name = UIPreview::GetDevices()[smallest->device].name;
        const Rect& rect = smallest->rect;
        ImGui::SetTooltip("%.*s%s\nx %g  y %g  w %g  h %g\noutput %g x %g", static_cast<int>(name.size()), name.data(), smallest->approximate ? " (approximate: regions are the live shape's)" : "",
                          rect.x, rect.y, rect.w, rect.h, smallest->shapeSize.x, smallest->shapeSize.y);
        return true;
    }

}  // namespace BECore
