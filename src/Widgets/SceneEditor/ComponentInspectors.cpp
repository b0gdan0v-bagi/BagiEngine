#include "ComponentInspectors.h"

#include <BECore/Reflection/ClassMeta.h>

#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <imgui.h>

namespace BECore {

    bool RenderComponentInspector(IntrusivePtr<IComponent> component) {
        if (!component) {
            return false;
        }

        bool changed = false;
        const auto& typeMeta = component->GetTypeMeta();

        // TransformComponent: four DragFloat fields
        if (typeMeta.typeName == eastl::string_view("TransformComponent")) {
            auto transform = IntrusivePtr<TransformComponent>(static_cast<TransformComponent*>(component.Get()));
            if (ImGui::DragFloat("X##transform_x", &transform->_x, 0.1f)) {
                changed = true;
            }
            if (ImGui::DragFloat("Y##transform_y", &transform->_y, 0.1f)) {
                changed = true;
            }
            if (ImGui::DragFloat("Width##transform_width", &transform->_width, 0.1f)) {
                changed = true;
            }
            if (ImGui::DragFloat("Height##transform_height", &transform->_height, 0.1f)) {
                changed = true;
            }
            return changed;
        }

        // QuadRendererComponent: ColorEdit4
        if (typeMeta.typeName == eastl::string_view("QuadRendererComponent")) {
            auto quad = IntrusivePtr<QuadRendererComponent>(static_cast<QuadRendererComponent*>(component.Get()));

            // Convert Color (uint8) to float[4]
            float colorFloat[4] = {
                quad->_color.r / 255.0f,
                quad->_color.g / 255.0f,
                quad->_color.b / 255.0f,
                quad->_color.a / 255.0f,
            };

            if (ImGui::ColorEdit4("Color##quad_color", colorFloat)) {
                // Convert back to uint8
                quad->_color.r = static_cast<unsigned char>(colorFloat[0] * 255.0f);
                quad->_color.g = static_cast<unsigned char>(colorFloat[1] * 255.0f);
                quad->_color.b = static_cast<unsigned char>(colorFloat[2] * 255.0f);
                quad->_color.a = static_cast<unsigned char>(colorFloat[3] * 255.0f);
                changed = true;
            }
            return changed;
        }

        // Unknown component type
        ImGui::TextDisabled("(no inspector)");
        return false;
    }

}  // namespace BECore
