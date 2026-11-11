#include "ViewportWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIPreviewOverlay.h>
#include <Generated/ViewportWidget.gen.hpp>
#include <SDL3/SDL.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr int kMouseButtonCount = 3;
        // The ImGui button index (0 left, 1 right, 2 middle) to the SDL button
        constexpr Uint8 kSDLButtons[kMouseButtonCount] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};

        constexpr float kSizeComboWidth = 170.0f;
        constexpr float kSizeFieldWidth = 60.0f;
        constexpr float kSafeAreaComboWidth = 130.0f;
        constexpr float kViewComboWidth = 130.0f;
        // The combo rows before the variants: Auto, then the default view
        constexpr int kViewComboAuto = 0;
        constexpr int kViewComboDefault = 1;
        constexpr int kViewComboFirstVariant = 2;
        // The grid is not drawn when its lines would be closer than this on screen (a big target shown small)
        constexpr float kMinDrawnGridStep = 2.0f;
        constexpr ImU32 kGridColor = IM_COL32(255, 255, 255, 40);
        constexpr ImU32 kSafeAreaOutlineColor = IM_COL32(255, 200, 0, 200);
        constexpr ImU32 kViewOutlineColor = IM_COL32(80, 160, 255, 220);
        constexpr ImU32 kViewBarTint = IM_COL32(0, 0, 0, 110);
        constexpr ImU32 kCanvasBoundsColor = IM_COL32(190, 90, 255, 200);
        constexpr ImU32 kContentBoundsColor = IM_COL32(255, 130, 220, 200);
        constexpr ImU32 kTouchOkColor = IM_COL32(60, 220, 90, 230);
        constexpr ImU32 kTouchOkFill = IM_COL32(60, 220, 90, 40);
        constexpr ImU32 kTouchSmallColor = IM_COL32(255, 60, 60, 240);
        constexpr ImU32 kTouchSmallFill = IM_COL32(255, 60, 60, 70);
        constexpr ImU32 kRegionColor = IM_COL32(0, 215, 215, 230);
        constexpr ImU32 kRegionFill = IM_COL32(0, 215, 215, 30);
        constexpr float kRegionLabelInset = 3.0f;  // the gap between the outline of a region and its name, in screen units
        // Bars thinner than this (output pixels) are not tinted
        constexpr float kMinBarSize = 0.5f;
        // Two bounds closer than this (output pixels) are the same rect
        constexpr float kSameRectTolerance = 0.5f;
        // The viewport pass runs at density 1: a window unit is an output pixel, what UISettings::minTouchSize is measured in
        constexpr float kViewportPixelDensity = 1.0f;
        constexpr const char* kOverlaysButton = "Overlays";
        constexpr const char* kOverlaysPopup = "##ViewportOverlays";

        // In the order of the enums (ViewportAspect, SafeAreaSimulation)
        constexpr const char* kAspectLabels[] = {"Free", "16:9", "4:3", "21:9", "9:16"};
        constexpr const char* kSafeAreaLabels[] = {"No safe area", "Notch", "Side insets", "Device"};
        static_assert(std::size(kAspectLabels) == EnumUtils<ViewportAspect>::Count());
        static_assert(std::size(kSafeAreaLabels) == EnumUtils<SafeAreaSimulation>::Count());
        constexpr const char* kCustomLabel = "Custom...";

        // The device of a shape for the Device safe area; none for a custom or an aspect shape and for an index this build lacks
        const PreviewDevice* DeviceOf(const PreviewShape& shape) {
            const auto devices = UIPreview::GetDevices();
            if (shape.custom || shape.device < 0 || static_cast<size_t>(shape.device) >= devices.size()) {
                return nullptr;
            }
            return &devices[static_cast<size_t>(shape.device)];
        }

        // The label of the current shape in the size combo
        eastl::string SizeLabel(const PreviewShape& shape) {
            if (shape.custom) {
                return kCustomLabel;
            }
            if (const PreviewDevice* device = DeviceOf(shape)) {
                return Format("{} ({}x{})", device->name, device->width, device->height);
            }
            return kAspectLabels[static_cast<size_t>(shape.aspect)];
        }

        // The canvases of a subtree in the order of the scene (the walk of CollectCanvases in UIControl.cpp)
        void CollectCanvases(const SceneNode& node, eastl::vector<IntrusivePtr<UICanvasComponent>>& out) {
            if (auto canvas = node.GetComponent<UICanvasComponent>()) {
                out.push_back(std::move(canvas));
            }
            for (const auto& child : node.GetChildren()) {
                CollectCanvases(*child, out);
            }
        }

        // A region drawn this frame, with the rect as the game published it (canvas units) for the tooltip
        struct HoverRegion {
            RegionMark mark;
            Rect canvasRect;
        };

        bool SameRect(const Rect& a, const Rect& b) {
            return std::abs(a.x - b.x) < kSameRectTolerance && std::abs(a.y - b.y) < kSameRectTolerance && std::abs(a.w - b.w) < kSameRectTolerance && std::abs(a.h - b.h) < kSameRectTolerance;
        }

        bool IsFree(const PreviewShape& shape) {
            return !shape.custom && !DeviceOf(shape) && shape.aspect == ViewportAspect::Free;
        }

    }  // namespace

    ViewportWidget::~ViewportWidget() {
        // Update() clears the overrides right after the scene pass; this is the guarantee that no canvas keeps a simulated safe
        // area or the density of the target in Game mode whatever state the widget dies in
        CoreManager::GetUIEnvironment().SetSafeAreaOverride(std::nullopt);
        CoreManager::GetUIEnvironment().SetPixelDensityOverride(std::nullopt);
        // The target's destructor waits for the GPU and gives back its ImGui texture. It needs the device and the ImGui context
        // alive: the widgets die last-first (the viewport before ImGuiWidget) and before the renderer's cleanup
        _renderTarget = {};
    }

    bool ViewportWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorPreferencesChangedEvent, &ViewportWidget::OnPreferencesChanged>(this);
        return true;
    }

    void ViewportWidget::OnPreferencesChanged(const EditorPreferencesChangedEvent& event) {
        _gridEnabled = event.viewportGrid;
        _gridStep = event.viewportGridStep;

        // The stored choice; it is also what we last sent, so applying it sends nothing back
        _storedPreview = event.GetPreview();
        _shape = _storedPreview.shape;
        _safeAreaSimulation = _storedPreview.safeArea;
        _overlays = _storedPreview.overlays;
    }

    void ViewportWidget::EmitPreviewIfChanged() {
        if (_editingSize) {
            return;
        }
        const PreviewSettings current{_shape, _safeAreaSimulation, _overlays};
        if (current == _storedPreview) {
            return;
        }
        _storedPreview = current;
        ViewportPreviewChangedEvent::Emit(current);
    }

    void ViewportWidget::Update() {
        ImGui::SetNextWindowSize({800.0f, 600.0f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Viewport")) {
            ImGui::End();
            return;
        }

        DrawPreviewToolbar();
        EmitPreviewIfChanged();

        // The target is the shape's size (the largest rect of a preset's ratio that fits what is left of the panel, or the exact size
        // of a device or custom size) and is shown at `zoom`, centered in the panel
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const PreviewTarget target = UIPreview::TargetFor(_shape, available.x, available.y);
        const uint32_t w = target.width;
        const uint32_t h = target.height;
        if (w == 0 || h == 0) {
            ImGui::End();
            return;
        }
        const ImVec2 size{static_cast<float>(w), static_cast<float>(h)};
        const float zoom = target.zoom;
        const ImVec2 shown{size.x * zoom, size.y * zoom};

        IRenderer* renderer = CoreManager::GetRenderer().Get();
        if (!renderer) {
            ImGui::End();
            return;
        }

        // Recreate the render target only when the viewport is resized.
        if (!_renderTarget || _lastWidth != w || _lastHeight != h) {
            _renderTarget = renderer->CreateRenderTarget(w, h);
            _lastWidth = w;
            _lastHeight = h;
        }

        if (_renderTarget) {
            // Set the clear color for the offscreen pass, then begin it.
            renderer->Clear(_clearColor);
            renderer->SetRenderTarget(_renderTarget.Get());
            // The target is not the window, so its safe area is the simulated one (the whole target for "No safe area"), never the window's
            UIEnvironment& uiEnvironment = CoreManager::GetUIEnvironment();
            const Rect safeArea = UIPreview::SimulatedSafeArea(_safeAreaSimulation, DeviceOf(_shape), size.x, size.y);
            uiEnvironment.SetSafeAreaOverride(safeArea);
            // The target has one pixel per ImGui unit at zoom 1, so its pixel density is 1 even when the window runs in the
            // high-density mode, where canvases would otherwise lay out twice as big. A sharper viewport (a target of size times
            // io.DisplayFramebufferScale, the mouse and the safe area scaled with it, the override set to that scale) is a follow-up.
            uiEnvironment.SetPixelDensityOverride(1.0f);
            // The forced view lives for this pass only: Update(), Game mode and the other scenes pick by their own output
            Scene* activeScene = CoreManager::GetSceneManager().GetActiveScene();
            ValidateForcedView(activeScene);
            if (activeScene) {
                activeScene->SetViewOverride(_forcedView);
            }
            CoreManager::GetSceneManager().DrawAll();
            if (activeScene) {
                activeScene->SetViewOverride(std::nullopt);
            }
            uiEnvironment.SetSafeAreaOverride(std::nullopt);
            uiEnvironment.SetPixelDensityOverride(std::nullopt);
            renderer->UnsetRenderTarget();

            // Display the result as an ImGui image, centered when the shape leaves room on one side
            const ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos({cursor.x + (available.x - shown.x) * 0.5f, cursor.y + (available.y - shown.y) * 0.5f});
            ImGui::Image(reinterpret_cast<ImTextureID>(_renderTarget->GetImGuiTextureId()), shown);

            // Hand the mouse to the scene with viewport-local coordinates, so it can map them to scene units
            // (Scene::ScreenToSceneX/Y) with the fit of its last draw.
            const ImVec2 imageMin = ImGui::GetItemRectMin();
            ForwardMouse(imageMin.x, imageMin.y, zoom, ImGui::IsItemHovered());
            const ImVec2 imageMax = ImGui::GetItemRectMax();
            const bool imageHovered = ImGui::IsItemHovered();
            DrawGrid(imageMin.x, imageMin.y, imageMax.x, imageMax.y, zoom);
            DrawOverlays(activeScene, imageMin, imageMax, zoom, size, safeArea, imageHovered);
        }

        ImGui::End();
    }

    void ViewportWidget::Draw() {}

    void ViewportWidget::DrawSizeCombo() {
        const auto devices = UIPreview::GetDevices();
        const bool onDevice = DeviceOf(_shape) != nullptr;

        // A device brings its own safe area; any other size drops it. The user can pick another simulation after either
        const auto dropDeviceSafeArea = [this] {
            if (_safeAreaSimulation == SafeAreaSimulation::Device) {
                _safeAreaSimulation = SafeAreaSimulation::None;
            }
        };

        ImGui::SetNextItemWidth(kSizeComboWidth);
        if (ImGui::BeginCombo("##ViewportSize", SizeLabel(_shape).c_str())) {
            for (size_t i = 0; i < std::size(kAspectLabels); ++i) {
                const bool selected = !_shape.custom && !onDevice && static_cast<size_t>(_shape.aspect) == i;
                if (ImGui::Selectable(kAspectLabels[i], selected)) {
                    _shape.aspect = static_cast<ViewportAspect>(i);
                    _shape.device = -1;
                    _shape.custom = false;
                    dropDeviceSafeArea();
                }
            }
            ImGui::Separator();
            for (size_t i = 0; i < devices.size(); ++i) {
                const bool selected = !_shape.custom && _shape.device == static_cast<int32_t>(i);
                const PreviewDevice& device = devices[i];
                if (ImGui::Selectable(Format("{} ({}x{})", device.name, device.width, device.height).c_str(), selected)) {
                    _shape.device = static_cast<int32_t>(i);
                    _shape.custom = false;
                    _safeAreaSimulation = SafeAreaSimulation::Device;
                }
            }
            ImGui::Separator();
            if (ImGui::Selectable(kCustomLabel, _shape.custom)) {
                _shape.custom = true;
                dropDeviceSafeArea();
            }
            ImGui::EndCombo();
        }
    }

    void ViewportWidget::DrawPreviewToolbar() {
        DrawSizeCombo();

        _editingSize = false;
        if (_shape.custom) {
            // The sides are clamped to the range once the field is left, not while typing ("1" on the way to "1000" is not 64)
            const auto sizeField = [this](const char* label, uint32_t& side) {
                int value = static_cast<int>(side);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(kSizeFieldWidth);
                if (ImGui::InputInt(label, &value, 0, 0)) {
                    side = static_cast<uint32_t>(std::max(value, 0));
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    side = std::clamp(side, kMinPreviewSide, kMaxPreviewSide);
                }
                _editingSize = _editingSize || ImGui::IsItemActive();
            };
            sizeField("W", _shape.customWidth);
            sizeField("H", _shape.customHeight);
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(IsFree(_shape));
        ImGui::Checkbox("Rotate", &_shape.rotated);
        ImGui::EndDisabled();

        ImGui::SameLine();
        int safeArea = static_cast<int>(_safeAreaSimulation);
        ImGui::SetNextItemWidth(kSafeAreaComboWidth);
        if (ImGui::Combo("##ViewportSafeArea", &safeArea, kSafeAreaLabels, static_cast<int>(std::size(kSafeAreaLabels)))) {
            _safeAreaSimulation = static_cast<SafeAreaSimulation>(safeArea);
        }

        ImGui::SameLine();
        DrawOverlaysPopup();

        if (const Scene* scene = CoreManager::GetSceneManager().GetActiveScene()) {
            DrawViewCombo(*scene);

            // The view the scene drew with last, picked by the size of this image or forced by the combo (read only)
            const ActiveSceneView view = scene->GetActiveView();
            ImGui::SameLine();
            const eastl::string_view name = view.index < 0 ? eastl::string_view("default") : view.name.ToStringView();
            ImGui::TextDisabled("View: %.*s %.0f x %.0f", static_cast<int>(name.size()), name.data(), view.width, view.height);
        }
    }

    void ViewportWidget::ValidateForcedView(const Scene* scene) {
        if (!_forcedView) {
            return;
        }
        const bool sameScene = scene && scene->GetName() == _forcedViewScene;
        const bool exists = sameScene && *_forcedView >= -1 && *_forcedView < static_cast<int32_t>(scene->GetViewVariants().size());
        if (!exists) {
            _forcedView = std::nullopt;
        }
    }

    void ViewportWidget::DrawViewCombo(const Scene& scene) {
        const eastl::span<const SceneViewVariant> variants = scene.GetViewVariants();
        if (variants.empty()) {
            return;
        }
        ValidateForcedView(&scene);

        const auto variantLabel = [&variants](size_t index) {
            const SceneViewVariant& variant = variants[index];
            return variant.name.Empty() ? Format("#{}", index) : eastl::string(variant.name.ToStringView());
        };

        int current = kViewComboAuto;
        if (_forcedView) {
            current = *_forcedView < 0 ? kViewComboDefault : kViewComboFirstVariant + *_forcedView;
        }
        const eastl::string currentLabel = current == kViewComboAuto      ? eastl::string("Auto")
                                           : current == kViewComboDefault ? eastl::string("Default")
                                                                          : variantLabel(static_cast<size_t>(*_forcedView));

        ImGui::SameLine();
        ImGui::SetNextItemWidth(kViewComboWidth);
        if (ImGui::BeginCombo("##ViewportView", currentLabel.c_str())) {
            if (ImGui::Selectable("Auto", current == kViewComboAuto)) {
                _forcedView = std::nullopt;
            }
            if (ImGui::Selectable("Default", current == kViewComboDefault)) {
                _forcedView = -1;
                _forcedViewScene = scene.GetName();
            }
            for (size_t i = 0; i < variants.size(); ++i) {
                const int row = kViewComboFirstVariant + static_cast<int>(i);
                if (ImGui::Selectable(variantLabel(i).c_str(), current == row)) {
                    _forcedView = static_cast<int32_t>(i);
                    _forcedViewScene = scene.GetName();
                }
            }
            ImGui::EndCombo();
        }
    }

    void ViewportWidget::DrawGrid(float minX, float minY, float maxX, float maxY, float zoom) const {
        if (!_gridEnabled || _gridStep <= 0) {
            return;
        }
        // A line every _gridStep image pixels from the top-left corner; a pixel of the image is zoom ImGui units
        const float step = static_cast<float>(_gridStep) * zoom;
        if (step < kMinDrawnGridStep) {
            return;
        }
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect({minX, minY}, {maxX, maxY}, true);
        for (float x = minX + step; x < maxX; x += step) {
            drawList->AddLine({x, minY}, {x, maxY}, kGridColor);
        }
        for (float y = minY + step; y < maxY; y += step) {
            drawList->AddLine({minX, y}, {maxX, y}, kGridColor);
        }
        drawList->PopClipRect();
    }

    void ViewportWidget::DrawOverlaysPopup() {
        if (ImGui::Button(kOverlaysButton)) {
            ImGui::OpenPopup(kOverlaysPopup);
        }
        if (ImGui::BeginPopup(kOverlaysPopup)) {
            ImGui::Checkbox("Safe area", &_overlays.safeArea);
            ImGui::Checkbox("View bounds", &_overlays.viewBounds);
            ImGui::Checkbox("Canvas bounds", &_overlays.canvasBounds);
            ImGui::Checkbox("Touch targets", &_overlays.touchTargets);
            ImGui::Checkbox("Regions", &_overlays.regions);
            ImGui::EndPopup();
        }
    }

    void ViewportWidget::DrawOverlays(Scene* scene, const ImVec2& imageMin, const ImVec2& imageMax, float zoom, const ImVec2& outputSize, const Rect& safeArea, bool hovered) const {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(imageMin, imageMax, true);

        // Rects are in target pixels, the image is zoom screen units per pixel
        const auto toScreen = [&](const Rect& rect) {
            return std::pair<ImVec2, ImVec2>{{imageMin.x + rect.x * zoom, imageMin.y + rect.y * zoom}, {imageMin.x + (rect.x + rect.w) * zoom, imageMin.y + (rect.y + rect.h) * zoom}};
        };
        const auto fill = [&](const Rect& rect, ImU32 color) {
            if (rect.w > kMinBarSize && rect.h > kMinBarSize) {
                const auto [min, max] = toScreen(rect);
                drawList->AddRectFilled(min, max, color);
            }
        };
        const auto outline = [&](const Rect& rect, ImU32 color) {
            const auto [min, max] = toScreen(rect);
            drawList->AddRect(min, max, color);
        };

        if (scene && _overlays.viewBounds) {
            const ActiveSceneView view = scene->GetActiveView();
            const Rect viewRect = UIPreviewOverlay::ViewRect(view.width, view.height, outputSize.x, outputSize.y);
            const float right = viewRect.x + viewRect.w;
            const float bottom = viewRect.y + viewRect.h;
            fill({0.0f, 0.0f, outputSize.x, viewRect.y}, kViewBarTint);
            fill({0.0f, bottom, outputSize.x, outputSize.y - bottom}, kViewBarTint);
            fill({0.0f, viewRect.y, viewRect.x, viewRect.h}, kViewBarTint);
            fill({right, viewRect.y, outputSize.x - right, viewRect.h}, kViewBarTint);
            outline(viewRect, kViewOutlineColor);
        }

        if (scene && (_overlays.canvasBounds || _overlays.touchTargets || _overlays.regions)) {
            eastl::vector<IntrusivePtr<UICanvasComponent>> canvases;
            CollectCanvases(scene->GetRootNode(), canvases);
            const float minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const TouchMark* hoveredMark = nullptr;
            eastl::vector<TouchMark> marks;
            eastl::vector<HoverRegion> regionHits;
            for (const auto& canvas : canvases) {
                const UICanvasLayout& layout = canvas->GetLayout();
                if (!canvas->_visible || !layout.HasLayout()) {
                    continue;
                }
                if (_overlays.canvasBounds) {
                    const Rect bounds = UIPreviewOverlay::CanvasBounds(layout.GetResolved());
                    const Rect content = UIPreviewOverlay::ContentBounds(layout.GetResolved());
                    outline(bounds, kCanvasBoundsColor);
                    if (!SameRect(bounds, content)) {
                        outline(content, kContentBoundsColor);
                    }
                }
                // Regions go under the touch targets
                if (_overlays.regions) {
                    for (RegionMark& mark : UIPreviewOverlay::RegionMarks(layout)) {
                        const auto [min, max] = toScreen(mark.rect);
                        fill(mark.rect, kRegionFill);
                        drawList->AddRect(min, max, kRegionColor);
                        const eastl::string_view name = mark.name.ToStringView();
                        const ImVec2 textSize = ImGui::CalcTextSize(name.data(), name.data() + name.size());
                        if (max.x - min.x >= textSize.x + kRegionLabelInset * 2.0f) {
                            drawList->AddText({min.x + kRegionLabelInset, min.y + kRegionLabelInset}, kRegionColor, name.data(), name.data() + name.size());
                        }
                        if (const Rect* canvasRect = layout.regions.Find(mark.name)) {
                            regionHits.push_back({std::move(mark), *canvasRect});
                        }
                    }
                }
                if (_overlays.touchTargets) {
                    for (TouchMark& mark : UIPreviewOverlay::TouchTargets(layout, minTouchSize, kViewportPixelDensity)) {
                        marks.push_back(std::move(mark));
                    }
                }
            }
            // The last mark under the pointer is the one drawn on top
            for (const TouchMark& mark : marks) {
                const auto [min, max] = toScreen(mark.rect);
                drawList->AddRectFilled(min, max, mark.tooSmall ? kTouchSmallFill : kTouchOkFill);
                drawList->AddRect(min, max, mark.tooSmall ? kTouchSmallColor : kTouchOkColor);
                if (hovered && mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y) {
                    hoveredMark = &mark;
                }
            }
            if (hoveredMark) {
                const eastl::string_view name = hoveredMark->name.ToStringView();
                ImGui::SetTooltip("%.*s", static_cast<int>(name.size()), name.data());
            } else if (hovered) {
                // The smallest region under the pointer, so a region inside another one stays reachable
                const HoverRegion* smallest = nullptr;
                for (const HoverRegion& hit : regionHits) {
                    const auto [min, max] = toScreen(hit.mark.rect);
                    const bool under = mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y;
                    if (under && (!smallest || hit.mark.rect.w * hit.mark.rect.h < smallest->mark.rect.w * smallest->mark.rect.h)) {
                        smallest = &hit;
                    }
                }
                if (smallest) {
                    const eastl::string_view name = smallest->mark.name.ToStringView();
                    const Rect& rect = smallest->canvasRect;
                    ImGui::SetTooltip("%.*s\nx %g  y %g  w %g  h %g", static_cast<int>(name.size()), name.data(), rect.x, rect.y, rect.w, rect.h);
                }
            }
        }

        drawList->PopClipRect();
        DrawSafeAreaOutline(imageMin.x, imageMin.y, zoom, safeArea);
    }

    void ViewportWidget::DrawSafeAreaOutline(float minX, float minY, float zoom, const Rect& safeArea) const {
        if (!_overlays.safeArea || _safeAreaSimulation == SafeAreaSimulation::None) {
            return;
        }
        ImGui::GetWindowDrawList()->AddRect({minX + safeArea.x * zoom, minY + safeArea.y * zoom}, {minX + (safeArea.x + safeArea.w) * zoom, minY + (safeArea.y + safeArea.h) * zoom},
                                            kSafeAreaOutlineColor);
    }

    void ViewportWidget::ForwardMouse(float originX, float originY, float zoom, bool hovered) {
        const ImGuiIO& io = ImGui::GetIO();
        UIInput& uiInput = CoreManager::GetUIInput();

        // The scene lives in target pixels, the image on screen is zoom times that
        const float localX = (io.MousePos.x - originX) / zoom;
        const float localY = (io.MousePos.y - originY) / zoom;

        // A press that began in the viewport is followed to its release even when the pointer has left the image
        const bool buttonHeld = std::any_of(std::begin(_pressedInViewport), std::end(_pressedInViewport), [](bool pressed) { return pressed; });

        if ((hovered || buttonHeld) && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
            SDL_MouseMotionEvent motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.x = localX;
            motion.y = localY;
            motion.xrel = io.MouseDelta.x / zoom;
            motion.yrel = io.MouseDelta.y / zoom;
            for (int i = 0; i < kMouseButtonCount; ++i) {
                if (io.MouseDown[i]) {
                    motion.state |= SDL_BUTTON_MASK(kSDLButtons[i]);
                }
            }
            uiInput.DispatchMouseMotion(motion);
        }

        for (int i = 0; i < kMouseButtonCount; ++i) {
            const bool pressed = hovered && ImGui::IsMouseClicked(i);
            const bool released = _pressedInViewport[i] && ImGui::IsMouseReleased(i);
            if (!pressed && !released) {
                continue;
            }

            SDL_MouseButtonEvent button{};
            button.button = kSDLButtons[i];
            button.x = localX;
            button.y = localY;
            if (pressed) {
                button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                button.down = true;
                button.clicks = static_cast<Uint8>(io.MouseClickedCount[i]);
                _pressedInViewport[i] = true;
                uiInput.DispatchMouseButtonDown(button);
            } else {
                button.type = SDL_EVENT_MOUSE_BUTTON_UP;
                button.down = false;
                _pressedInViewport[i] = false;
                uiInput.DispatchMouseButtonUp(button);
            }
        }

        if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)) {
            SDL_MouseWheelEvent wheel{};
            wheel.type = SDL_EVENT_MOUSE_WHEEL;
            wheel.x = io.MouseWheelH;
            wheel.y = io.MouseWheel;
            wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            wheel.mouse_x = localX;
            wheel.mouse_y = localY;
            uiInput.DispatchMouseWheel(wheel);
        }
    }

}  // namespace BECore
