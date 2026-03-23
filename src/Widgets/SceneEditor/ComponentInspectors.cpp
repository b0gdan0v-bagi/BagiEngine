#include "ComponentInspectors.h"

#include "ImGuiPropertyVisitor.h"

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <EASTL/unordered_map.h>
#include <imgui.h>

namespace BECore {

    namespace {

        eastl::unordered_map<uint64_t, InspectorFunc>& GetRegistry() {
            static eastl::unordered_map<uint64_t, InspectorFunc> reg;
            return reg;
        }

        bool InspectSpriteRenderer(SpriteRendererComponent& s) {
            bool changed = false;

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

        const bool _registeredSpriteRenderer = [] {
            RegisterComponentInspector<SpriteRendererComponent>(eastl::function<bool(SpriteRendererComponent&)>(InspectSpriteRenderer));
            return true;
        }();

    }  // namespace

    void RegisterComponentInspector(uint64_t typeHash, InspectorFunc func) {
        GetRegistry()[typeHash] = eastl::move(func);
    }

    bool RenderComponentInspector(IComponent* component) {
        if (!component) {
            return false;
        }

        const auto it = GetRegistry().find(component->GetTypeMeta().typeHash);
        if (it != GetRegistry().end()) {
            return it->second(component);
        }

        ImGuiPropertyVisitor visitor;
        return component->AcceptPropertyVisitor(visitor);
    }

}  // namespace BECore
