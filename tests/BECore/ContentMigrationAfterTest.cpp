#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIControl.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UISliderComponent.h>
#include <BECore/UI/UIToggleComponent.h>
#include <Common/MigrationTwin.h>
#include <Common/UILayoutMatrix.h>
#include <cstdio>
#include <gtest/gtest.h>
#include <map>
#include <set>

namespace BECore::Tests {

    using namespace MigrationTwin;

    /*
     * The final form of the content that the migration steps 1 to 4 (`tools/migrate/steps/s0001..s0004`) write: a button, a segment, a toggle, a slider, a text, a picture and the
     * header of a dropdown are an element with components. Every `<case>.after.xml` of `tools/migrate/tests/fixtures/ui_button/`, `ui_group/`, `ui_value/` and `ui_text/` is loaded into canvases on a
     * `RecordingRenderer` and the test checks the composed form on its own:
     *   - the layout validator finds nothing on the size matrix but what `kExpectedIssues` names (the touch size is not checked there), and every control of the case has a rect at
     *     two output sizes;
     *   - `ui_tree` answers every named control with the fields its fixture says (`text`, `icon`, `action`, `value`, `tooltip`, `bind`, `checked`, the range of a slider,
     *     `disabled`), a toggle with its variable unset and on;
     *   - a click emits the `ActionEvent` the fixture says (a toggle: the value it flips to, from the caption, the switch and the row; a slider: the value under the pointer), and
     *     nothing when the control is `disabled` by itself or by its group (one that is not a member of a `UISegmented`), nor when the click is on no control.
     * Nothing here builds the class that a fixture's `.before.xml` names (`UIButton`, `UIToggle`, `UISlider`), so the classes can go: the pairs are held by the pytest of each step
     * (`tools/migrate/tests/`) and the composed controls by their component tests (`UIToggleComponentTest`, `UISliderComponentTest`, `UIComposedButtonScreenTest`), which hold the
     * geometry and the draw. A test that compared a migrated screen with the class's draw would need the class.
     *
     * A fixture that still holds an element of a type a later step rewrites (the old header of a `UIDropdown`, a `UILabel`, a `UIImage`) is not loaded here (`kSkipped`): the
     * `ui_text/` fixtures (step 4, 4 to 5) are those shapes in their final form. The ones with no control to click (labels, pictures) are loaded for the validator and the draw
     * only; `picture_under_clicker` has a test of its own, which holds that a picture under a clicker keeps its colour in every state.
     */

    namespace {

        enum class Kind { Button, Toggle, Slider };

        /// A named control of a fixture and what the test expects of it; a button's click emits `action=value`, a toggle's `action=on` (variable unset) or `action=off` (variable on)
        struct Control {
            const char* name;
            Kind kind;
            const char* action;
            const char* value = "";    // the clicker's value (a button)
            const char* text = "";     // `text` of `ui_tree`: the caption, expanded
            const char* icon = "";     // the sprite
            const char* tooltip = "";  // expanded
            const char* bind = "";
            bool disabled = false;
            const char* on = "true";  // a toggle's onValue
            const char* off = "false";
            const char* range = "";   // a slider: "min..max step s"
            const char* middle = "";  // a slider: the value of a press in the middle of its rect
        };

        Control Button(const char* name, const char* action, const char* value = "", const char* text = "", const char* icon = "", const char* tooltip = "", bool disabled = false) {
            return {name, Kind::Button, action, value, text, icon, tooltip, "", disabled};
        }

        Control Toggle(const char* name, const char* bind, const char* action, const char* text, bool disabled = false, const char* on = "true", const char* off = "false") {
            return {name, Kind::Toggle, action, "", text, "", "", bind, disabled, on, off};
        }

        Control Slider(const char* name, const char* bind, const char* action, const char* range, const char* middle, bool disabled = false) {
            Control control{name, Kind::Slider, action, "", "", "", "", bind, disabled};
            control.range = range;
            control.middle = middle;
            return control;
        }

