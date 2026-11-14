#include "UIControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIChoice.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UIProgressBar.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UISliderComponent.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIToggleComponent.h>
#include <BECore/UI/UITooltipComponent.h>
#include <BECore/UI/UIValueText.h>
#include <SDL3/SDL_mouse.h>

namespace BECore {

    namespace {

        struct CanvasEntry {
            PoolString node;
            IntrusivePtr<UICanvasComponent> canvas;
        };

        /// The canvases of a subtree in the order of the scene: a node, then its children
        void CollectCanvases(const SceneNode& node, eastl::vector<CanvasEntry>& out) {
            if (auto canvas = node.GetComponent<UICanvasComponent>()) {
                out.push_back({node.GetName(), std::move(canvas)});
            }
            for (const auto& child : node.GetChildren()) {
                CollectCanvases(*child, out);
            }
        }

        Json RectToJson(const Rect& rect) {
            Json json = Json::object();
            json["x"] = rect.x;
            json["y"] = rect.y;
            json["w"] = rect.w;
            json["h"] = rect.h;
            return json;
        }

        Json DescribeShadow(const UIShadowSpec& shadow) {
            Json json = Json::object();
            json["offsetX"] = shadow.offsetX;
            json["offsetY"] = shadow.offsetY;
            json["blur"] = shadow.blur;
            json["spread"] = shadow.spread;
            json["color"] = MakeJsonString(UIText::FormatColor(shadow.color));
            return json;
        }

        /// The authored box style, each setting only when it differs from the default (the state shifts of a button are paint, not reported, like `color`)
        Json DescribeBox(const UIBoxStyle& box) {
            const UIBoxStyle defaults;
            Json json = Json::object();
            if (!(box.cornerRadius == defaults.cornerRadius)) {
                json["cornerRadius"] = Json::array({box.cornerRadius.topLeft, box.cornerRadius.topRight, box.cornerRadius.bottomRight, box.cornerRadius.bottomLeft});
            }
            if (box.borderWidth != defaults.borderWidth) {
                json["borderWidth"] = box.borderWidth;
            }
            if (!ColorsEqual(box.borderColor, defaults.borderColor)) {
                json["borderColor"] = MakeJsonString(UIText::FormatColor(box.borderColor));
            }
            if (box.gradientDirection != defaults.gradientDirection || !ColorsEqual(box.gradientColor, defaults.gradientColor)) {
                Json gradient = Json::object();
                gradient["direction"] = MakeJsonString(EnumUtils<UIGradientDirection>::ToString(box.gradientDirection));
                gradient["color"] = MakeJsonString(UIText::FormatColor(box.gradientColor));
                json["gradient"] = std::move(gradient);
            }
            if (!(box.shadow == defaults.shadow)) {
                json["shadow"] = DescribeShadow(box.shadow);
            }
            if (!(box.glow == defaults.glow)) {
                json["glow"] = DescribeShadow(box.glow);
            }
            if (!ColorsEqual(box.highlight, defaults.highlight)) {
                json["highlight"] = MakeJsonString(UIText::FormatColor(box.highlight));
            }
            return json;
        }

        /// The first component of type `T` that `accept` takes, in the descendants of `element` (depth first, in the order of the children). It does not go through an element with a
        /// `UIInputClicker`: that is another button, and what it shows is its own
        template <class T, class Accept>
        const T* FindInDescendants(const UIElement& element, const Accept& accept) {
            for (const auto& child : element.children) {
                if (child->GetComponent<UIInputClicker>() != nullptr) {
                    continue;
                }
                if (const auto* component = child->GetComponent<T>(); component != nullptr && accept(*component)) {
                    return component;
                }
                if (const T* found = FindInDescendants<T>(*child, accept)) {
                    return found;
                }
            }
            return nullptr;
        }

