#include "ComponentInspectors.h"

#include "ImGuiPropertyVisitor.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/ActionsLibrary.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/Components/ChangeToRandomColorDebugComponent.h>
#include <BECore/Scene/Components/ClickableComponent.h>
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

            // -----------------------------------------------------------------
            // Sprite ID — pick from TextureLibrary
            // -----------------------------------------------------------------
            const auto& entries = CoreManager::GetTextureLibrary().GetAll();

            int currentSpriteIndex = -1;
            for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
                if (entries[i].name == s._spriteId) {
                    currentSpriteIndex = i;
                    break;
                }
            }

            const char* spritePreview = currentSpriteIndex >= 0 ? entries[currentSpriteIndex].name.CStr() : "(none)";
            if (ImGui::BeginCombo("Sprite##sprite_id", spritePreview)) {
                if (ImGui::Selectable("(none)", currentSpriteIndex < 0)) {
                    s._spriteId = PoolString{};
                    s.ReloadTexture();
                    changed = true;
                    currentSpriteIndex = -1;
                }
                for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
                    const bool isSelected = (currentSpriteIndex == i);
                    if (ImGui::Selectable(entries[i].name.CStr(), isSelected)) {
                        s._spriteId = entries[i].name;
                        s.ReloadTexture();
                        changed = true;
                        currentSpriteIndex = i;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            // -----------------------------------------------------------------
            // Texture path + srcRect — read-only when a sprite ID is active
            // -----------------------------------------------------------------
            const bool lockedBySprite = !s._spriteId.Empty();
            if (lockedBySprite) {
                ImGui::BeginDisabled();
            }

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

            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh##tex")) {
                assetsLoaded = false;
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

            if (lockedBySprite) {
                ImGui::EndDisabled();
            }

            return changed;
        }

        const bool _registeredSpriteRenderer = [] {
            RegisterComponentInspector<SpriteRendererComponent>(eastl::function<bool(SpriteRendererComponent&)>(InspectSpriteRenderer));
            return true;
        }();

        // -----------------------------------------------------------------
        // Shared helper: action combo + optional "Add new action" input
        // noneLabel — displayed when current is empty ("(none)" / "(any)")
        // showAdd   — show text field + button to register a new action
        // -----------------------------------------------------------------
        bool ActionCombo(const char* label, PoolString& current,
                         const char* noneLabel, bool showAdd) {
            const auto& entries = ActionsLibrary::GetInstance().GetAll();
            bool changed = false;

            int currentIndex = -1;
            for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
                if (entries[i] == current) {
                    currentIndex = i;
                    break;
                }
            }

            const char* preview = currentIndex >= 0 ? entries[currentIndex].CStr() : noneLabel;
            if (ImGui::BeginCombo(label, preview)) {
                if (ImGui::Selectable(noneLabel, currentIndex < 0)) {
                    current = PoolString{};
                    changed = true;
                }
                for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
                    const bool isSelected = (currentIndex == i);
                    if (ImGui::Selectable(entries[i].CStr(), isSelected)) {
                        current = entries[i];
                        changed = true;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (showAdd) {
                static char newActionBuf[128] = {};
                ImGui::InputText("##new_action", newActionBuf, sizeof(newActionBuf));
                ImGui::SameLine();
                if (ImGui::SmallButton("Add")) {
                    if (newActionBuf[0] != '\0') {
                        ActionsLibrary::GetInstance().Add(PoolString::Intern(newActionBuf));
                        newActionBuf[0] = '\0';
                    }
                }
            }

            return changed;
        }

        bool InspectClickable(ClickableComponent& c) {
            return ActionCombo("Action##clickable_action", c._actionName, "(none)", true);
        }

        const bool _registeredClickable = [] {
            RegisterComponentInspector<ClickableComponent>(eastl::function<bool(ClickableComponent&)>(InspectClickable));
            return true;
        }();

        bool InspectChangeToRandomColor(ChangeToRandomColorDebugComponent& c) {
            return ActionCombo("Filter##random_color_filter", c._actionFilter, "(any)", false);
        }

        const bool _registeredChangeToRandomColor = [] {
            RegisterComponentInspector<ChangeToRandomColorDebugComponent>(eastl::function<bool(ChangeToRandomColorDebugComponent&)>(InspectChangeToRandomColor));
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
