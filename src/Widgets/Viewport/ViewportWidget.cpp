#include "ViewportWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Generated/ViewportWidget.gen.hpp>
#include <SDL3/SDL.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr int kMouseButtonCount = 3;
        // The ImGui button index (0 left, 1 right, 2 middle) to the SDL button
        constexpr Uint8 kSDLButtons[kMouseButtonCount] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};

        constexpr float kAspectComboWidth = 80.0f;
        constexpr float kSafeAreaComboWidth = 130.0f;
        constexpr float kViewComboWidth = 130.0f;
        // The combo rows before the variants: Auto, then the default view
        constexpr int kViewComboAuto = 0;
        constexpr int kViewComboDefault = 1;
        constexpr int kViewComboFirstVariant = 2;
        constexpr ImU32 kGridColor = IM_COL32(255, 255, 255, 40);
        constexpr ImU32 kSafeAreaOutlineColor = IM_COL32(255, 200, 0, 200);

        // In the order of the enums (ViewportAspect, SafeAreaSimulation)
        constexpr const char* kAspectLabels[] = {"Free", "16:9", "4:3", "21:9", "9:16"};
        constexpr const char* kSafeAreaLabels[] = {"No safe area", "Notch", "Side insets"};
        static_assert(std::size(kAspectLabels) == EnumUtils<ViewportAspect>::Count());
        static_assert(std::size(kSafeAreaLabels) == EnumUtils<SafeAreaSimulation>::Count());

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
    }

    void ViewportWidget::Update() {
        ImGui::SetNextWindowSize({800.0f, 600.0f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Viewport")) {
            ImGui::End();
            return;
        }

        DrawPreviewToolbar();

        // The image is the largest rect of the preset's shape that fits what is left of the panel, centered in it
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const auto [w, h] = UIPreview::FitSize(_aspect, available.x, available.y);
        if (w == 0 || h == 0) {
            ImGui::End();
            return;
        }
        const ImVec2 size{static_cast<float>(w), static_cast<float>(h)};

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
            uiEnvironment.SetSafeAreaOverride(UIPreview::SimulatedSafeArea(_safeAreaSimulation, size.x, size.y));
            // The target has one pixel per ImGui unit (the image is shown at that size), so its pixel density is 1 even when the
            // window runs in the high-density mode, where canvases would otherwise lay out twice as big. A sharper viewport (a target
            // of size times io.DisplayFramebufferScale, the mouse and the safe area scaled with it, the override set to that scale) is a follow-up.
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

            // Display the result as an ImGui image, centered when a preset leaves room on one side
            const ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos({cursor.x + (available.x - size.x) * 0.5f, cursor.y + (available.y - size.y) * 0.5f});
            ImGui::Image(reinterpret_cast<ImTextureID>(_renderTarget->GetImGuiTextureId()), size);

            // Hand the mouse to the scene with viewport-local coordinates, so it can map them to scene units
            // (Scene::ScreenToSceneX/Y) with the fit of its last draw.
            const ImVec2 imageMin = ImGui::GetItemRectMin();
            ForwardMouse(imageMin.x, imageMin.y, ImGui::IsItemHovered());
            const ImVec2 imageMax = ImGui::GetItemRectMax();
            DrawGrid(imageMin.x, imageMin.y, imageMax.x, imageMax.y);
            DrawSafeAreaOutline(imageMin.x, imageMin.y, imageMax.x, imageMax.y);
        }

        ImGui::End();
    }

    void ViewportWidget::Draw() {}

    void ViewportWidget::DrawPreviewToolbar() {
        int aspect = static_cast<int>(_aspect);
        ImGui::SetNextItemWidth(kAspectComboWidth);
        if (ImGui::Combo("##ViewportAspect", &aspect, kAspectLabels, static_cast<int>(std::size(kAspectLabels)))) {
            _aspect = static_cast<ViewportAspect>(aspect);
        }

        ImGui::SameLine();
        int safeArea = static_cast<int>(_safeAreaSimulation);
        ImGui::SetNextItemWidth(kSafeAreaComboWidth);
        if (ImGui::Combo("##ViewportSafeArea", &safeArea, kSafeAreaLabels, static_cast<int>(std::size(kSafeAreaLabels)))) {
            _safeAreaSimulation = static_cast<SafeAreaSimulation>(safeArea);
        }

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

    void ViewportWidget::DrawGrid(float minX, float minY, float maxX, float maxY) const {
        if (!_gridEnabled || _gridStep <= 0) {
            return;
        }
        // A line every _gridStep image pixels from the top-left corner; the image is shown at one pixel per ImGui unit
        const float step = static_cast<float>(_gridStep);
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

    void ViewportWidget::DrawSafeAreaOutline(float minX, float minY, float maxX, float maxY) const {
        if (_safeAreaSimulation == SafeAreaSimulation::None) {
            return;
        }
        const Rect safe = UIPreview::SimulatedSafeArea(_safeAreaSimulation, maxX - minX, maxY - minY);
        ImGui::GetWindowDrawList()->AddRect({minX + safe.x, minY + safe.y}, {minX + safe.x + safe.w, minY + safe.y + safe.h}, kSafeAreaOutlineColor);
    }

    void ViewportWidget::ForwardMouse(float originX, float originY, bool hovered) {
        const ImGuiIO& io = ImGui::GetIO();
        UIInput& uiInput = CoreManager::GetUIInput();

        const float localX = io.MousePos.x - originX;
        const float localY = io.MousePos.y - originY;

        // A press that began in the viewport is followed to its release even when the pointer has left the image
        const bool buttonHeld = std::any_of(std::begin(_pressedInViewport), std::end(_pressedInViewport), [](bool pressed) { return pressed; });

        if ((hovered || buttonHeld) && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
            SDL_MouseMotionEvent motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.x = localX;
            motion.y = localY;
            motion.xrel = io.MouseDelta.x;
            motion.yrel = io.MouseDelta.y;
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