        /// The button-like facts of an element that is composed (a button is not a class there): the type names of its components and what its text, clicker, image and tooltip say, under
        /// the names a button always had. Only what the element has: a plain element gets nothing. A clicker element with no text or image of its own answers
        /// `text` and `icon` from the first descendant that has one (the caption and the icon child of a button made of children)
        void DescribeComponents(const UIElement& element, Json& json) {
            if (element.components.empty()) {
                return;
            }
            Json types = Json::array();
            for (const auto& component : element.components) {
                types.push_back(MakeJsonString(component->GetTypeMeta().typeName));
            }
            json["components"] = std::move(types);
            const bool hasClicker = element.GetComponent<UIInputClicker>() != nullptr;
            const UITextComponent* caption = element.GetComponent<UITextComponent>();
            if (caption == nullptr && hasClicker) {
                caption = FindInDescendants<UITextComponent>(element, [](const UITextComponent&) { return true; });
            }
            if (caption != nullptr) {
                json["text"] = MakeJsonString(caption->GetDisplayText(element));  // as shown: a dropdown's header says the chosen caption
            }
            if (const auto* clicker = element.GetComponent<UIInputClicker>()) {
                json["action"] = MakeJsonString(clicker->action.ToStringView());
                json["disabled"] = element.IsDisabled();
                json["value"] = MakeJsonString(clicker->value.ToStringView());
                json["selected"] = element.selected;
            }
            const UIImageComponent* image = element.GetComponent<UIImageComponent>();
            if (image == nullptr && hasClicker) {
                image = FindInDescendants<UIImageComponent>(element, [](const UIImageComponent& found) { return !found.sprite.Empty(); });
            }
            if (image != nullptr && !image->sprite.Empty()) {
                json["icon"] = MakeJsonString(image->sprite.ToStringView());
            }
            const eastl::string& tooltip = element.GetTooltipText();
            if (!tooltip.empty()) {
                json["tooltip"] = MakeJsonString(StringView(tooltip));
            }
        }

        Json DescribeElement(UIElement& element, const UICanvasResolved* resolved) {
            Json json = Json::object();
            json["type"] = MakeJsonString(element.GetTypeMeta().typeName);
            json["name"] = MakeJsonString(element.name.ToStringView());
            json["interactive"] = element.IsInteractive();
            json["blocksInput"] = element.blocksInput;
            json["hovered"] = element.hovered;
            json["pressed"] = element.pressed;
            if (resolved != nullptr) {
                const Rect& rect = element.rect;
                json["visible"] = !rect.IsEmpty();
                json["rect"] = RectToJson(rect);
                json["outputRect"] = RectToJson(resolved->transform.ToOutput(rect.x, rect.y, rect.w, rect.h));
            }
            if (!element.active.rect.region.Empty()) {
                // The name, published or not: a client reads the cause of a MissingRegion in `issues` here
                json["region"] = MakeJsonString(element.active.rect.region.ToStringView());
            }
            if (!(element.box == UIBoxStyle{})) {
                json["box"] = DescribeBox(element.box);
            }
            DescribeComponents(element, json);
            if (auto* group = element.Cast<UISegmented>()) {
                // The segments take `selected` and `groupDisabled` at the draw; apply them now so the tree shows the variable as it is, not as of the last draw
                group->SyncChildren();
                json["bind"] = MakeJsonString(group->bind.ToStringView());
                json["selectedValue"] = MakeJsonString(group->GetSelectedValue());
                json["disabled"] = group->disabled;
            }
            if (auto* dropdown = element.Cast<UIDropdown>()) {
                // Like the group: the options take `selected` now, and the children below are described after this, so they show the variable as it is
                dropdown->SyncChildren();
                json["bind"] = MakeJsonString(dropdown->bind.ToStringView());
                json["selectedValue"] = MakeJsonString(dropdown->GetSelectedValue());
                json["open"] = dropdown->IsPopupOpen();
                json["action"] = MakeJsonString(dropdown->action.ToStringView());
                json["disabled"] = dropdown->disabled;
            }
            if (const auto* bar = element.Cast<UIProgressBar>()) {
                json["bind"] = MakeJsonString(bar->bind.ToStringView());
                json["minValue"] = bar->minValue;
                json["maxValue"] = bar->maxValue;
                json["fraction"] = bar->GetFraction();
            }
            // The toggle and the slider read the variable now, not as of the last draw. After `DescribeComponents`, so the `action` they carry replaces the empty one of the clicker beside
            // them. A toggle's `text` is its caption, found by `DescribeComponents` below the row's clicker
            if (const auto* toggle = element.GetComponent<UIToggleComponent>()) {
                json["bind"] = MakeJsonString(toggle->bind.ToStringView());
                json["onValue"] = MakeJsonString(toggle->onValue.ToStringView());
                json["offValue"] = MakeJsonString(toggle->offValue.ToStringView());
                json["checked"] = toggle->IsOn();
                json["action"] = MakeJsonString(toggle->action.ToStringView());
                json["disabled"] = element.IsDisabled();
            }
            if (const auto* slider = element.GetComponent<UISliderComponent>()) {
                json["bind"] = MakeJsonString(slider->bind.ToStringView());
                json["minValue"] = slider->minValue;
                json["maxValue"] = slider->maxValue;
                json["step"] = slider->step;
                json["value"] = slider->GetValue();
                json["fraction"] = slider->GetFraction();
                json["action"] = MakeJsonString(slider->action.ToStringView());
                json["disabled"] = element.IsDisabled();
            }
            Json children = Json::array();
            for (const auto& child : element.children) {
                children.push_back(DescribeElement(*child, resolved));
            }
            json["children"] = std::move(children);
            return json;
        }

