#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIControl.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UISlider.h>
#include <BECore/UI/UISliderComponent.h>
#include <BECore/UI/UISwitchComponent.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UIToggle.h>
#include <BECore/UI/UIToggleComponent.h>
#include <Common/MigrationTwin.h>
#include <Common/UILayoutMatrix.h>
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <tuple>

namespace BECore::Tests {

    using namespace MigrationTwin;

    /*
     * The twin test of the migration step 3 to 4 (`tools/migrate/steps/s0003_ui_toggle_slider_to_components.py`): a `UIToggle` and a `UISlider` rewritten as composed elements must
     * draw, lay out, answer and emit as the classes did. Every `<case>.before.xml` / `<case>.after.xml` pair of `tools/migrate/tests/fixtures/ui_value/` is loaded into canvases on a
     * `RecordingRenderer`, once as it was and once as it is now, and for every named toggle and slider of the screen the test compares:
     *   - the laid-out rect of every named element at two output sizes (`<N>Caption` and `<N>Switch` exist only in the new form), and the track and the caption area of a toggle against
     *     the geometry of the class (`UIToggle::GetParts` of the row) taken from the new form's switch and caption;
     *   - the draw of the whole scene with the bound variable on, off, unset and (for a slider) at the ends, in the middle, off the step grid and beyond the range, with the control
     *     `disabled` and as authored, and after a theme switch;
     *   - the draw with the pointer over the control and with it pressed: the switch, the thumb and the caption are equal; the plate of a toggle is not (the row has a clicker and so
     *     takes the state look of every button, the class never changed its plate): the test pins that the class plate stays and the composed one shifts. A slider keeps its plate still
     *     in both forms and is compared whole;
     *   - what `ui_tree` says about every element of the old form in those states (`type` and `components` differ by design);
     *   - the `ActionEvent` of a click on a toggle's caption, on its switch and outside it; of a press at the ends and inside a slider and of a drag that leaves its rect, a press
     *     outside it; each enabled and disabled;
     *   - the findings of the layout validator on the size matrix.
     * When a case differs the mapping of the step is wrong and the step is fixed, not this test.
     */

    namespace {

        constexpr const char* kNoValue = "(none)";
        constexpr const char* kStateAuthored = "authored";
        constexpr const char* kStateEnabled = "enabled";
        constexpr const char* kStateDisabled = "disabled";
        constexpr const char* kStateHover = "hover";
        constexpr const char* kStatePress = "press";
        constexpr const char* kStateThemeB = "theme B";
        constexpr const char* kCaptionSuffix = "Caption";
        constexpr const char* kSwitchSuffix = "Switch";
        constexpr const char* kThemeA = "TwinA";
        constexpr const char* kThemeB = "TwinB";
        constexpr float kPlateTolerance = 0.5f;  // output pixels: a call is the plate when its rect is the row's
        constexpr float kMidpoint = 0.5f;
        constexpr float kOffGridFraction = 0.37f;
        constexpr float kEdgeInset = 0.5f;       // canvas units: a press at the end that still hits the rect
        constexpr float kOutsideOffset = 30.0f;  // canvas units left of a control: on nothing
        constexpr std::array kSliderPressPoints{0.0f, 0.2f, 0.5f, 0.77f, 1.0f};
        constexpr std::array kDragPath{0.2f, 0.7f, 1.5f, -0.5f};  // the fraction of the width, beyond 0..1 the pointer is outside the rect

        /// A finding of the layout validator: the canvas, the case of the matrix and the kind, then the element and the other one it names
        struct Issue {
            std::string head;
            std::string element;
            std::string other;
            bool operator<(const Issue& rhs) const {
                return std::tie(head, element, other) < std::tie(rhs.head, rhs.element, rhs.other);
            }
            [[nodiscard]] std::string Describe() const {
                return head + " | " + element + " | " + other;
            }
        };

