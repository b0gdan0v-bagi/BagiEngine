#include "UIControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
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

        Json DescribeElement(const UIElement& element, const UICanvasResolved* resolved) {
            Json json = Json::object();
            json["type"] = MakeJsonString(element.GetTypeMeta().typeName);
            json["name"] = MakeJsonString(element.name.ToStringView());
            json["interactive"] = element.interactive;
            json["blocksInput"] = element.blocksInput;
            json["hovered"] = element.hovered;
            json["pressed"] = element.pressed;
            if (resolved != nullptr) {
                const Rect& rect = element.rect;
                json["visible"] = !rect.IsEmpty();
                json["rect"] = RectToJson(rect);
                json["outputRect"] = RectToJson(resolved->transform.ToOutput(rect.x, rect.y, rect.w, rect.h));
            }
            if (const auto* label = dynamic_cast<const UILabel*>(&element)) {
                json["text"] = MakeJsonString(label->GetDisplayText());
            }
            if (const auto* button = dynamic_cast<const UIButton*>(&element)) {
                json["action"] = MakeJsonString(button->action.ToStringView());
                json["disabled"] = button->disabled;
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

        Json DescribeCanvas(const CanvasEntry& entry) {
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
                list.push_back(DescribeCanvas(entry));
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

        constexpr float kHalf = 0.5f;

        std::pair<float, float> CenterOf(const Rect& rect) {
            return {rect.x + rect.w * kHalf, rect.y + rect.h * kHalf};
        }

        /// Looks for the element to click. A candidate that cannot be clicked is remembered only until a better miss or a hit turns up
        bool FindClickable(const eastl::vector<CanvasEntry>& canvases, PoolString name, std::optional<PoolString> onlyNode, ClickTarget& target, bool& anyNamed) {
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
                    } else if (!element->interactive) {
                        miss = ClickMiss::NotInteractive;
                    } else if (element->rect.IsEmpty()) {
                        miss = ClickMiss::Hidden;
                    } else {
                        const auto [x, y] = CenterOf(element->rect);
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
            std::optional<PoolString> onlyNode;
            if (FindJsonMember(call.Params(), "canvas") != nullptr) {
                const auto nodeText = GetJsonString(call.Params(), "canvas");
                if (!nodeText) {
                    call.Fail(ControlErrors::kInvalidParams, "'canvas' has to be the name of a scene node");
                    return;
                }
                // A name that was never interned belongs to no node; client text does not go into the pool
                onlyNode = PoolString::Find(eastl::string_view(nodeText->c_str(), nodeText->size())).value_or(PoolString{});
            }
            Scene* scene = CoreManager::GetSceneManager().GetActiveScene();
            if (scene == nullptr) {
                call.Fail(UIControlErrors::kNoScene, "there is no active scene");
                return;
            }
            const auto name = PoolString::Find(eastl::string_view(nameText->c_str(), nameText->size()));
            eastl::vector<CanvasEntry> canvases;
            CollectCanvases(scene->GetRootNode(), canvases);

            ClickTarget target;
            bool anyNamed = false;
            if (name && FindClickable(canvases, *name, onlyNode, target, anyNamed)) {
                // Copy what the answer needs first: a click may switch the scene and destroy the canvas
                const auto [centerX, centerY] = CenterOf(target.element->rect);
                const UICanvasResolved& resolved = target.canvas->GetLayout().GetResolved();
                const float outputX = centerX * resolved.transform.scale + resolved.transform.offsetX;
                const float outputY = centerY * resolved.transform.scale + resolved.transform.offsetY;
                const PoolString canvasNode = target.canvasNode;
                const auto* button = dynamic_cast<const UIButton*>(target.element.Get());
                const bool isButton = button != nullptr;
                const PoolString action = isButton ? button->action : PoolString{};

                UIInput& input = CoreManager::GetUIInput();
                SDL_MouseMotionEvent motion{};
                motion.type = SDL_EVENT_MOUSE_MOTION;
                motion.x = outputX;
                motion.y = outputY;
                static_cast<void>(input.DispatchMouseMotion(motion));
                const bool consumed = input.DispatchMouseButtonDown(MakeButtonEvent(outputX, outputY, true));
                static_cast<void>(input.DispatchMouseButtonUp(MakeButtonEvent(outputX, outputY, false)));

                Json result = Json::object();
                result["clicked"] = MakeJsonString(eastl::string_view(nameText->c_str(), nameText->size()));
                result["canvas"] = MakeJsonString(canvasNode.ToStringView());
                result["x"] = outputX;
                result["y"] = outputY;
                result["consumed"] = consumed;
                if (isButton) {
                    result["action"] = MakeJsonString(action.ToStringView());
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
