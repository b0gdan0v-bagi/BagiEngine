#include "ComponentInspectors.h"

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
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

        ImGui::TextDisabled("(no inspector)");
        return false;
    }

}  // namespace BECore