        Json DescribeIssue(const UILayoutIssue& issue) {
            Json json = Json::object();
            json["kind"] = MakeJsonString(EnumUtils<UILayoutIssueKind>::ToString(issue.kind));
            json["element"] = MakeJsonString(issue.element.ToStringView());
            json["other"] = MakeJsonString(issue.other.ToStringView());
            json["rect"] = RectToJson(issue.rect);
            return json;
        }

        /// The regions the game published, also before the first draw (they do not depend on one)
        Json DescribeRegions(const UIRegionSet& regions) {
            Json json = Json::object();
            for (const UIRegionSet::Entry& entry : regions) {
                json[std::string(entry.name.ToStringView().data(), entry.name.ToStringView().size())] = RectToJson(entry.rect);
            }
            return json;
        }

        Json DescribeCanvas(const CanvasEntry& entry, const Scene& scene) {
            const UICanvasComponent& canvas = *entry.canvas;
            const UICanvasLayout& layout = canvas.GetLayout();
            Json json = Json::object();
            json["node"] = MakeJsonString(entry.node.ToStringView());
            json["space"] = MakeJsonString(EnumUtils<CanvasSpace>::ToString(canvas._space));
            json["visible"] = canvas._visible;
            json["modal"] = canvas._modal;
            json["inputEnabled"] = canvas._inputEnabled;
            json["takesInput"] = canvas.TakesInput();
            json["inputOrder"] = canvas.GetInputOrder();
            json["laidOut"] = layout.HasLayout();
            json["regions"] = DescribeRegions(layout.regions);
            if (canvas._space == CanvasSpace::SceneView) {
                // The view the scene drew with last: the canvas is laid out against it
                const ActiveSceneView view = scene.GetActiveView();
                Json viewJson = Json::object();
                viewJson["name"] = MakeJsonString(view.name.ToStringView());
                viewJson["width"] = view.width;
                viewJson["height"] = view.height;
                json["view"] = std::move(viewJson);
            }

            const UICanvasResolved* resolved = layout.HasLayout() ? &layout.GetResolved() : nullptr;
            if (resolved != nullptr) {
                json["scale"] = resolved->scale;
                json["bounds"] = RectToJson(resolved->bounds);
                json["safeBounds"] = RectToJson(resolved->safeBounds);
                json["contentBounds"] = RectToJson(resolved->contentBounds);
            }
            Json elements = Json::array();
            for (const auto& child : layout.root->children) {
                elements.push_back(DescribeElement(*child, resolved));
            }
            json["elements"] = std::move(elements);

            Json issues = Json::array();
            if (resolved != nullptr) {
                UILayoutValidatorOptions options;
                options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
                options.pixelDensity = layout.GetContext().pixelDensity;
                for (const UILayoutIssue& issue : layout.Validate(options)) {
                    issues.push_back(DescribeIssue(issue));
                }
            }
            json["issues"] = std::move(issues);
            return json;
        }

        void HandleUITree(ControlCall& call) {
            Scene* scene = CoreManager::GetSceneManager().GetActiveScene();
            if (scene == nullptr) {
                call.Fail(UIControlErrors::kNoScene, "there is no active scene");
                return;
            }
            eastl::vector<CanvasEntry> canvases;
            CollectCanvases(scene->GetRootNode(), canvases);

            Json list = Json::array();
            for (const CanvasEntry& entry : canvases) {
                list.push_back(DescribeCanvas(entry, *scene));
            }
            Json result = Json::object();
            result["scene"] = MakeJsonString(scene->GetName().ToStringView());
            result["canvases"] = std::move(list);
            call.Reply(std::move(result));
        }

