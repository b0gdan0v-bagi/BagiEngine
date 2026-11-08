#include "ViewportWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Renderer/IRenderer.h>
#include <Generated/ViewportWidget.gen.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr int kMouseButtonCount = 3;
        // The ImGui button index (0 left, 1 right, 2 middle) to the SDL button
        constexpr Uint8 kSDLButtons[kMouseButtonCount] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};

        constexpr float kAspectComboWidth = 80.0f;
        constexpr float kSafeAreaComboWidth = 130.0f;
        constexpr ImU32 kSafeAreaOutlineColor = IM_COL32(255, 200, 0, 200);

        // In the order of the enums (ViewportAspect, SafeAreaSimulation)
        constexpr const char* kAspectLabels[] = {"Free", "16:9", "4:3", "21:9", "9:16"};
        constexpr const char* kSafeAreaLabels[] = {"No safe area", "Notch", "Side insets"};
        static_assert(std::size(kAspectLabels) == EnumUtils<ViewportAspect>::Count());
        static_assert(std::size(kSafeAreaLabels) == EnumUtils<SafeAreaSimulation>::Count());

    }  // namespace

    ViewportWidget::~ViewportWidget() {
        // Update() clears the override right after the scene pass; this is the guarantee that no canvas keeps a simulated safe
        // area in Game mode whatever state the widget dies in
        CoreManager::GetUIEnvironment().SetSafeAreaOverride(std::nullopt);
        // The target's destructor waits for the GPU and gives back its ImGui texture; the manager waited already before the widgets died
        _renderTarget = {};
    }

    bool ViewportWidget::Initialize(IDeserializer& /*deserializer*/) {
        return true;
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
            CoreManager::GetSceneManager().DrawAll();
            uiEnvironment.SetSafeAreaOverride(std::nullopt);
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
