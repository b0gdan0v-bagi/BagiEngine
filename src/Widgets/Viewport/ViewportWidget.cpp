#include "ViewportWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Renderer/IRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Generated/ViewportWidget.gen.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>

namespace BECore {

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
            _lastWidth    = w;
            _lastHeight   = h;
        }

        if (_renderTarget) {
            // Set the clear color for the offscreen pass, then begin it.
            renderer->Clear(_clearColor);
            renderer->SetRenderTarget(_renderTarget.Get());
            CoreManager::GetSceneManager().DrawAll();
            renderer->UnsetRenderTarget();

            // Display the result as an ImGui image.
            ImGui::Image(reinterpret_cast<ImTextureID>(_renderTarget->GetImGuiTextureId()), size);

            // Re-emit mouse button events with viewport-local coordinates so
            // ClickableComponent (which works in scene space) receives correct coords.
            if (ImGui::IsItemHovered()) {
                const ImVec2 imgMin   = ImGui::GetItemRectMin();
                const ImVec2 mousePos = ImGui::GetMousePos();
                const float  localX   = mousePos.x - imgMin.x;
                const float  localY   = mousePos.y - imgMin.y;

                static constexpr Uint8 sdlButtons[] = {
                    SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE
                };
                for (int i = 0; i < 3; ++i) {
                    if (ImGui::IsMouseClicked(i)) {
                        SDL_MouseButtonEvent btn{};
                        btn.type   = SDL_EVENT_MOUSE_BUTTON_DOWN;
                        btn.button = sdlButtons[i];
                        btn.down   = true;
                        btn.x      = localX;
                        btn.y      = localY;
                        SDLEvents::MouseButtonDownEvent::Emit(btn);
                    }
                }
            }
        }

        ImGui::End();
    }

    void ViewportWidget::Draw() {
    }

}  // namespace BECore