        /// Why no element of this name can be clicked, kept while the canvases are searched so that the answer names the closest miss
        enum class ClickMiss : uint8_t { None, NotInteractive, Hidden, Covered, CanvasCannotTakeInput, NotLaidOut };

        struct ClickTarget {
            IntrusivePtr<UICanvasComponent> canvas;
            IntrusivePtr<UIElement> element;
            PoolString canvasNode;
            ClickMiss miss = ClickMiss::None;
            PoolString coveredBy;
        };

        /// The elements of @p root named @p searched in depth first order
        void CollectNamed(UIElement& root, PoolString searched, eastl::vector<UIElement*>& out) {
            if (root.name == searched) {
                out.push_back(&root);
            }
            for (const auto& child : root.children) {
                CollectNamed(*child, searched, out);
            }
        }

        /// The dropdown that has @p option among its children (an option: `UIChoice::IsOption`), searched in @p root; null when @p option is not a child of any
        UIDropdown* FindDropdownOf(UIElement& root, const UIElement* option) {
            if (auto* dropdown = root.Cast<UIDropdown>()) {
                for (const auto& child : dropdown->children) {
                    if (child.Get() == option) {
                        return dropdown;
                    }
                }
            }
            for (const auto& child : root.children) {
                if (UIDropdown* found = FindDropdownOf(*child, option)) {
                    return found;
                }
            }
            return nullptr;
        }

        /// Whether @p searched is @p root or lies below it
        bool IsSameOrBelow(const UIElement& root, const UIElement& searched) {
            if (&root == &searched) {
                return true;
            }
            for (const auto& child : root.children) {
                if (IsSameOrBelow(*child, searched)) {
                    return true;
                }
            }
            return false;
        }

        /// Whether a press on @p element only closes the open list of its canvas: a list is open and the element is neither the dropdown that owns it (its header) nor below it.
        /// The canvas consumes such a press (UICanvasComponent::OnMouseButtonDown), so the element emits nothing
        bool PressOnlyClosesList(const UICanvasLayout& layout, const UIElement& element) {
            const eastl::vector<UIElement*> open = layout.OpenPopups();
            if (open.empty()) {
                return false;
            }
            for (const UIElement* popup : open) {
                if (IsSameOrBelow(*popup, element)) {
                    return false;
                }
            }
            return true;
        }

        constexpr float kHalf = 0.5f;

        /// The press never lands on the very edge of a rect: the right and bottom edges are outside it for the hit test, and the round trip through output pixels may move a point by a
        /// rounding error. 0 and 1 of `at` are this far inside
        constexpr float kEdgeInset = 0.01f;
        constexpr float kDefaultAt = 0.5f;

        /// The point of a press: @p at (0..1) along the width, the middle of the height
        std::pair<float, float> PointAt(const Rect& rect, float at) {
            const float travel = std::max(rect.w - 2.0f * kEdgeInset, 0.0f);
            return {rect.x + (rect.w - travel) * kHalf + travel * at, rect.y + rect.h * kHalf};
        }

