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

    }  // namespace

    bool ViewportWidget::Initialize(IDeserializer& /*deserializer*/) {
        return true;
    }

    void ViewportWidget::Update() {
        ImGui::SetNextWindowSize({800.0f, 600.0f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Viewport")) {
            ImGui::End();
            return;
        }

        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x < 1.0f || size.y < 1.0f) {
            ImGui::End();
            return;
        }

        const auto w = static_cast<uint32_t>(size.x);
        const auto h = static_cast<uint32_t>(size.y);

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
            CoreManager::GetSceneManager().DrawAll();
            renderer->UnsetRenderTarget();

            // Display the result as an ImGui image.
            ImGui::Image(reinterpret_cast<ImTextureID>(_renderTarget->GetImGuiTextureId()), size);

            // Hand the mouse to the scene with viewport-local coordinates, so it can map them to scene units
            // (Scene::ScreenToSceneX/Y) with the fit of its last draw.
            const ImVec2 imageMin = ImGui::GetItemRectMin();
            ForwardMouse(imageMin.x, imageMin.y, ImGui::IsItemHovered());
        }

        ImGui::End();
    }

    void ViewportWidget::Draw() {}

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
