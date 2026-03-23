#include "ViewportWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Renderer/IRenderer.h>
#include <Generated/ViewportWidget.gen.hpp>
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
        }

        ImGui::End();
    }

    void ViewportWidget::Draw() {
    }

}  // namespace BECore
