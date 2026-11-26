#include "ComponentInspectors.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/ActionsLibrary.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/Components/ChangeToRandomColorDebugComponent.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <EASTL/unordered_map.h>
#include <Widgets/AssetBrowser/AssetDrag.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Feedback/TouchFlashDraw.h>
#include <Widgets/Feedback/TouchHighlights.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/SceneEditor/ComponentDefaults.h>
#include <Widgets/SceneEditor/DefaultMarkDraw.h>
#include <Widgets/SceneEditor/ImGuiPropertyVisitor.h>
#include <Widgets/SceneEditor/NumberField.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kSourceRectFieldWidth = 60.0f;
        /// Pixels of the source rect per pixel of mouse travel
        constexpr double kSourceRectDragSpeed = 0.5;

        HashMap<uint64_t, InspectorFunc>& GetRegistry() {
            static HashMap<uint64_t, InspectorFunc> reg;
            return reg;
        }

        /// The marks of a component whose inspector does not draw field by field, each flashing with its field
        void DrawMarkLines(const ComponentTouches& touches) {
            touches.highlights.ForEachMark(touches.nodeId, touches.component, [&touches](StringView field, const ControlClient& client) {
                DrawTouchMark(client, field, field);
                FillItemWithTouchFlash(touches.highlights.FieldAlpha(touches.nodeId, touches.component, field));
            });
        }

        /// The differences of a component whose inspector does not draw field by field, a line each; the field whose arrow was clicked, or empty
        StringView DrawDifferenceLines(Span<const ComponentFieldDifference> differences) {
            StringView clicked;
            for (const ComponentFieldDifference& difference : differences) {
                const float left = ImGui::GetCursorScreenPos().x;
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%.*s differs from the default", static_cast<int>(difference.field.size()), difference.field.data());
                if (DrawDefaultMark(difference, left, ImGui::GetItemRectMin().y - ImGui::GetStyle().FramePadding.y, ImGui::GetItemRectMax().y)) {
                    clicked = difference.field;
                }
            }
            return clicked;
        }

        bool DrawSourceRectNumber(StringView name, const char* id, float& value) {
            FieldPresentation::Presentation presentation = FieldPresentation::Describe(FieldAttributes{}, FieldPresentation::NumberKind::Float);
            presentation.speed = kSourceRectDragSpeed;
            return DrawNumber(name, id, presentation, value, ImGuiDataType_Float);
        }

        bool InspectSpriteRenderer(SpriteRendererComponent& s) {
            bool changed = false;

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

            // Texture path and srcRect are read-only while a sprite ID is set
            const bool lockedBySprite = !s._spriteId.Empty();
            if (lockedBySprite) {
                ImGui::BeginDisabled();
            }

            static Vector<PoolString> cachedAssets;
            static bool assetsLoaded = false;

            if (!assetsLoaded) {
                static const StringView extensions[] = {".png", ".jpg", ".jpeg", ".bmp"};
                cachedAssets = CoreManager::GetFileSystem().EnumerateFiles(MountPoints::assets, Span<const StringView>(extensions));
                assetsLoaded = true;
            }

            int currentIndex = -1;
            for (int i = 0; i < static_cast<int>(cachedAssets.size()); ++i) {
                if (cachedAssets[i] == s._texturePath) {
                    currentIndex = i;
                    break;
                }
            }

            // The path itself: a dropped file may be one the cached list does not have
            if (ImGui::BeginCombo("Texture##sprite_texture", s._texturePath.Empty() ? "(none)" : s._texturePath.CStr())) {
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
            if (const Optional<AssetDragPayload> dropped = AcceptAssetDrop(AssetKind::Texture)) {
                s._texturePath = PoolString::Intern(StringView(dropped->path));
                s.ReloadTexture();
                changed = true;
            }

            ImGui::SameLine();
            if (IconButton(ICON_LC_REFRESH_CW, "Rescan the assets folder for textures", "tex_refresh")) {
                assetsLoaded = false;
            }

            ImGui::TextUnformatted("Source Rect:");

            ImGui::PushItemWidth(kSourceRectFieldWidth);
            changed |= DrawSourceRectNumber("x", "srcRect_x", s._srcRect.x);
            ImGui::SameLine();
            changed |= DrawSourceRectNumber("y", "srcRect_y", s._srcRect.y);
            changed |= DrawSourceRectNumber("w", "srcRect_w", s._srcRect.w);
            ImGui::SameLine();
            changed |= DrawSourceRectNumber("h", "srcRect_h", s._srcRect.h);
            ImGui::PopItemWidth();

            if (lockedBySprite) {
                ImGui::EndDisabled();
            }

            // Fit is the node's choice, not the library's, so it stays editable with a sprite ID
            const auto& fitNames = EnumUtils<SpriteFit>::Names();
            const auto& fitValues = EnumUtils<SpriteFit>::Values();
            if (ImGui::BeginCombo("Fit##sprite_fit", EnumUtils<SpriteFit>::ToString(s._fit).data())) {
                for (size_t i = 0; i < fitNames.size(); ++i) {
                    const bool isSelected = (s._fit == fitValues[i]);
                    if (ImGui::Selectable(fitNames[i].data(), isSelected)) {
                        s._fit = fitValues[i];
                        changed = true;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            // Blend and flip are the node's choices too (the rotation is on the TransformComponent)
            const auto& blendNames = EnumUtils<BlendMode>::Names();
            const auto& blendValues = EnumUtils<BlendMode>::Values();
            if (ImGui::BeginCombo("Blend##sprite_blend", EnumUtils<BlendMode>::ToString(s._blend).data())) {
                for (size_t i = 0; i < blendNames.size(); ++i) {
                    const bool isSelected = (s._blend == blendValues[i]);
                    if (ImGui::Selectable(blendNames[i].data(), isSelected)) {
                        s._blend = blendValues[i];
                        changed = true;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::Checkbox("Flip X##sprite_flip_x", &s._flipX)) {
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Flip Y##sprite_flip_y", &s._flipY)) {
                changed = true;
            }

            return changed;
        }

        const bool _registeredSpriteRenderer = [] {
            RegisterComponentInspector<SpriteRendererComponent>(InspectSpriteRenderer);
            return true;
        }();

        // Action combo; noneLabel is shown when current is empty ("(none)" / "(any)"), showAdd adds a text field and
        // a button to register a new action
        bool ActionCombo(const char* label, PoolString& current, const char* noneLabel, bool showAdd) {
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
                if (IconTextButton(ICON_LC_PLUS, "Add", "Add the name to the actions library")) {
                    if (newActionBuf[0] != '\0') {
                        ActionsLibrary::GetInstance().Add(PoolString::Intern(StringView(newActionBuf)));
                        newActionBuf[0] = '\0';
                    }
                }
            }

            return changed;
        }

        bool InspectClickable(ClickableComponent& c) {
            bool changed = ActionCombo("Action##clickable_action", c._actionName, "(none)", true);

            const auto& shapeNames = EnumUtils<ClickableShape>::Names();
            const auto& shapeValues = EnumUtils<ClickableShape>::Values();
            if (ImGui::BeginCombo("Shape##clickable_shape", EnumUtils<ClickableShape>::ToString(c._shape).data())) {
                for (size_t i = 0; i < shapeNames.size(); ++i) {
                    const bool isSelected = (c._shape == shapeValues[i]);
                    if (ImGui::Selectable(shapeNames[i].data(), isSelected)) {
                        c._shape = shapeValues[i];
                        changed = true;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::Checkbox("Blocks input##clickable_blocks_input", &c._blocksInput)) {
                changed = true;
            }
            return changed;
        }

        const bool _registeredClickable = [] {
            RegisterComponentInspector<ClickableComponent>(InspectClickable);
            return true;
        }();

        bool InspectChangeToRandomColor(ChangeToRandomColorDebugComponent& c) {
            return ActionCombo("Filter##random_color_filter", c._actionFilter, "(any)", false);
        }

        const bool _registeredChangeToRandomColor = [] {
            RegisterComponentInspector<ChangeToRandomColorDebugComponent>(InspectChangeToRandomColor);
            return true;
        }();

    }  // namespace

    void RegisterComponentInspector(uint64_t typeHash, InspectorFunc func) {
        GetRegistry()[typeHash] = eastl::move(func);
    }

    bool RenderComponentInspector(IComponent* component, const ComponentTouches* touches, ComponentDefaults* defaults) {
        if (!component) {
            return false;
        }

        const Span<const ComponentFieldDifference> differences = defaults ? defaults->Compare(*component) : Span<const ComponentFieldDifference>{};
        StringView reset;
        bool changed = false;
        const auto it = GetRegistry().find(component->GetTypeMeta().typeHash);
        if (it != GetRegistry().end()) {
            if (touches) {
                DrawMarkLines(*touches);
            }
            reset = DrawDifferenceLines(differences);
            changed = it->second(component);
            if (changed && touches) {
                touches->highlights.DismissComponent(touches->nodeId, touches->component);
            }
        } else {
            ImGuiPropertyVisitor visitor(touches, differences);
            changed = component->AcceptPropertyVisitor(visitor);
            reset = visitor.ResetRequest();
        }

        // After the inspector has run, which reads the differences and may hold references into the fields
        if (!reset.empty() && defaults->Reset(*component, reset)) {
            changed = true;
            if (touches) {
                touches->highlights.Dismiss(touches->nodeId, touches->component, reset);
            }
        }
        return changed;
    }

}  // namespace BECore