        struct Expectation {
            const char* folder;
            const char* name;
            std::vector<Control> controls;
        };

        /// The controls of every fixture that is loaded. An element without a name (some cases have one on purpose) cannot be told from another in `ui_tree` and is not listed
        const std::vector<Expectation>& Expectations() {
            static const std::vector<Expectation> all = {
                {"ui_button", "blocker", {Button("Shield", ""), Button("Corner", "")}},
                {"ui_button", "disabled_tooltip", {Button("Buy", "Buy", "sword", "Buy ", "", "{#shop.tip}: {price} & more", true)}},
                {"ui_button", "icon_attributes_without_icon", {Button("Empty", "Go", "", "Go"), Button("Stray", "Stop", "", "Stop")}},
                {"ui_button",
                 "icon_only",
                 {Button("Settings", "Settings", "", "", "icons.settings", "{#tip.settings}"), Button("Sized", "Play", "", "", "icons.play"), Button("Tinted", "Gear", "1", "", "icons.gear"),
                  Button("Tokened", "Gear", "2", "", "icons.gear"), Button("Off", "Stop", "", "", "icons.stop", "", true)}},
                {"ui_button",
                 "icon_text",
                 {Button("Play", "Play", "easy", "Play", "icons.play", "{#tip.play}: {level}"), Button("Off", "Stop", "", "Stop", "icons.stop", "", true),
                  Button("Styled", "Gear", "", "A long caption for the rest", "icons.gear"), Button("Narrow", "Play", "", "Narrow caption", "icons.play")}},
                {"ui_button",
                 "long_caption",
                 {Button("Ellipsis", "Go", "", "A caption far too long for its rect"), Button("Shrunk", "Go", "", "A caption far too long for its rect"),
                  Button("Wrapped", "Go", "", "A caption far too long for its rect")}},
                {"ui_button", "mahjong_hand_over_action", {Button("NextHand", "MahjongNextHand", "", "")}},
                {"ui_button", "mahjong_hints_button", {Button("HintButton", "MahjongHintAdvice", "", "hints.adviseButton")}},
                {"ui_button", "text_attributes", {Button("Left", "Go", "", "Left aligned"), Button("Styled", "Go", "", "He said \"go\""), Button("OnlyColor", "Go")}},
                {"ui_group", "segmented_icon", {Button("ModeGrid", "SetMode", "grid", "", "icons.grid", "{#tip.grid}"), Button("ModeList", "SetMode", "list", "List", "icons.list")}},
                {"ui_value", "already_composed", {Toggle("Sound", "soundOn", "SetSound", "Sound"), Slider("Volume", "volume", "SetVolume", "0..1 step 0", "0.5")}},
                {"ui_value", "mahjong_declare", {Button("Pass", "MahjongPass", "", "button.Pass"), Toggle("Declare", "declareNext", "MahjongDeclare", "button.Declare", false, "on", "off")}},
                {"ui_value", "slider_disabled", {Slider("Locked", "volume", "SetVolume", "0..1 step 0.1", "0.5", true), Toggle("LockedToggle", "a", "A", "Locked", true)}},
                {"ui_value", "slider_full", {Slider("Volume", "volume", "SetVolume", "0..10 step 0.5", "5"), Slider("Plain", "p", "P", "0..1 step 0", "0.5")}},
                {"ui_value",
                 "toggle_literal_color",
                 {Toggle("Dark", "a", "A", "Dark"), Toggle("Translucent", "b", "B", "Translucent"), Toggle("White", "c", "C", "White"), Toggle("Given", "d", "D", "Given"),
                  Toggle("Transparent", "e", "E", "Transparent")}},
                {"ui_value", "toggle_options", {Toggle("Declare", "declareNext", "Declare", "button.Declare", true, "on", "off")}},
                {"ui_value", "toggle_plain", {Toggle("Sound", "soundOn", "SetSound", "Sound")}},
                {"ui_value", "toggle_token_color", {Toggle("Themed", "a", "A", "Themed")}},
                // Texts, pictures and the header of a dropdown: no control to click here (the dropdown tests hold the options); loaded for the validator and the draw
                {"ui_text", "already_composed", {}},
                {"ui_text", "dropdown_mahjong", {}},
                {"ui_text", "dropdown_placeholder", {}},
                {"ui_text", "label_all_attributes", {}},
                {"ui_text", "label_no_attributes", {}},
                {"ui_text", "label_token_color", {}},
                {"ui_text", "nested", {}},
                {"ui_text", "picture", {}},
                {"ui_text", "picture_under_clicker", {}},
                {"ui_text", "scene_config", {}},
            };
            return all;
        }