        /// Looks for the element to click. A candidate that cannot be clicked is remembered only until a better miss or a hit turns up
        bool FindClickable(const eastl::vector<CanvasEntry>& canvases, PoolString name, Optional<PoolString> onlyNode, float at, ClickTarget& target, bool& anyNamed) {
            anyNamed = false;
            for (const CanvasEntry& entry : canvases) {
                if (onlyNode && entry.node != *onlyNode) {
                    continue;
                }
                eastl::vector<UIElement*> named;
                CollectNamed(*entry.canvas->GetLayout().root, name, named);
                for (UIElement* element : named) {
                    anyNamed = true;
                    ClickMiss miss = ClickMiss::None;
                    PoolString coveredBy;
                    if (!entry.canvas->GetLayout().HasLayout()) {
                        miss = ClickMiss::NotLaidOut;
                    } else if (!entry.canvas->TakesInput()) {
                        miss = ClickMiss::CanvasCannotTakeInput;
                    } else if (!element->IsInteractive()) {
                        miss = ClickMiss::NotInteractive;
                    } else if (element->rect.IsEmpty()) {
                        miss = ClickMiss::Hidden;
                    } else {
                        const auto [x, y] = PointAt(element->rect, at);
                        const UIElement* top = entry.canvas->GetLayout().HitTest(x, y);
                        if (top != element) {
                            miss = ClickMiss::Covered;
                            coveredBy = top != nullptr ? top->name : PoolString{};
                        }
                    }
                    if (miss == ClickMiss::None) {
                        target = {entry.canvas, IntrusivePtr<UIElement>(element), entry.node, ClickMiss::None, {}};
                        return true;
                    }
                    if (target.miss == ClickMiss::None) {  // the first miss is the one reported
                        target = {entry.canvas, IntrusivePtr<UIElement>(element), entry.node, miss, coveredBy};
                    }
                }
            }
            return false;
        }

        eastl::string DescribeMiss(const ClickTarget& target, PoolString name) {
            const auto text = name.ToStringView();
            switch (target.miss) {
                case ClickMiss::NotInteractive:
                    return Format("'{}' is not interactive (not a button)", text);
                case ClickMiss::Hidden:
                    return Format("'{}' is hidden", text);
                case ClickMiss::Covered:
                    return Format("'{}' is covered by '{}' in its canvas", text, target.coveredBy.ToStringView());
                case ClickMiss::CanvasCannotTakeInput:
                    return Format("the canvas '{}' of '{}' is hidden, has input off or is in a scene that is not active", target.canvasNode.ToStringView(), text);
                case ClickMiss::NotLaidOut:
                    return Format("the canvas '{}' of '{}' has not been drawn yet: step a frame first", target.canvasNode.ToStringView(), text);
                case ClickMiss::None:
                    break;
            }
            return Format("'{}' cannot be clicked", text);
        }

        SDL_MouseButtonEvent MakeButtonEvent(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.clicks = 1;
            event.x = x;
            event.y = y;
            return event;
        }