        /// What one form of a screen did; the keys name the control, the state and, for a tree, the element
        struct Snapshot {
            std::map<std::string, std::vector<Rect>> rects;          // "scene|node|element" -> one per output size
            std::map<std::string, std::vector<Rect>> tracks;         // "scene|node|toggle" -> the track of its switch, one per output size
            std::map<std::string, std::vector<Rect>> captions;       // ... and the area of its caption
            std::map<std::string, eastl::vector<DrawCall>> draws;    // "control|state" -> the whole scene's draw (without the plate in the states of the pointer)
            std::map<std::string, eastl::vector<DrawCall>> plates;   // "control|state" -> the calls of the plate of a toggle in the states of the pointer, and "control|rest"
            std::map<std::string, std::string> trees;                // "control|state|scene|node|element" -> what `ui_tree` says
            std::map<std::string, std::vector<std::string>> clicks;  // "control|what" -> the actions emitted
            std::set<Issue> issues;                                  // from the validator
        };

        /// A toggle or a slider of either form, with what the test needs to drive it
        struct Control {
            UIElement* element = nullptr;
            bool isToggle = false;
            PoolString bind;
            std::string onValue;
            std::string offValue;
            float minValue = 0.0f;
            float maxValue = 1.0f;
        };

        bool AsControl(UIElement& element, Control& out) {
            out = {};
            out.element = &element;
            if (const auto* toggle = element.Cast<UIToggle>()) {
                out.isToggle = true;
                out.bind = toggle->bind;
                out.onValue = toggle->onValue.CStr();
                out.offValue = toggle->offValue.CStr();
                return true;
            }
            if (const auto* component = element.GetComponent<UIToggleComponent>()) {
                out.isToggle = true;
                out.bind = component->bind;
                out.onValue = component->onValue.CStr();
                out.offValue = component->offValue.CStr();
                return true;
            }
            if (const auto* slider = element.Cast<UISlider>()) {
                out.bind = slider->bind;
                out.minValue = slider->minValue;
                out.maxValue = slider->maxValue;
                return true;
            }
            if (const auto* component = element.GetComponent<UISliderComponent>()) {
                out.bind = component->bind;
                out.minValue = component->minValue;
                out.maxValue = component->maxValue;
                return true;
            }
            return false;
        }

        void SetDisabled(UIElement& element, bool on) {
            if (auto* toggle = element.Cast<UIToggle>()) {
                toggle->disabled = on;
            } else if (auto* slider = element.Cast<UISlider>()) {
                slider->disabled = on;
            } else if (auto* clicker = element.GetComponent<UIInputClicker>()) {
                clicker->disabled = on;
            }
        }

        void SetVariable(const Control& control, const std::string& value) {
            if (control.bind.Empty()) {
                return;
            }
            if (value == kNoValue) {
                CoreManager::GetUIVars().Remove(control.bind);
            } else {
                CoreManager::GetUIVars().Set(control.bind, eastl::string_view(value.data(), value.size()));
            }
        }

        std::vector<std::string> ValuesOf(const Control& control) {
            if (control.isToggle) {
                return {control.onValue, control.offValue, kNoValue};
            }
            const float range = control.maxValue - control.minValue;
            return {std::to_string(control.minValue),
                    std::to_string(control.minValue + range * kMidpoint),
                    std::to_string(control.maxValue),
                    std::to_string(control.minValue + range * kOffGridFraction),
                    std::to_string(control.maxValue + range + 1.0f),
                    kNoValue};
        }

        std::string Number(const Json& object, eastl::string_view key) {
            const auto value = GetJsonNumber(object, key);
            return value ? std::to_string(*value) : std::string();
        }

        /// What `ui_tree` says about an element that a migration must keep: a missing key and an empty or false one are the same answer
        std::string TreeFields(const Json& element) {
            std::string fields;
            for (const char* key : {"text", "action", "value", "bind", "onValue", "offValue"}) {
                if (const Json* member = FindJsonMember(element, key); member != nullptr && member->is_string()) {
                    fields += std::string(key) + "=" + JsonText(element, key) + "; ";
                } else {
                    fields += std::string(key) + "=; ";
                }
            }
            for (const char* key : {"minValue", "maxValue", "step", "fraction"}) {
                fields += std::string(key) + "=" + Number(element, key) + "; ";
            }
            // A slider's `value` is a number in the tree
            if (const Json* member = FindJsonMember(element, "value"); member != nullptr && member->is_number()) {
                fields += "valueNumber=" + Number(element, "value") + "; ";
            }
            for (const char* key : {"disabled", "checked", "interactive", "visible", "blocksInput"}) {
                fields += std::string(key) + "=" + JsonFlag(element, key) + "; ";
            }
            return fields;
        }