        /// The cases that are not loaded, with the reason: each has an element of a type that a later step (step 4, 4 to 5) rewrites
        constexpr const char* kSkipped[] = {
            "ui_button/already_composed",            // a UISegmented with UIButton segments beside a composed button
            "ui_button/match3_menu_hud",             // a UILabel
            "ui_button/mahjong_setup",               // UIButton and the old header of a UIDropdown
            "ui_button/with_children",               // a UILabel child
            "ui_group/already_composed",             // a UIDropdown with its old header
            "ui_group/dropdown_disabled_option",     // a UIDropdown
            "ui_group/dropdown_options",             // a UIDropdown
            "ui_group/mahjong_setup",                // a UIDropdown
            "ui_group/plain_button_beside_a_group",  // a UIButton and a UIDropdown
            "ui_group/segmented_plain",              // a UILabel
            "ui_value/beside_a_set",                 // a UILabel
        };

        /// The findings of the layout validator on the size matrix that a loaded case is meant to have, as "folder/case | kind | element | other": the fixture puts a control where
        /// a narrow output scales it off the canvas. The touch size is not checked (`minTouchSize` is 0 below): the fixtures are 56 unit controls on `Screen` canvases that scale
        /// below the minimum in a small window, and the minimum of real screens is held by the tests of those screens
        const Set<std::string> kExpectedIssues = {
            "ui_group/segmented_icon | OutsideCanvas | Mode | ",
            "ui_group/segmented_icon | OutsideCanvas |  | ",
            "ui_text/nested | OutsideCanvas | Badge | ",
            "ui_text/scene_config | OutsideCanvas | Score | ",
            "ui_text/scene_config | OutsideCanvas | Seed | ",
            "ui_value/toggle_literal_color | OutsideCanvas | Transparent | ",
            "ui_value/toggle_literal_color | OutsideCanvas | TransparentCaption | ",
            "ui_value/toggle_literal_color | OutsideCanvas | TransparentSwitch | ",
        };

        std::string Key(const char* folder, const char* name) {
            return std::string(folder) + "/" + name;
        }

        std::string Number(double value) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", value);
            return buffer;
        }

        /// What the test checks of one entry of `ui_tree`, as one line; @p checked is the state of a toggle
        std::string TreeLine(const char* type, const Control& control, bool checked) {
            std::string line = std::string("type=") + type + "; text=" + control.text + "; icon=" + control.icon + "; action=" + control.action + "; value=" + control.value +
                               "; tooltip=" + control.tooltip + "; bind=" + control.bind + "; range=" + control.range + "; disabled=" + (control.disabled ? "true" : "false");
            if (control.kind == Kind::Toggle) {
                line += std::string("; checked=") + (checked ? "true" : "false");
            }
            return line;
        }

