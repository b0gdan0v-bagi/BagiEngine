#include "ComponentInspectors.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <imgui.h>

namespace BECore {

    bool RenderComponentInspector(IComponent* component) {
        if (!component) {
            return false;
        }

        bool changed = false;
        const auto& typeMeta = component->GetTypeMeta();

        if (typeMeta.typeName == eastl::string_view("TransformComponent")) {
            auto& t = static_cast<TransformComponent&>(*component);
            changed |= ImGui::DragFloat("X##transform_x", &t._x, 0.1f);
            changed |= ImGui::DragFloat("Y##transform_y", &t._y, 0.1f);
            changed |= ImGui::DragFloat("Width##transform_width", &t._width, 0.1f);
            changed |= ImGui::DragFloat("Height##transform_height", &t._height, 0.1f);
            return changed;
        }

        if (typeMeta.typeName == eastl::string_view("QuadRendererComponent")) {
            auto& q = static_cast<QuadRendererComponent&>(*component);

            float colorFloat[4] = {
                q._color.r / 255.0f,
                q._color.g / 255.0f,
                q._color.b / 255.0f,
                q._color.a / 255.0f,
            };

            if (ImGui::ColorEdit4("Color##quad_color", colorFloat)) {
                q._color.r = static_cast<uint8_t>(colorFloat[0] * 255.0f);
                q._color.g = static_cast<uint8_t>(colorFloat[1] * 255.0f);
                q._color.b = static_cast<uint8_t>(colorFloat[2] * 255.0f);
                q._color.a = static_cast<uint8_t>(colorFloat[3] * 255.0f);
                changed = true;
            }
            return changed;
        }

        if (typeMeta.typeName == eastl::string_view("SpriteRendererComponent")) {
            auto& s = static_cast<SpriteRendererComponent&>(*component);

            static eastl::vector<PoolString> cachedAssets;
            static bool assetsLoaded = false;

            if (!assetsLoaded) {
                static const eastl::string_view extensions[] = {".png", ".jpg", ".jpeg", ".bmp"};
                cachedAssets = CoreManager::GetFileSystem().EnumerateFiles("assets"_intern, eastl::span<const eastl::string_view>(extensions));
                assetsLoaded = true;
            }

            int currentIndex = -1;
            for (int i = 0; i < static_cast<int>(cachedAssets.size()); ++i) {
                if (cachedAssets[i] == s._texturePath) {
                    currentIndex = i;
                    break;
                }
            }

            if (ImGui::BeginCombo("Texture##sprite_texture", currentIndex >= 0 ? cachedAssets[currentIndex].CStr() : "(none)")) {
                for (int i = 0; i < static_cast<int>(cachedAssets.size()); ++i) {
                    const bool isSelected = (currentIndex == i);
                    if (ImGui::Selectable(cachedAssets[i].CStr(), isSelected)) {
                        s._texturePath = cachedAssets[i];
                        s.ReloadTexture();
                        changed = true;
                        currentIndex = i;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::TextUnformatted("Source Rect:");

            ImGui::PushItemWidth(60.0f);
            if (ImGui::DragFloat("X##srcRect_x", &s._srcRect.x, 0.5f)) {
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::DragFloat("Y##srcRect_y", &s._srcRect.y, 0.5f)) {
                changed = true;
            }
            ImGui::PopItemWidth();

            ImGui::PushItemWidth(60.0f);
            if (ImGui::DragFloat("W##srcRect_w", &s._srcRect.w, 0.5f, 0.0f)) {
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::DragFloat("H##srcRect_h", &s._srcRect.h, 0.5f, 0.0f)) {
                changed = true;
            }
            ImGui::PopItemWidth();

            return changed;
        }

        ImGui::TextDisabled("(no inspector)");
        return false;
    }

}  // namespace BECore