        void HandleUIClick(ControlCall& call) {
            const auto nameText = GetJsonString(call.Params(), "name");
            if (!nameText || nameText->empty()) {
                call.Fail(ControlErrors::kInvalidParams, "'name' has to be the name of an element");
                return;
            }
            Optional<PoolString> onlyNode;
            if (FindJsonMember(call.Params(), "canvas") != nullptr) {
                const auto nodeText = GetJsonString(call.Params(), "canvas");
                if (!nodeText) {
                    call.Fail(ControlErrors::kInvalidParams, "'canvas' has to be the name of a scene node");
                    return;
                }
                // A name that was never interned belongs to no node; client text does not go into the pool
                onlyNode = PoolString::Find(StringView(nodeText->c_str(), nodeText->size())).value_or(PoolString{});
            }
            float at = kDefaultAt;
            if (FindJsonMember(call.Params(), "at") != nullptr) {
                const auto atNumber = GetJsonNumber(call.Params(), "at");
                if (!atNumber || !(*atNumber >= 0.0 && *atNumber <= 1.0)) {
                    call.Fail(ControlErrors::kInvalidParams, "'at' has to be a number from 0 to 1 (the horizontal position inside the element)");
                    return;
                }
                at = static_cast<float>(*atNumber);
            }
            Scene* scene = CoreManager::GetSceneManager().GetActiveScene();
            if (scene == nullptr) {
                call.Fail(UIControlErrors::kNoScene, "there is no active scene");
                return;
            }
            const auto name = PoolString::Find(StringView(nameText->c_str(), nameText->size()));
            eastl::vector<CanvasEntry> canvases;
            CollectCanvases(scene->GetRootNode(), canvases);

            ClickTarget target;
            bool anyNamed = false;
            if (name && FindClickable(canvases, *name, onlyNode, at, target, anyNamed)) {
                // Copy what the answer needs first: a click may switch the scene and destroy the canvas
                const auto [pressX, pressY] = PointAt(target.element->rect, at);
                const UICanvasResolved& resolved = target.canvas->GetLayout().GetResolved();
                const float outputX = pressX * resolved.transform.scale + resolved.transform.offsetX;
                const float outputY = pressY * resolved.transform.scale + resolved.transform.offsetY;
                const PoolString canvasNode = target.canvasNode;
                // A composed button answers as a button; a disabled one emits nothing, so the answer carries nothing
                const auto* clicker = target.element->GetComponent<UIInputClicker>();
                const bool clickerEmits = clicker != nullptr && !target.element->IsDisabled();
                const PoolString clickerAction = clickerEmits ? clicker->action : PoolString{};
                const PoolString clickerValue = clickerEmits ? clicker->value : PoolString{};
                // A toggle reports the value it asks for, read before the click (a handler may set the variable at once)
                const auto* toggleComponent = target.element->GetComponent<UIToggleComponent>();
                PoolString toggleAction;
                PoolString toggleValue;
                if (toggleComponent != nullptr) {
                    toggleAction = toggleComponent->action;
                    toggleValue = toggleComponent->IsOn() ? toggleComponent->offValue : toggleComponent->onValue;
                }
                // The slider: what the press emitted is read after the click
                const auto* sliderComponent = target.element->GetComponent<UISliderComponent>();
                // An option of a dropdown emits the dropdown's action with its own value (the option's `action` is empty), nothing when the choice is refused; the header emits nothing
                const UIDropdown* owner = UIChoice::IsOption(*target.element) ? FindDropdownOf(*target.canvas->GetLayout().root, target.element.Get()) : nullptr;
                const bool optionEmits = owner != nullptr && owner->EmitsOnChoice(*target.element);
                const PoolString optionAction = optionEmits ? owner->action : PoolString{};
                const PoolString optionValue = optionEmits ? UIChoice::GetValue(*target.element) : PoolString{};
                const bool onlyClosesList = PressOnlyClosesList(target.canvas->GetLayout(), *target.element);

                UIInput& input = CoreManager::GetUIInput();
                SDL_MouseMotionEvent motion{};
                motion.type = SDL_EVENT_MOUSE_MOTION;
                motion.x = outputX;
                motion.y = outputY;
                static_cast<void>(input.DispatchMouseMotion(motion));
                const bool consumed = input.DispatchMouseButtonDown(MakeButtonEvent(outputX, outputY, true));
                static_cast<void>(input.DispatchMouseButtonUp(MakeButtonEvent(outputX, outputY, false)));

                Json result = Json::object();
                result["clicked"] = MakeJsonString(StringView(nameText->c_str(), nameText->size()));
                result["canvas"] = MakeJsonString(canvasNode.ToStringView());
                result["x"] = outputX;
                result["y"] = outputY;
                result["consumed"] = consumed;
                if (owner != nullptr) {
                    result["action"] = MakeJsonString(optionAction.ToStringView());
                    result["value"] = MakeJsonString(optionValue.ToStringView());
                } else if (toggleComponent != nullptr) {
                    result["action"] = MakeJsonString(toggleAction.ToStringView());
                    result["value"] = MakeJsonString(toggleValue.ToStringView());
                } else if (sliderComponent != nullptr) {
                    result["action"] = MakeJsonString(sliderComponent->action.ToStringView());
                    result["value"] = MakeJsonString(sliderComponent->GetLastEmittedValue().ToStringView());
                } else if (clicker != nullptr) {
                    // After the toggle and the slider: their clicker has no action of its own, and must not shadow them
                    result["action"] = MakeJsonString(clickerAction.ToStringView());
                    result["value"] = MakeJsonString(clickerValue.ToStringView());
                }
                if (onlyClosesList && result.contains("action")) {
                    // The press only closed the open list and reached nothing below it: no action, no value
                    result["action"] = MakeJsonString(StringView{});
                    result["value"] = MakeJsonString(StringView{});
                }
                call.Reply(std::move(result));
                return;
            }
            if (!anyNamed) {
                call.Fail(UIControlErrors::kNotFound, Format("no element named '{}' in the canvases of scene '{}'", nameText->c_str(), scene->GetName().ToStringView()));
                return;
            }
            call.Fail(UIControlErrors::kNotClickable, DescribeMiss(target, *name));
        }

    }  // namespace

    void RegisterUIControl(ControlService& service) {
        service.RegisterHandler("ui_tree", HandleUITree);
        service.RegisterHandler("ui_click", HandleUIClick);
    }

}  // namespace BECore