        std::string TreeLineOf(const Json& element) {
            std::string range;
            if (const auto min = GetJsonNumber(element, "minValue")) {
                range = Number(*min) + ".." + Number(GetJsonNumber(element, "maxValue").value_or(0.0)) + " step " + Number(GetJsonNumber(element, "step").value_or(0.0));
            }
            // A slider answers `value` as a number: the clicker's value is the string; only a string is read here
            std::string line = "type=" + JsonText(element, "type") + "; text=" + JsonText(element, "text") + "; icon=" + JsonText(element, "icon") + "; action=" + JsonText(element, "action") +
                               "; value=" + (range.empty() ? JsonText(element, "value") : std::string()) + "; tooltip=" + JsonText(element, "tooltip") + "; bind=" + JsonText(element, "bind") +
                               "; range=" + range + "; disabled=" + JsonFlag(element, "disabled");
            if (FindJsonMember(element, "checked") != nullptr) {
                line += "; checked=" + JsonFlag(element, "checked");
            }
            return line;
        }

    }  // namespace

    class ContentMigrationAfterTest : public Fixture {
    protected:
        void LoadCase(const std::string& xml) {
            ASSERT_TRUE(Load(xml));
            const std::vector<std::string> sceneNames = SceneNames(xml);  // a node file is put into a scene of its own, a SceneConfig names its scenes
            ASSERT_FALSE(sceneNames.empty());
            _scene = _manager->GetSceneByName(PoolString::Intern(sceneNames.front().c_str()));
            ASSERT_NE(_scene, nullptr);
            _manager->SetActiveScene(PoolString::Intern(sceneNames.front().c_str()));
            _canvases = CanvasesOf(*_scene);
            ShowEverything(*_scene, _canvases);
            Draw(kLargeWidth, kLargeHeight);
        }

        /// The first element of that name in the scene and the canvas it is in
        UIElement* Find(const std::string& name, const UICanvasComponent** canvas = nullptr) {
            for (const CanvasRef& ref : _canvases) {
                UIElement* found = nullptr;
                WalkElements(ref.canvas->GetRoot(), [&](UIElement& element) {
                    if (found == nullptr && name == element.name.CStr()) {
                        found = &element;
                    }
                });
                if (found != nullptr) {
                    if (canvas != nullptr) {
                        *canvas = ref.canvas.Get();
                    }
                    return found;
                }
            }
            return nullptr;
        }

        /// What the layout validator finds on the size matrix, as "kind | element | other" (the case of the matrix is dropped: a finding is named by what it is about)
        Set<std::string> ValidatorFindings() {
            Set<std::string> findings;
            UILayoutValidatorOptions options;
            options.minTouchSize = 0.0f;
            UILayoutContext context;
            context.sceneViewWidth = _scene->GetViewWidth();
            context.sceneViewHeight = _scene->GetViewHeight();
            for (const CanvasRef& ref : _canvases) {
                for (const UIMatrixFailure& failure : ValidateOnMatrix(ref.canvas->GetLayout(), options, context)) {
                    for (const UILayoutIssue& issue : failure.issues) {
                        findings.insert(std::string(EnumUtils<UILayoutIssueKind>::ToString(issue.kind).data()) + " | " + issue.element.CStr() + " | " + issue.other.CStr());
                    }
                }
            }
            return findings;
        }

        /// The tree answer of every named element, by name (the first of a name wins)
        Map<std::string, std::string> Tree() {
            Map<std::string, std::string> lines;
            for (const Json& canvas : CallTree(kNodeSceneName)) {
                Set<std::string> seen;
                CollectTree(canvas, "", seen, lines, TreeLineOf);
            }
            Map<std::string, std::string> byName;
            for (const auto& [key, line] : lines) {
                byName.emplace(key.substr(key.find('|') + 1), line);
            }
            return byName;
        }

        /// A click at the centre of @p element, then the pointer is parked; the actions that went out
        std::vector<std::string> ClickOn(const UICanvasComponent& canvas, const UIElement& element) {
            const std::vector<std::string> emitted = Click(OutputCenter(canvas, element));
            Park();
            Draw(kLargeWidth, kLargeHeight);
            return emitted;
        }

        void CheckControl(const std::string& label, const Control& control, const Map<std::string, std::string>& tree) {
            const UICanvasComponent* canvas = nullptr;
            UIElement* element = Find(control.name, &canvas);
            ASSERT_NE(element, nullptr) << label << ": no element " << control.name;
            EXPECT_FALSE(element->rect.IsEmpty()) << label << " " << control.name << " has no rect";

            const auto line = tree.find(control.name);
            ASSERT_NE(line, tree.end()) << label << ": ui_tree does not list " << control.name;
            EXPECT_EQ(line->second, TreeLine("UIElement", control, false)) << label << " " << control.name << " in ui_tree";

            const std::string action = control.action;
            const std::string expected = control.disabled || action.empty() ? ""
                                                                            : action + "=" +
                                                                                  (control.kind == Kind::Toggle   ? control.on
                                                                                   : control.kind == Kind::Slider ? control.middle
                                                                                                                  : control.value);
            const auto emitted = [&](const std::vector<std::string>& actions) { return actions.empty() ? std::string() : actions.front(); };

            std::vector<const UIElement*> targets{element};
            if (control.kind == Kind::Toggle) {
                for (const char* suffix : {"Caption", "Switch"}) {
                    if (const UIElement* part = Find(std::string(control.name) + suffix); part != nullptr && !part->rect.IsEmpty()) {
                        targets.push_back(part);
                    }
                }
            }
            for (const UIElement* target : targets) {
                const std::vector<std::string> actions = ClickOn(*canvas, *target);
                EXPECT_LE(actions.size(), 1u) << label << " a click on " << target->name.CStr() << " emitted more than one action";
                EXPECT_EQ(emitted(actions), expected) << label << " a click on " << target->name.CStr();
            }

            EXPECT_TRUE(Click({kOutside, kOutside}).empty()) << label << " a click on nothing emitted an action";

            if (control.kind == Kind::Toggle) {
                CoreManager::GetUIVars().Set(PoolString::Intern(control.bind), control.on);
                Draw(kLargeWidth, kLargeHeight);
                const Map<std::string, std::string> on = Tree();
                const auto onLine = on.find(control.name);
                ASSERT_NE(onLine, on.end());
                EXPECT_EQ(onLine->second, TreeLine("UIElement", control, true)) << label << " " << control.name << " in ui_tree with the variable on";
                const std::string flipped = control.disabled || action.empty() ? "" : action + "=" + control.off;
                EXPECT_EQ(emitted(ClickOn(*canvas, *element)), flipped) << label << " a click on " << control.name << " with the variable on";
                CoreManager::GetUIVars().Clear();
                Draw(kLargeWidth, kLargeHeight);
            }
        }

        /// Every control that is not a member of a group is disabled by its group (the way a `UISegmented` or a form disables what it holds): a click on it, or on a part of it,
        /// emits nothing. A member of a group takes its flags from the group at every draw, so it is not set by hand
        void CheckGroupDisabled(const std::string& label, const Expectation& expectation) {
            Set<const UIElement*> members;
            std::vector<UIElement*> clickers;
            for (const CanvasRef& ref : _canvases) {
                WalkElements(ref.canvas->GetRoot(), [&](UIElement& element) {
                    if (element.Cast<UISegmented>() != nullptr) {
                        WalkElements(element, [&](UIElement& member) { members.insert(&member); });
                    }
                    if (element.GetComponent<UIInputClicker>() != nullptr) {
                        clickers.push_back(&element);
                    }
                });
            }
            for (UIElement* element : clickers) {
                element->groupDisabled = !members.contains(element);
            }
            Draw(kLargeWidth, kLargeHeight);
            for (const Control& control : expectation.controls) {
                const UICanvasComponent* canvas = nullptr;
                const UIElement* element = Find(control.name, &canvas);
                if (element != nullptr && element->groupDisabled) {
                    EXPECT_TRUE(ClickOn(*canvas, *element).empty()) << label << " " << control.name << " answers a click while its group disables it";
                }
            }
            for (UIElement* element : clickers) {
                element->groupDisabled = false;
            }
            Draw(kLargeWidth, kLargeHeight);
        }

        Scene* _scene = nullptr;
        std::vector<CanvasRef> _canvases;
    };

    TEST(ContentMigrationAfterFixtures, EveryFixtureIsLoadedOrSkippedWithAReason) {
        Set<std::string> found;
        for (const char* folder : {"ui_button", "ui_group", "ui_value", "ui_text"}) {
            ASSERT_TRUE(std::filesystem::is_directory(FixtureFolder(folder))) << FixtureFolder(folder).string();
            for (const std::string& name : CaseNames(FixtureFolder(folder))) {
                found.insert(Key(folder, name.c_str()));
            }
        }
        Set<std::string> known;
        for (const Expectation& expectation : Expectations()) {
            known.insert(Key(expectation.folder, expectation.name));
        }
        for (const char* skipped : kSkipped) {
            EXPECT_TRUE(known.insert(skipped).second) << skipped << " is both loaded and skipped";
        }
        EXPECT_EQ(found, known) << "a fixture pair with no entry in Expectations() or kSkipped, or an entry with no pair";
    }

    TEST(ContentMigrationAfterFixtures, ASkippedFixtureHoldsATypeALaterStepRewrites) {
        static const std::regex removed(R"re(type="(UIButton|UILabel|UIImage|UIToggle|UISlider|UIDropdown)")re");
        static const std::regex removedInText(R"re(type="(UIButton|UILabel|UIImage|UIToggle|UISlider)")re");
        for (const char* skipped : kSkipped) {
            const std::string key = skipped;
            const std::string folder = key.substr(0, key.find('/'));
            const std::string name = key.substr(key.find('/') + 1);
            const std::string xml = ReadFile(FixtureFolder(folder.c_str()) / (name + ".after.xml"));
            ASSERT_FALSE(xml.empty()) << key;
            EXPECT_TRUE(std::regex_search(xml, removed)) << key << " holds no such element: load it instead of skipping it";
        }
        for (const Expectation& expectation : Expectations()) {
            const std::string xml = ReadFile(FixtureFolder(expectation.folder) / (std::string(expectation.name) + ".after.xml"));
            ASSERT_FALSE(xml.empty()) << expectation.name;
            // The dropdown stays a class: in `ui_text/` it is the final form, with its header as a text component
            const bool isText = std::string(expectation.folder) == "ui_text";
            EXPECT_FALSE(std::regex_search(xml, isText ? removedInText : removed)) << Key(expectation.folder, expectation.name) << " holds an element of a class that is removed: skip it";
        }
    }

    TEST_P(ContentMigrationAfterTest, TheComposedScreenBehaves) {
        const Expectation& expectation = Expectations()[static_cast<size_t>(std::stoi(GetParam()))];
        const std::string label = Key(expectation.folder, expectation.name);
        const std::string xml = SceneManagerXml(ReadFile(FixtureFolder(expectation.folder) / (std::string(expectation.name) + ".after.xml")));
        ASSERT_FALSE(xml.empty());
        LoadCase(xml);
        ASSERT_NE(_scene, nullptr);

        for (const std::string& finding : ValidatorFindings()) {
            EXPECT_TRUE(kExpectedIssues.contains(label + " | " + finding)) << label << ": the validator finds on the size matrix: " << finding;
        }

        // The controls have a rect at the second output size too
        Draw(kSmallWidth, kSmallHeight);
        for (const Control& control : expectation.controls) {
            const UIElement* element = Find(control.name);
            ASSERT_NE(element, nullptr) << label << ": no element " << control.name;
            EXPECT_FALSE(element->rect.IsEmpty()) << label << " " << control.name << " has no rect at " << kSmallWidth << " x " << kSmallHeight;
        }
        Draw(kLargeWidth, kLargeHeight);

        const Map<std::string, std::string> tree = Tree();
        for (const Control& control : expectation.controls) {
            CheckControl(label, control, tree);
        }
        CheckGroupDisabled(label, expectation);
    }

    /// The pictures of a claim button (`ui_text/picture_under_clicker`): a picture component follows the state of the clicker above it, and the step writes its three state tints equal
    /// to its tint so that it keeps the colour the `UIImage` had; the tints of every picture drawn are the same at rest, under the pointer, pressed and disabled
    class ContentMigrationPictureTest : public ContentMigrationAfterTest {
    protected:
        /// The tints of the pictures drawn in the last frame (the test gives every sprite the same texture), in draw order
        std::vector<uint32_t> PictureTints() const {
            std::vector<uint32_t> tints;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.texture == _icon.Get()) {
                    tints.push_back((static_cast<uint32_t>(call.color.r) << 24) | (static_cast<uint32_t>(call.color.g) << 16) | (static_cast<uint32_t>(call.color.b) << 8) | call.color.a);
                }
            }
            return tints;
        }
    };

    TEST_P(ContentMigrationPictureTest, APictureUnderAClickerKeepsItsColourInEveryState) {
        LoadCase(SceneManagerXml(ReadFile(FixtureFolder("ui_text") / "picture_under_clicker.after.xml")));
        ASSERT_NE(_scene, nullptr);
        Draw(kLargeWidth, kLargeHeight);
        const std::vector<uint32_t> atRest = PictureTints();
        ASSERT_EQ(atRest.size(), 4u) << "three tiles on two buttons and one picture beside them";

        for (const char* name : {"Pung", "Chow0"}) {
            const UICanvasComponent* canvas = nullptr;
            UIElement* button = Find(name, &canvas);
            ASSERT_NE(button, nullptr) << name;
            const Vec2 centre = OutputCenter(*canvas, *button);

            CoreManager::GetUIInput().DispatchMouseMotion(Motion(centre.x, centre.y));
            Draw(kLargeWidth, kLargeHeight);
            EXPECT_TRUE(button->hovered) << name;
            EXPECT_EQ(PictureTints(), atRest) << name << " hovered";

            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(centre.x, centre.y, true));
            Draw(kLargeWidth, kLargeHeight);
            EXPECT_TRUE(button->pressed) << name;
            EXPECT_EQ(PictureTints(), atRest) << name << " pressed";
            CoreManager::GetUIInput().DispatchMouseButtonUp(Button(centre.x, centre.y, false));
            Park();

            UIInputClicker* clicker = button->GetComponent<UIInputClicker>();
            ASSERT_NE(clicker, nullptr) << name;
            clicker->disabled = true;
            Draw(kLargeWidth, kLargeHeight);
            EXPECT_EQ(PictureTints(), atRest) << name << " disabled";
            clicker->disabled = false;
            Draw(kLargeWidth, kLargeHeight);
        }
    }

    INSTANTIATE_TEST_SUITE_P(Fixtures, ContentMigrationPictureTest, ::testing::Values(std::string("picture_under_clicker")));

    INSTANTIATE_TEST_SUITE_P(Fixtures, ContentMigrationAfterTest, ::testing::ValuesIn([] {
                                 std::vector<std::string> indices;
                                 for (size_t i = 0; i < Expectations().size(); ++i) {
                                     indices.push_back(std::to_string(i));
                                 }
                                 return indices;
                             }()),
                             [](const ::testing::TestParamInfo<std::string>& info) {
                                 const Expectation& expectation = Expectations()[static_cast<size_t>(std::stoi(info.param))];
                                 return std::string(expectation.folder) + "_" + expectation.name;
                             });

}  // namespace BECore::Tests