        bool SameRect(const Rect& a, const Rect& b, float tolerance) {
            return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.w - b.w) <= tolerance && std::abs(a.h - b.h) <= tolerance;
        }

        std::string RectText(const Rect& rect) {
            return "(" + std::to_string(rect.x) + ", " + std::to_string(rect.y) + ", " + std::to_string(rect.w) + " x " + std::to_string(rect.h) + ")";
        }

        UIElement* ChildNamed(UIElement& parent, const std::string& name) {
            for (const auto& child : parent.children) {
                if (name == child->name.CStr()) {
                    return child.Get();
                }
            }
            return nullptr;
        }

        void FillThemes(UIThemes& themes) {
            const PoolString a = PoolString::Intern(kThemeA);
            const PoolString b = PoolString::Intern(kThemeB);
            themes.Set(a, "text", Color{240, 240, 240, 255});
            themes.Set(a, "muted", Color{90, 90, 90, 255});
            themes.Set(a, "accent", Color{20, 200, 90, 255});
            themes.SetDefaultTheme(a);
            themes.Set(b, "text", Color{20, 20, 20, 255});
            themes.Set(b, "muted", Color{200, 200, 200, 255});
            themes.Set(b, "accent", Color{200, 40, 40, 255});
        }

    }  // namespace

    class ContentMigrationValueTest : public Fixture {
    protected:
        void SetUp() override {
            Fixture::SetUp();
            FillThemes(CoreManager::GetUIThemes());
        }

        void TearDown() override {
            CoreManager::GetUIThemes().Clear();
            Fixture::TearDown();
        }

        /// Loads a scene manager from @p xml and records what it does into @p snapshot
        bool Capture(const std::string& xml, Snapshot& snapshot) {
            if (!Load(xml)) {
                return false;
            }
            for (const std::string& scene : SceneNames(xml)) {
                CaptureScene(scene, snapshot);
            }
            Unload();
            return true;
        }

        void CaptureScene(const std::string& sceneName, Snapshot& snapshot) {
            _manager->SetActiveScene(PoolString::Intern(sceneName.c_str()));
            Scene* scene = _manager->GetSceneByName(PoolString::Intern(sceneName.c_str()));
            ASSERT_NE(scene, nullptr) << sceneName;
            const std::vector<CanvasRef> canvases = CanvasesOf(*scene);
            ShowEverything(*scene, canvases);

            for (const auto& [width, height] : {std::pair{kLargeWidth, kLargeHeight}, std::pair{kSmallWidth, kSmallHeight}}) {
                Draw(width, height);
                for (const CanvasRef& ref : canvases) {
                    std::set<std::string> seen;
                    WalkElements(ref.canvas->GetRoot(), [&](UIElement& element) {
                        const std::string key = Where(sceneName, ref.node, element.name.CStr());
                        if (element.name.Empty() || !seen.insert(key).second) {
                            return;
                        }
                        snapshot.rects[key].push_back(element.rect);
                        Control control;
                        if (AsControl(element, control) && control.isToggle) {
                            RecordToggleParts(element, key, snapshot);
                        }
                    });
                }
            }

            Draw(kLargeWidth, kLargeHeight);
            for (const CanvasRef& ref : canvases) {
                std::vector<Control> controls;
                std::set<std::string> seen;
                WalkElements(ref.canvas->GetRoot(), [&](UIElement& element) {
                    Control control;
                    if (!element.name.Empty() && AsControl(element, control) && seen.insert(element.name.CStr()).second) {
                        controls.push_back(control);
                    }
                });
                for (const Control& control : controls) {
                    CaptureControl(sceneName, ref, control, snapshot);
                }
                CaptureValidator(*scene, ref, sceneName + "|" + ref.node, snapshot);
            }
        }

        /// The track and the caption area of a toggle: those of the class's geometry from the row (a `UIToggle`), or from the switch and the caption child (the composed row)
        static void RecordToggleParts(UIElement& row, const std::string& key, Snapshot& snapshot) {
            if (row.Cast<UIToggle>() != nullptr) {
                const UIToggle::Parts parts = UIToggle::GetParts(row.rect);
                snapshot.tracks[key].push_back(parts.track);
                snapshot.captions[key].push_back(parts.text);
                return;
            }
            const std::string name = row.name.CStr();
            UIElement* switchElement = ChildNamed(row, name + kSwitchSuffix);
            UIElement* caption = ChildNamed(row, name + kCaptionSuffix);
            EXPECT_NE(switchElement, nullptr) << key << ": no switch child";
            EXPECT_NE(caption, nullptr) << key << ": no caption child";
            if (switchElement != nullptr && caption != nullptr) {
                snapshot.tracks[key].push_back(UISwitchComponent::GetParts(switchElement->rect).track);
                snapshot.captions[key].push_back(caption->rect);
            }
        }

        /// The draw and the tree of the screen in one state of one control. In the states of the pointer a toggle's plate is taken out of the draw and kept apart
        void Record(const std::string& sceneName, const std::string& control, const std::string& state, const Control& subject, const CanvasRef& ref, bool splitPlate, Snapshot& snapshot) {
            Draw(kLargeWidth, kLargeHeight);
            const std::string key = control + "|" + state;
            if (splitPlate) {
                const Rect output = ref.canvas->GetLayout().GetResolved().transform.ToOutput(subject.element->rect.x, subject.element->rect.y, subject.element->rect.w, subject.element->rect.h);
                for (const DrawCall& call : _renderer->calls) {
                    if (SameRect(call.rect, output, kPlateTolerance)) {
                        snapshot.plates[key].push_back(call);
                    } else {
                        snapshot.draws[key].push_back(call);
                    }
                }
            } else {
                snapshot.draws[key] = _renderer->calls;
            }
            std::map<std::string, std::string> fields;
            for (const Json& canvas : CallTree(sceneName)) {
                const std::string canvasKey = sceneName + "|" + JsonText(canvas, "node");
                std::set<std::string> seen;
                CollectTree(canvas, canvasKey, seen, fields, TreeFields);
            }
            for (const auto& [name, text] : fields) {
                snapshot.trees[key + "|" + name] = text;
            }
        }

        /// A canvas point (the row's, at @p fractionX of its width and the middle of its height) in output pixels
        static Vec2 OutputAt(const CanvasRef& ref, const UIElement& element, float fractionX) {
            const Rect output = ref.canvas->GetLayout().GetResolved().transform.ToOutput(element.rect.x + element.rect.w * fractionX, element.rect.y + element.rect.h * kMidpoint, 0.0f, 0.0f);
            return {output.x, output.y};
        }

        static Vec2 OutputOfCanvasPoint(const CanvasRef& ref, float x, float y) {
            const Rect output = ref.canvas->GetLayout().GetResolved().transform.ToOutput(x, y, 0.0f, 0.0f);
            return {output.x, output.y};
        }

        /// A press at the first point, the pointer moved through the others and released at the last; the actions that went out
        static std::vector<std::string> Gesture(const std::vector<Vec2>& path) {
            ActionRecorder recorder;
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(path.front().x, path.front().y));
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(path.front().x, path.front().y, true));
            for (size_t i = 1; i < path.size(); ++i) {
                CoreManager::GetUIInput().DispatchMouseMotion(Motion(path[i].x, path[i].y));
            }
            CoreManager::GetUIInput().DispatchMouseButtonUp(Button(path.back().x, path.back().y, false));
            return recorder.emitted;
        }

        void CaptureControl(const std::string& sceneName, const CanvasRef& ref, const Control& control, Snapshot& snapshot) {
            UIElement& element = *control.element;
            const std::string name = std::string(sceneName) + "|" + ref.node + "|" + element.name.CStr();
            const bool authoredDisabled = element.IsDisabled();
            const std::vector<std::string> values = ValuesOf(control);

            // The variable takes each value in turn, the control as authored
            for (const std::string& value : values) {
                SetVariable(control, value);
                Record(sceneName, name, std::string(kStateAuthored) + " value=" + value, control, ref, false, snapshot);
            }
            SetVariable(control, values.front());
            for (const bool disabled : {false, true}) {
                SetDisabled(element, disabled);
                Record(sceneName, name, disabled ? kStateDisabled : kStateEnabled, control, ref, false, snapshot);
            }
            // A toggle that is on and disabled, and one that is on after a theme switch (the tokens of the colours resolve again)
            SetDisabled(element, false);
            CoreManager::GetUIThemes().SetTheme(PoolString::Intern(kThemeB));
            Record(sceneName, name, kStateThemeB, control, ref, false, snapshot);
            CoreManager::GetUIThemes().SetTheme(PoolString::Intern(kThemeA));
            Draw(kLargeWidth, kLargeHeight);

            // The plate at rest: the state look of the row shifts it from here
            Record(sceneName, name, "rest", control, ref, control.isToggle, snapshot);
            const Vec2 middle = OutputAt(ref, element, kMidpoint);
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(middle.x, middle.y));
            Record(sceneName, name, kStateHover, control, ref, control.isToggle, snapshot);
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(middle.x, middle.y, true));
            Record(sceneName, name, kStatePress, control, ref, control.isToggle, snapshot);
            CoreManager::GetUIInput().DispatchMouseButtonUp(Button(middle.x, middle.y, false));
            Park();
            Draw(kLargeWidth, kLargeHeight);

            for (const bool disabled : {false, true}) {
                SetDisabled(element, disabled);
                Draw(kLargeWidth, kLargeHeight);
                const std::string suffix = disabled ? "|disabled" : "";
                if (control.isToggle) {
                    CaptureToggleClicks(name + suffix, ref, control, snapshot);
                } else {
                    CaptureSliderGestures(name + suffix, ref, control, snapshot);
                }
            }
            SetDisabled(element, authoredDisabled);
            CoreManager::GetUIVars().Clear();
            Park();
            Draw(kLargeWidth, kLargeHeight);
        }

        /// A click on the caption, on the switch and outside, with the variable on, off and unset
        void CaptureToggleClicks(const std::string& label, const CanvasRef& ref, const Control& control, Snapshot& snapshot) {
            const UIElement& row = *control.element;
            const UIToggle::Parts parts = UIToggle::GetParts(row.rect);
            const std::pair<const char*, Vec2> points[] = {
                {"caption", OutputOfCanvasPoint(ref, parts.text.x + parts.text.w * kMidpoint, parts.text.y + parts.text.h * kMidpoint)},
                {"switch", OutputOfCanvasPoint(ref, parts.track.x + parts.track.w * kMidpoint, parts.track.y + parts.track.h * kMidpoint)},
                {"outside", {kOutside, kOutside}},
            };
            for (const std::string& value : ValuesOf(control)) {
                SetVariable(control, value);
                Draw(kLargeWidth, kLargeHeight);
                for (const auto& [where, at] : points) {
                    snapshot.clicks[label + "|" + where + "|value=" + value] = Click(at);
                    Park();
                    Draw(kLargeWidth, kLargeHeight);
                }
            }
        }

        /// A press at the ends and inside, a press outside, and a drag that leaves the rect on both sides
        void CaptureSliderGestures(const std::string& label, const CanvasRef& ref, const Control& control, Snapshot& snapshot) {
            const UIElement& slider = *control.element;
            for (const float fraction : kSliderPressPoints) {
                const float x = fraction == 0.0f ? slider.rect.x + kEdgeInset : (fraction == 1.0f ? slider.rect.x + slider.rect.w - kEdgeInset : slider.rect.x + slider.rect.w * fraction);
                const Vec2 at = OutputOfCanvasPoint(ref, x, slider.rect.y + slider.rect.h * kMidpoint);
                snapshot.clicks[label + "|press at " + std::to_string(fraction)] = Gesture({at});
                Park();
                Draw(kLargeWidth, kLargeHeight);
            }
            std::vector<Vec2> drag;
            for (const float fraction : kDragPath) {
                drag.push_back(OutputAt(ref, slider, fraction));
            }
            snapshot.clicks[label + "|drag"] = Gesture(drag);
            Park();
            Draw(kLargeWidth, kLargeHeight);

            const Vec2 outside = OutputOfCanvasPoint(ref, slider.rect.x - kOutsideOffset, slider.rect.y + slider.rect.h * kMidpoint);
            snapshot.clicks[label + "|press outside"] = Gesture({outside});
            snapshot.clicks[label + "|press outside, drag in"] = Gesture({outside, OutputAt(ref, slider, kMidpoint)});
            Park();
            Draw(kLargeWidth, kLargeHeight);
        }

        /// What the layout validator finds on the size matrix, as "case | kind | element | other" lines
        void CaptureValidator(Scene& scene, const CanvasRef& ref, const std::string& canvasKey, Snapshot& snapshot) {
            UILayoutValidatorOptions options;
            options.minTouchSize = kTouchSize;
            UILayoutContext context;
            context.sceneViewWidth = scene.GetViewWidth();
            context.sceneViewHeight = scene.GetViewHeight();
            for (const UIMatrixFailure& failure : ValidateOnMatrix(ref.canvas->GetLayout(), options, context)) {
                for (const UILayoutIssue& issue : failure.issues) {
                    snapshot.issues.insert(
                        {canvasKey + " | " + Text(failure.label) + " | " + std::string(EnumUtils<UILayoutIssueKind>::ToString(issue.kind).data()), issue.element.CStr(), issue.other.CStr()});
                }
            }
        }
    };

    TEST(ContentMigrationValueFixtures, TheFixturePairsAreThere) {
        ASSERT_TRUE(std::filesystem::is_directory(FixtureFolder("ui_value"))) << FixtureFolder("ui_value").string();
        const std::vector<std::string> names = CaseNames(FixtureFolder("ui_value"));
        for (const char* required :
             {"toggle_plain", "toggle_options", "toggle_literal_color", "toggle_token_color", "slider_full", "slider_disabled", "beside_a_set", "mahjong_declare", "already_composed"}) {
            EXPECT_TRUE(std::find(names.begin(), names.end(), required) != names.end()) << "no pair for " << required;
        }
    }

    TEST_P(ContentMigrationValueTest, TheMigratedTogglesAndSlidersBehaveLikeTheClasses) {
        const std::string name = GetParam();
        const std::string before = SceneManagerXml(ReadFile(FixtureFolder("ui_value") / (name + ".before.xml")));
        const std::string after = SceneManagerXml(ReadFile(FixtureFolder("ui_value") / (name + ".after.xml")));
        ASSERT_FALSE(before.empty());
        ASSERT_FALSE(after.empty());

        Snapshot old;
        Snapshot now;
        ASSERT_TRUE(Capture(before, old));
        ASSERT_TRUE(Capture(after, now));
        ASSERT_FALSE(old.rects.empty()) << name << ": no element with a name";
        ASSERT_FALSE(old.draws.empty()) << name << ": no toggle or slider in the screen";

        for (const auto& [key, rects] : old.rects) {
            const auto found = now.rects.find(key);
            ASSERT_NE(found, now.rects.end()) << name << ": the element " << key << " is gone";
            ASSERT_EQ(found->second.size(), rects.size());
            for (size_t i = 0; i < rects.size(); ++i) {
                EXPECT_TRUE(SameRect(rects[i], found->second[i], kTolerance))
                    << name << " " << key << " at output size " << i << ": the rect was " << RectText(rects[i]) << " and is " << RectText(found->second[i]);
            }
        }
        for (const auto& [key, tracks] : old.tracks) {
            for (const auto* parts : {&now.tracks, &now.captions}) {
                const auto found = parts->find(key);
                ASSERT_NE(found, parts->end()) << name << ": no parts of the toggle " << key;
                ASSERT_EQ(found->second.size(), tracks.size()) << name << " " << key;
            }
            for (size_t i = 0; i < tracks.size(); ++i) {
                EXPECT_TRUE(SameRect(tracks[i], now.tracks[key][i], kTolerance))
                    << name << " " << key << " the track at output size " << i << " was " << RectText(tracks[i]) << " and is " << RectText(now.tracks[key][i]);
                EXPECT_TRUE(SameRect(old.captions[key][i], now.captions[key][i], kTolerance))
                    << name << " " << key << " the caption at output size " << i << " was " << RectText(old.captions[key][i]) << " and is " << RectText(now.captions[key][i]);
            }
        }

        // The states of the pointer compare the screen without the plate of a toggle; the plate has its own check below
        const auto isPointerState = [](const std::string& key) { return key.ends_with(std::string("|") + kStateHover) || key.ends_with(std::string("|") + kStatePress); };
        for (const auto& [key, calls] : old.draws) {
            const auto found = now.draws.find(key);
            ASSERT_NE(found, now.draws.end()) << name << ": no draw of " << key;
            const std::string difference = DrawDifference(calls, found->second, kTolerance);
            EXPECT_TRUE(difference.empty()) << name << " " << key << ": " << difference << (isPointerState(key) ? " (without the plate)" : "");
        }
        size_t shifted = 0;
        // The plate: the class keeps it at rest in both states of the pointer, the composed row shifts it (it is a button with a clicker); the slider has none of its own
        for (const auto& [key, rest] : old.plates) {
            if (!key.ends_with("|rest")) {
                continue;
            }
            const std::string base = key.substr(0, key.size() - std::string("rest").size());
            const auto nowRest = now.plates.find(key);
            ASSERT_NE(nowRest, now.plates.end()) << name << ": no plate of " << key;
            EXPECT_TRUE(DrawDifference(rest, nowRest->second, kTolerance).empty()) << name << " " << key << ": the plate at rest differs: " << DrawDifference(rest, nowRest->second, kTolerance);
            for (const char* state : {kStateHover, kStatePress}) {
                const std::string stateKey = base + state;
                EXPECT_TRUE(DrawDifference(rest, old.plates[stateKey], kTolerance).empty()) << name << " " << stateKey << ": the plate of the class must stay still";
                if (!rest.empty() && rest.front().color.a > 0) {
                    ++shifted;
                    EXPECT_FALSE(DrawDifference(rest, now.plates[stateKey], kTolerance).empty()) << name << " " << stateKey << ": the plate of the composed row must shift with the state";
                }
            }
        }

        if (name == "mahjong_declare") {
            EXPECT_GT(shifted, 0u) << "the plate of Declare is a visible one: the check of its shift must have run";
        }
        if (name == "toggle_token_color") {
            // The theme case would pass if the switch of a token never changed: the colours of the other theme are in the draw
            const std::string key = "Main|Toggles|Themed|";
            EXPECT_FALSE(DrawDifference(now.draws[key + kStateEnabled], now.draws[key + kStateThemeB], kTolerance).empty()) << "the theme switch changes nothing in the draw";
        }

        for (const auto& [key, fields] : old.trees) {
            const auto found = now.trees.find(key);
            ASSERT_NE(found, now.trees.end()) << name << ": ui_tree no longer lists " << key;
            EXPECT_EQ(fields, found->second) << name << " in the tree: " << key;
        }
        for (const auto& [key, emitted] : old.clicks) {
            EXPECT_EQ(emitted, now.clicks[key]) << name << " a click: " << key;
        }
        // The comparisons would pass on a screen where nothing was clicked: some enabled gesture emitted an action
        EXPECT_TRUE(std::any_of(old.clicks.begin(), old.clicks.end(), [](const auto& entry) { return !entry.second.empty() && entry.first.find("|disabled|") == std::string::npos; }))
            << name << ": no click emitted an action";
        for (const Issue& issue : old.issues) {
            EXPECT_TRUE(now.issues.contains(issue)) << name << ": the validator found on the size matrix only before: " << issue.Describe();
        }
        // The caption and the switch of a row are elements of their own now: they leave the canvas with the row they sit in, which the old form reported once, on the row
        for (const Issue& issue : now.issues) {
            std::string row = issue.element;
            for (const char* suffix : {kCaptionSuffix, kSwitchSuffix}) {
                if (row.size() > std::strlen(suffix) && row.ends_with(suffix)) {
                    row.resize(row.size() - std::strlen(suffix));
                    break;
                }
            }
            const bool partOfARow = row != issue.element && old.issues.contains({issue.head, row, issue.other});
            EXPECT_TRUE(old.issues.contains(issue) || partOfARow) << name << ": the validator finds on the size matrix only now: " << issue.Describe();
        }
    }

    INSTANTIATE_TEST_SUITE_P(Fixtures, ContentMigrationValueTest, ::testing::ValuesIn(CaseNames(FixtureFolder("ui_value"))));

}  // namespace BECore::Tests
