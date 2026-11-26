#include "UIControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/InputControl.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIChoice.h>
#include <BECore/UI/UIControlAnswers.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIEnvironment.h>
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
        void CollectCanvases(const SceneNode& node, Vector<CanvasEntry>& out) {
            if (auto canvas = node.GetComponent<UICanvasComponent>()) {
                out.push_back({node.GetName(), std::move(canvas)});
            }
            for (const auto& child : node.GetChildren()) {
                CollectCanvases(*child, out);
            }
        }

        ArenaString ArenaText(StringView text) {
            return ArenaString(text.data(), text.size());
        }

        constexpr size_t kCornerCount = 4;

        UIShadowAnswer BuildShadow(const UIShadowSpec& shadow) {
            UIShadowAnswer answer;
            answer.blur = shadow.blur;
            answer.color = ArenaText(UIText::FormatColor(shadow.color));
            answer.offsetX = shadow.offsetX;
            answer.offsetY = shadow.offsetY;
            answer.spread = shadow.spread;
            return answer;
        }

        /// The authored box style, each setting only when it differs from the default (the state shifts of a button are paint, not reported, like `color`). Nothing for the default style
        Optional<UIBoxAnswer> BuildBox(const UIBoxStyle& box) {
            const UIBoxStyle defaults;
            if (box == defaults) {
                return std::nullopt;
            }
            UIBoxAnswer answer;
            if (!(box.cornerRadius == defaults.cornerRadius)) {
                ArenaVector<double> radii;
                radii.reserve(kCornerCount);
                radii.push_back(box.cornerRadius.topLeft);
                radii.push_back(box.cornerRadius.topRight);
                radii.push_back(box.cornerRadius.bottomRight);
                radii.push_back(box.cornerRadius.bottomLeft);
                answer.cornerRadius = std::move(radii);
            }
            if (box.borderWidth != defaults.borderWidth) {
                answer.borderWidth = box.borderWidth;
            }
            if (!ColorsEqual(box.borderColor, defaults.borderColor)) {
                answer.borderColor = ArenaText(UIText::FormatColor(box.borderColor));
            }
            if (box.gradientDirection != defaults.gradientDirection || !ColorsEqual(box.gradientColor, defaults.gradientColor)) {
                UIBoxGradientAnswer gradient;
                gradient.color = ArenaText(UIText::FormatColor(box.gradientColor));
                gradient.direction = box.gradientDirection;
                answer.gradient = std::move(gradient);
            }
            if (!(box.shadow == defaults.shadow)) {
                answer.shadow = BuildShadow(box.shadow);
            }
            if (!(box.glow == defaults.glow)) {
                answer.glow = BuildShadow(box.glow);
            }
            if (!ColorsEqual(box.highlight, defaults.highlight)) {
                answer.highlight = ArenaText(UIText::FormatColor(box.highlight));
            }
            return answer;
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

        /// The button-like facts of an element that is composed (a button is not a class): the type names of its components and what its text, clicker, image and tooltip say.
        /// Only what the element has: a plain element gets nothing. A clicker element with no text or image of its own answers
        /// `text` and `icon` from the first descendant that has one (the caption and the icon child of a button made of children)
        void FillComponents(const UIElement& element, UITreeElement& out) {
            if (element.components.empty()) {
                return;
            }
            ArenaVector<TextView> types;
            types.reserve(element.components.size());
            for (const auto& component : element.components) {
                types.push_back(TextView(component->GetTypeMeta().typeName));
            }
            out.components = std::move(types);
            const bool hasClicker = element.GetComponent<UIInputClicker>() != nullptr;
            const UITextComponent* caption = element.GetComponent<UITextComponent>();
            if (caption == nullptr && hasClicker) {
                caption = FindInDescendants<UITextComponent>(element, [](const UITextComponent&) { return true; });
            }
            if (caption != nullptr) {
                out.text = ArenaText(caption->GetDisplayText(element));  // as shown: a dropdown's header says the chosen caption
            }
            if (const auto* clicker = element.GetComponent<UIInputClicker>()) {
                out.action = clicker->action;
                out.disabled = element.IsDisabled();
                out.value = clicker->value;
                out.selected = element.selected;
            }
            const UIImageComponent* image = element.GetComponent<UIImageComponent>();
            if (image == nullptr && hasClicker) {
                image = FindInDescendants<UIImageComponent>(element, [](const UIImageComponent& found) { return !found.sprite.Empty(); });
            }
            if (image != nullptr && !image->sprite.Empty()) {
                out.icon = image->sprite;
            }
            const String& tooltip = element.GetTooltipText();
            if (!tooltip.empty()) {
                out.tooltip = TextView(tooltip);
            }
        }

        /// A deeper tree is cut at this depth: the generated writer fails the whole answer past `JsonWriter::kMaxDepth`, and a real screen is far from the cap, so it only
        /// protects against a hostile or broken file. An element with children at the cap has none left in the answer and `truncated`
        constexpr size_t kMaxElementDepth = 100;
        /// The containers above the elements (the answer, `canvases`, a canvas, `elements`): a margin, not a count
        constexpr size_t kContainersAboveElements = 8;
        static_assert(2 * kMaxElementDepth + kContainersAboveElements < JsonWriter::kMaxDepth, "the cap must leave the writer's depth to the answer around the tree");

        /// Fills @p out from @p element and its subtree; @p depth is 1 for an element of the canvas
        void BuildElement(UIElement& element, const UICanvasResolved* resolved, size_t depth, UITreeElement& out) {
            out.type = TextView(element.GetTypeMeta().typeName);
            out.name = element.name;
            out.interactive = element.IsInteractive();
            out.blocksInput = element.blocksInput;
            out.hovered = element.hovered;
            out.pressed = element.pressed;
            if (resolved != nullptr) {
                const Rect& rect = element.rect;
                out.visible = !rect.IsEmpty();
                out.rect = rect;
                out.outputRect = resolved->transform.ToOutput(rect.x, rect.y, rect.w, rect.h);
            }
            if (!element.active.rect.region.Empty()) {
                // The name, published or not: a client reads the cause of a MissingRegion in `issues` here
                out.region = element.active.rect.region;
            }
            out.box = BuildBox(element.box);
            FillComponents(element, out);
            if (auto* group = element.Cast<UISegmented>()) {
                // The segments take `selected` and `groupDisabled` at the draw; apply them now so the tree shows the variable as it is, not as of the last draw
                group->SyncChildren();
                out.bind = group->bind;
                out.selectedValue = TextView(group->GetSelectedValue());
                out.disabled = group->disabled;
            }
            if (auto* dropdown = element.Cast<UIDropdown>()) {
                // Like the group: the options take `selected` now, and the children below are described after this, so they show the variable as it is
                dropdown->SyncChildren();
                out.bind = dropdown->bind;
                out.selectedValue = TextView(dropdown->GetSelectedValue());
                out.open = dropdown->IsPopupOpen();
                out.action = dropdown->action;
                out.disabled = dropdown->disabled;
            }
            if (const auto* bar = element.Cast<UIProgressBar>()) {
                out.bind = bar->bind;
                out.minValue = bar->minValue;
                out.maxValue = bar->maxValue;
                out.fraction = bar->GetFraction();
            }
            // The toggle and the slider read the variable now, not as of the last draw. After `FillComponents`, so the `action` they carry replaces the empty one of the clicker beside
            // them. A toggle's `text` is its caption, found by `FillComponents` below the row's clicker
            if (const auto* toggle = element.GetComponent<UIToggleComponent>()) {
                out.bind = toggle->bind;
                out.onValue = toggle->onValue;
                out.offValue = toggle->offValue;
                out.checked = toggle->IsOn();
                out.action = toggle->action;
                out.disabled = element.IsDisabled();
            }
            if (const auto* slider = element.GetComponent<UISliderComponent>()) {
                out.bind = slider->bind;
                out.minValue = slider->minValue;
                out.maxValue = slider->maxValue;
                out.step = slider->step;
                out.value.reset();  // a slider's number is `sliderValue`: `value` is the string of the clicker beside it
                out.sliderValue = slider->GetValue();
                out.fraction = slider->GetFraction();
                out.action = slider->action;
                out.disabled = element.IsDisabled();
            }
            if (depth >= kMaxElementDepth) {
                if (!element.children.empty()) {
                    out.truncated = true;
                }
                return;
            }
            out.children.reserve(element.children.size());
            for (const auto& child : element.children) {
                out.children.emplace_back();
                BuildElement(*child, resolved, depth + 1, out.children.back());
            }
        }

        UILayoutIssueAnswer BuildIssue(const UILayoutIssue& issue) {
            UILayoutIssueAnswer answer;
            answer.element = issue.element;
            answer.kind = issue.kind;
            answer.other = issue.other;
            answer.rect = issue.rect;
            return answer;
        }

        /// The regions the game published, also before the first draw (they do not depend on one), sorted by name
        ArenaVector<UIRegionAnswer> BuildRegions(const UIRegionSet& regions) {
            ArenaVector<UIRegionAnswer> answer;
            answer.reserve(regions.Size());
            for (const UIRegionSet::Entry& entry : regions) {
                UIRegionAnswer region;
                region.name = entry.name;
                region.rect = entry.rect;
                answer.push_back(std::move(region));
            }
            eastl::sort(answer.begin(), answer.end(), [](const UIRegionAnswer& a, const UIRegionAnswer& b) { return a.name.ToStringView() < b.name.ToStringView(); });
            return answer;
        }

        UICanvasAnswer BuildCanvas(const CanvasEntry& entry, const Scene& scene) {
            const UICanvasComponent& canvas = *entry.canvas;
            const UICanvasLayout& layout = canvas.GetLayout();
            UICanvasAnswer answer;
            answer.node = entry.node;
            answer.space = canvas._space;
            answer.visible = canvas._visible && canvas.IsEnabled();
            answer.modal = canvas._modal;
            answer.inputEnabled = canvas._inputEnabled;
            answer.takesInput = canvas.TakesInput();
            answer.inputOrder = canvas.GetInputOrder();
            answer.laidOut = layout.HasLayout();
            answer.regions = BuildRegions(layout.regions);
            if (canvas._space == CanvasSpace::SceneView) {
                // The view the scene drew with last: the canvas is laid out against it
                const ActiveSceneView view = scene.GetActiveView();
                UICanvasViewAnswer viewAnswer;
                viewAnswer.height = view.height;
                viewAnswer.name = view.name;
                viewAnswer.width = view.width;
                answer.view = std::move(viewAnswer);
            }

            const UICanvasResolved* resolved = layout.HasLayout() ? &layout.GetResolved() : nullptr;
            if (resolved != nullptr) {
                answer.scale = resolved->scale;
                answer.bounds = resolved->bounds;
                answer.safeBounds = resolved->safeBounds;
                answer.contentBounds = resolved->contentBounds;
            }
            answer.elements.reserve(layout.root->children.size());
            for (const auto& child : layout.root->children) {
                answer.elements.emplace_back();
                BuildElement(*child, resolved, 1, answer.elements.back());
            }

            if (resolved != nullptr) {
                UILayoutValidatorOptions options;
                options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
                options.pixelDensity = layout.GetContext().pixelDensity;
                const auto issues = layout.Validate(options);
                answer.issues.reserve(issues.size());
                for (const UILayoutIssue& issue : issues) {
                    answer.issues.push_back(BuildIssue(issue));
                }
            }
            return answer;
        }

        void HandleUITree(ControlCall& call) {
            Scene* scene = CoreManager::GetSceneManager().GetActiveScene();
            if (scene == nullptr) {
                call.Fail(UIControlErrors::kNoScene, "there is no active scene");
                return;
            }
            Vector<CanvasEntry> canvases;
            CollectCanvases(scene->GetRootNode(), canvases);

            UITreeAnswer answer;
            answer.scene = scene->GetName();
            answer.canvases.reserve(canvases.size());
            for (const CanvasEntry& entry : canvases) {
                answer.canvases.push_back(BuildCanvas(entry, *scene));
            }
            call.ReplyObject(answer);
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
        void CollectNamed(UIElement& root, PoolString searched, Vector<UIElement*>& out) {
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
            bool anyOpen = false;
            bool owned = false;
            layout.ForEachOpenPopup([&](UIElement& popup) {
                anyOpen = true;
                owned = owned || IsSameOrBelow(popup, element);
            });
            return anyOpen && !owned;
        }

        constexpr float kHalf = 0.5f;

        /// The press never lands on the very edge of a rect: the right and bottom edges are outside it for the hit test, and the round trip through output pixels may move a point by a
        /// rounding error. 0 and 1 of `at` are this far inside
        constexpr float kEdgeInset = 0.01f;
        constexpr float kDefaultAt = 0.5f;

        /// The point of a press: @p at (0..1) along the width, the middle of the height
        Pair<float, float> PointAt(const Rect& rect, float at) {
            const float travel = std::max(rect.w - 2.0f * kEdgeInset, 0.0f);
            return {rect.x + (rect.w - travel) * kHalf + travel * at, rect.y + rect.h * kHalf};
        }

        /// Looks for the element to click. A candidate that cannot be clicked is remembered only until a better miss or a hit turns up
        bool FindClickable(const Vector<CanvasEntry>& canvases, PoolString name, Optional<PoolString> onlyNode, float at, ClickTarget& target, bool& anyNamed) {
            anyNamed = false;
            for (const CanvasEntry& entry : canvases) {
                if (onlyNode && entry.node != *onlyNode) {
                    continue;
                }
                Vector<UIElement*> named;
                CollectNamed(*entry.canvas->GetLayout().root, name, named);
                for (UIElement* element : named) {
                    anyNamed = true;
                    ClickMiss miss = ClickMiss::None;
                    PoolString coveredBy;
                    // A disabled canvas is never drawn: no step would lay it out
                    if (!entry.canvas->IsEnabled()) {
                        miss = ClickMiss::CanvasCannotTakeInput;
                    } else if (!entry.canvas->GetLayout().HasLayout()) {
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

        String DescribeMiss(const ClickTarget& target, PoolString name) {
            const auto text = name.ToStringView();
            switch (target.miss) {
                case ClickMiss::NotInteractive:
                    return Format("'{}' is not interactive (not a button)", text);
                case ClickMiss::Hidden:
                    return Format("'{}' is hidden", text);
                case ClickMiss::Covered:
                    return Format("'{}' is covered by '{}' in its canvas", text, target.coveredBy.ToStringView());
                case ClickMiss::CanvasCannotTakeInput:
                    return Format("the canvas '{}' of '{}' is hidden or disabled, has input off or is in a scene that is not active", target.canvasNode.ToStringView(), text);
                case ClickMiss::NotLaidOut:
                    return Format("the canvas '{}' of '{}' has not been drawn yet: step a frame first", target.canvasNode.ToStringView(), text);
                case ClickMiss::None:
                    break;
            }
            return Format("'{}' cannot be clicked", text);
        }

        /// The words of `takenBy`: lower case, so not the names of a reflected enum
        struct TakenBy {
            static constexpr auto none = "none"_intern;
            static constexpr auto element = "element"_intern;
            static constexpr auto target = "target"_intern;
        };

        void HandleUIClick(ControlCall& call, ControlService& service) {
            const JsonView params = call.Params();
            const auto nameText = GetParamString(params, "name");
            if (!nameText || nameText->empty()) {
                call.Fail(ControlErrors::kInvalidParams, "'name' has to be the name of an element");
                return;
            }
            Optional<PoolString> onlyNode;
            if (FindParam(params, "canvas")) {
                const auto nodeText = GetParamString(params, "canvas");
                if (!nodeText) {
                    call.Fail(ControlErrors::kInvalidParams, "'canvas' has to be the name of a scene node");
                    return;
                }
                // A name that was never interned belongs to no node; client text does not go into the pool
                onlyNode = PoolString::Find(StringView(nodeText->c_str(), nodeText->size())).value_or(PoolString{});
            }
            float at = kDefaultAt;
            if (FindParam(params, "at")) {
                const auto atNumber = GetParamNumber(params, "at");
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
            Vector<CanvasEntry> canvases;
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
                const uint64_t canvasNodeId = target.canvas->GetNode().GetRuntimeId();
                // A composed button answers as a button; a disabled one emits nothing, so the answer carries nothing
                const auto* clicker = target.element->GetComponent<UIInputClicker>();
                const bool clickerEmits = clicker != nullptr && !target.element->IsDisabled();
                const PoolString clickerAction = clickerEmits ? clicker->action : PoolString{};
                const PoolString clickerValue = clickerEmits ? clicker->value : PoolString{};
                // A toggle reports the value it asks for, read before the click (a handler may set the variable at once); one that emits nothing reports nothing
                const auto* toggleComponent = target.element->GetComponent<UIToggleComponent>();
                PoolString toggleAction;
                PoolString toggleValue;
                if (toggleComponent != nullptr && !target.element->IsDisabled() && !toggleComponent->action.Empty()) {
                    toggleAction = toggleComponent->action;
                    toggleValue = toggleComponent->IsOn() ? toggleComponent->offValue : toggleComponent->onValue;
                }
                // The slider: what the press emitted is read after the click. A disabled one is not asked, its last value is of an earlier press
                const auto* sliderComponent = target.element->GetComponent<UISliderComponent>();
                const bool sliderCanEmit = sliderComponent != nullptr && !target.element->IsDisabled();
                // An option of a dropdown emits the dropdown's action with its own value (the option's `action` is empty), nothing when the choice is refused; the header emits nothing
                const UIDropdown* owner = UIChoice::IsOption(*target.element) ? FindDropdownOf(*target.canvas->GetLayout().root, target.element.Get()) : nullptr;
                const bool optionEmits = owner != nullptr && owner->EmitsOnChoice(*target.element);
                const PoolString optionAction = optionEmits ? owner->action : PoolString{};
                const PoolString optionValue = optionEmits ? UIChoice::GetValue(*target.element) : PoolString{};
                const bool onlyClosesList = PressOnlyClosesList(target.canvas->GetLayout(), *target.element);

                const IUIInputHandler* const canvasHandler = target.canvas.Get();
                const PointClick click = ClickOutputPoint(CoreManager::GetUIInput(), outputX, outputY, SDL_BUTTON_LEFT, kControlPointerId);
                const bool consumed = click.consumed;
                // A handler above the element (a scene target, a modal canvas) that took the press left the element nothing to emit
                const bool takenAbove = consumed && click.taker != canvasHandler;
                service.ReportTouch(canvasNodeId, target.canvas->GetTypeMeta().typeName);

                UIClickAnswer answer;
                answer.clicked = TextView(StringView(nameText->c_str(), nameText->size()));
                answer.canvas = canvasNode;
                answer.x = outputX;
                answer.y = outputY;
                answer.consumed = consumed;
                answer.takenBy = !consumed ? PoolString(TakenBy::none) : takenAbove ? PoolString(TakenBy::target) : PoolString(TakenBy::element);
                if (owner != nullptr) {
                    answer.action = optionAction;
                    answer.value = optionValue;
                } else if (toggleComponent != nullptr) {
                    answer.action = toggleAction;
                    answer.value = toggleValue;
                } else if (sliderComponent != nullptr) {
                    const PoolString emitted = sliderCanEmit ? sliderComponent->GetLastEmittedValue() : PoolString{};
                    answer.action = emitted.Empty() ? PoolString{} : sliderComponent->action;
                    answer.value = emitted;
                } else if (clicker != nullptr) {
                    // After the toggle and the slider: their clicker has no action of its own, and must not shadow them
                    answer.action = clickerAction;
                    answer.value = clickerValue;
                }
                if ((onlyClosesList || takenAbove) && answer.action) {
                    // The press only closed the open list, or a handler above took it: the element emitted nothing
                    answer.action = PoolString{};
                    answer.value = PoolString{};
                }
                call.ReplyObject(answer);
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
        service.RegisterHandler("ui_click", [&service](ControlCall& call) { HandleUIClick(call, service); });

        service.DescribeMethod("ui_tree", {"The canvases and elements of the active scene with their rects and the validator's findings", {}, /*quiet=*/true});
        service.DescribeMethod("ui_click", {"Clicks a visible button, toggle or slider by its name as a person would",
                                            {{"name", "string", true, "The name of the element"},
                                             {"canvas", "string", false, "The scene node of the canvas, when the name is not unique"},
                                             {"at", "number", false, "The horizontal position inside the element, 0 to 1; sets a slider"}}});
    }

}  // namespace BECore
