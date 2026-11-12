#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIChoice.h>
#include <BECore/UI/UIControl.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UISegmented.h>
#include <Common/MigrationTwin.h>
#include <Common/UILayoutMatrix.h>
#include <gtest/gtest.h>
#include <map>
#include <set>

namespace BECore::Tests {

    using namespace MigrationTwin;

    /*
     * The twin test of the migration step 2 to 3 (`tools/migrate/steps/s0002_ui_group_children_to_components.py`): the segments of a `UISegmented` and the options of a `UIDropdown`
     * rewritten as composed elements must make the group behave as it did. Every `<case>.before.xml` / `<case>.after.xml` pair of `tools/migrate/tests/fixtures/ui_group/` is loaded
     * into canvases on a `RecordingRenderer`, once as it was and once as it is now, and for every group of the screen the test compares:
     *   - the laid-out rect of every named element at two output sizes;
     *   - the draw of the whole scene with the variable of the group set to each value of its options in turn and to none, and with the group `disabled`;
     *   - what `ui_tree` says about every element in those states (`type` and `components` differ by design), the group's `bind`, `selectedValue`, `open` among them, which for a
     *     dropdown is also the text of its header;
     *   - the `ActionEvent` of a click on each segment, with the group enabled and disabled;
     *   - for a dropdown: the list opened by a click on the header (the rect of each option and the draw), the `(action, value)` emitted by a click on each option and whether the list
     *     is open after it (a disabled option emits nothing and keeps it open), a `disabled` dropdown that stays closed;
     *   - the findings of the layout validator on the size matrix, with the lists closed and open.
     * When a case differs the mapping of the step is wrong and the step is fixed, not this test.
     *
     * An icon case (a segment or an option with an icon) is compared within `kIconTolerance`: the icon and the caption are other elements now and may land on a rounding of the layout;
     * the colours and the number of calls stay exact.
     */

    namespace {

        constexpr const char* kIconCases[] = {"segmented_icon", "dropdown_disabled_option"};  // the pairs whose segments or options have an icon
        constexpr const char* kNoValue = "(none)";
        constexpr const char* kStateDisabled = "disabled";
        constexpr const char* kStateHover = "hover";
        constexpr const char* kStateClosed = "closed";
        constexpr const char* kStateOpen = "open";

        /// What one form of a screen did; the keys name the group, the state and, for a tree, the element
        struct Snapshot {
            std::map<std::string, std::vector<Rect>> rects;          // "scene|node|element" -> one per output size
            std::map<std::string, eastl::vector<DrawCall>> draws;    // "group|state" -> the whole scene's draw
            std::map<std::string, std::string> trees;                // "group|state|scene|node|element" -> what `ui_tree` says
            std::map<std::string, std::vector<std::string>> clicks;  // "group|what" -> the actions emitted
            std::map<std::string, std::vector<Rect>> popups;         // "group|state" -> the rect of each option
            std::map<std::string, std::string> answers;              // "group|what" -> a short answer (is the list open)
            std::set<std::string> issues;                            // from the validator
        };

        /// What `ui_tree` says about an element that a migration must keep: a missing key and an empty or false one are the same answer
        std::string TreeFields(const Json& element) {
            std::string fields;
            for (const char* key : {"text", "icon", "action", "value", "tooltip", "bind", "selectedValue"}) {
                fields += std::string(key) + "=" + JsonText(element, key) + "; ";
            }
            for (const char* key : {"disabled", "selected", "interactive", "visible", "blocksInput", "open"}) {
                fields += std::string(key) + "=" + JsonFlag(element, key) + "; ";
            }
            return fields;
        }

        std::string Joined(const std::vector<std::string>& lines) {
            std::string joined;
            for (const std::string& line : lines) {
                joined += line + ",";
            }
            return joined;
        }

        void SetVariable(UIElement& group, const std::string& value) {
            const PoolString bind = group.Cast<UISegmented>() != nullptr ? group.Cast<UISegmented>()->bind : group.Cast<UIDropdown>()->bind;
            if (value == kNoValue) {
                CoreManager::GetUIVars().Remove(bind);
            } else {
                CoreManager::GetUIVars().Set(bind, eastl::string_view(value.data(), value.size()));
            }
        }

        /// The options of a group in order (a `UIButton` or an element with a clicker), the direct children only
        std::vector<UIElement*> OptionsOf(UIElement& group) {
            std::vector<UIElement*> options;
            for (const auto& child : group.children) {
                if (UIChoice::IsOption(*child)) {
                    options.push_back(child.Get());
                }
            }
            return options;
        }

        std::vector<std::string> ValuesOf(UIElement& group) {
            std::vector<std::string> values;
            for (UIElement* option : OptionsOf(group)) {
                values.emplace_back(UIChoice::GetValue(*option).CStr());
            }
            values.emplace_back(kNoValue);
            return values;
        }

        bool IsGroup(UIElement& element) {
            return element.Cast<UISegmented>() != nullptr || element.Cast<UIDropdown>() != nullptr;
        }

        bool IsOpen(UIElement& group) {
            auto* dropdown = group.Cast<UIDropdown>();
            return dropdown != nullptr && dropdown->IsOpen();
        }

    }  // namespace

    class ContentMigrationGroupTest : public Fixture {
    protected:
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
                        if (!element.name.Empty() && seen.insert(key).second) {
                            snapshot.rects[key].push_back(element.rect);
                        }
                    });
                }
            }

            Draw(kLargeWidth, kLargeHeight);
            for (const CanvasRef& ref : canvases) {
                std::vector<UIElement*> groups;
                std::set<std::string> seen;
                WalkElements(ref.canvas->GetRoot(), [&](UIElement& element) {
                    if (!element.name.Empty() && IsGroup(element) && seen.insert(element.name.CStr()).second) {
                        groups.push_back(&element);
                    }
                });
                for (UIElement* group : groups) {
                    CaptureGroup(sceneName, ref, *group, snapshot);
                }
                CaptureValidator(*scene, ref, sceneName + "|" + ref.node, snapshot);
            }
        }

        /// The draw and the tree of the screen in one state of one group
        void Record(const std::string& sceneName, const std::string& group, const std::string& state, Snapshot& snapshot) {
            Draw(kLargeWidth, kLargeHeight);
            snapshot.draws[group + "|" + state] = _renderer->calls;
            std::map<std::string, std::string> fields;
            for (const Json& canvas : CallTree(sceneName)) {
                const std::string canvasKey = sceneName + "|" + JsonText(canvas, "node");
                std::set<std::string> seen;
                CollectTree(canvas, canvasKey, seen, fields, TreeFields);
            }
            for (const auto& [key, text] : fields) {
                snapshot.trees[group + "|" + state + "|" + key] = text;
            }
        }

        void CaptureGroup(const std::string& sceneName, const CanvasRef& ref, UIElement& group, Snapshot& snapshot) {
            const std::string name = group.name.CStr();
            const std::vector<std::string> values = ValuesOf(group);
            auto* dropdown = group.Cast<UIDropdown>();
            auto* segmented = group.Cast<UISegmented>();
            const auto setDisabled = [&](bool on) {
                if (dropdown != nullptr) {
                    dropdown->disabled = on;
                } else {
                    segmented->disabled = on;
                }
            };

            // The variable chooses each value in turn, the group closed
            for (const std::string& value : values) {
                SetVariable(group, value);
                Record(sceneName, name, std::string(kStateClosed) + " value=" + value, snapshot);
            }
            SetVariable(group, values.front());
            setDisabled(true);
            Record(sceneName, name, kStateDisabled, snapshot);
            setDisabled(false);
            Draw(kLargeWidth, kLargeHeight);

            if (segmented != nullptr) {
                CaptureSegments(sceneName, ref, group, snapshot);
            } else {
                CaptureDropdown(sceneName, ref, *dropdown, values, snapshot);
            }
            CoreManager::GetUIVars().Clear();
            Park();
            Draw(kLargeWidth, kLargeHeight);
        }

        void CaptureSegments(const std::string& sceneName, const CanvasRef& ref, UIElement& group, Snapshot& snapshot) {
            const std::string name = group.name.CStr();
            const std::vector<UIElement*> options = OptionsOf(group);
            auto* segmented = group.Cast<UISegmented>();
            for (size_t i = 0; i < options.size(); ++i) {
                const std::string label = name + "|segment " + std::to_string(i);
                const Vec2 at = OutputCenter(*ref.canvas, *options[i]);
                Park();
                Draw(kLargeWidth, kLargeHeight);
                CoreManager::GetUIInput().DispatchMouseMotion(Motion(at.x, at.y));
                Record(sceneName, name, std::string(kStateHover) + " " + std::to_string(i), snapshot);
                snapshot.clicks[label] = Click(at);
                Park();
                Draw(kLargeWidth, kLargeHeight);
                segmented->disabled = true;
                Draw(kLargeWidth, kLargeHeight);
                snapshot.clicks[label + "|disabled"] = Click(at);
                segmented->disabled = false;
                Park();
                Draw(kLargeWidth, kLargeHeight);
                // A disabled segment of its own (a clicker or a button with `disabled`)
                snapshot.answers[label + "|disabled by itself"] = options[i]->IsDisabled() ? "true" : "false";
            }
        }

        std::vector<Rect> PopupRects(UIElement& group) {
            std::vector<Rect> rects;
            for (UIElement* option : OptionsOf(group)) {
                rects.push_back(option->rect);
            }
            return rects;
        }

        void CaptureDropdown(const std::string& sceneName, const CanvasRef& ref, UIDropdown& dropdown, const std::vector<std::string>& values, Snapshot& snapshot) {
            const std::string name = dropdown.name.CStr();
            const Vec2 header = OutputCenter(*ref.canvas, dropdown);

            // Open by a click on the header, with each value chosen: the list and the header text
            for (const std::string& value : values) {
                SetVariable(dropdown, value);
                Draw(kLargeWidth, kLargeHeight);
                Click(header);
                Draw(kLargeWidth, kLargeHeight);
                snapshot.answers[name + "|opened by the header|" + value] = dropdown.IsOpen() ? "open" : "closed";
                Record(sceneName, name, std::string(kStateOpen) + " value=" + value, snapshot);
                snapshot.popups[name + "|" + kStateOpen + " value=" + value] = PopupRects(dropdown);
                Click(header);  // the header toggles
                Draw(kLargeWidth, kLargeHeight);
                snapshot.answers[name + "|closed by the header|" + value] = dropdown.IsOpen() ? "open" : "closed";
            }

            // A click on each option: what goes out and whether the list stays open
            SetVariable(dropdown, values.front());
            const std::vector<UIElement*> options = OptionsOf(dropdown);
            for (size_t i = 0; i < options.size(); ++i) {
                Click(header);
                Draw(kLargeWidth, kLargeHeight);
                const std::string label = name + "|option " + std::to_string(i);
                snapshot.clicks[label] = Click(OutputCenter(*ref.canvas, *options[i]));
                snapshot.answers[label] = dropdown.IsOpen() ? "open" : "closed";
                if (dropdown.IsOpen()) {
                    Click(header);
                }
                Park();
                Draw(kLargeWidth, kLargeHeight);
            }

            // A press elsewhere closes the list and is consumed
            Click(header);
            Draw(kLargeWidth, kLargeHeight);
            ActionRecorder recorder;
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(1.0f, 1.0f));
            const bool consumed = CoreManager::GetUIInput().DispatchMouseButtonDown(Button(1.0f, 1.0f, true));
            CoreManager::GetUIInput().DispatchMouseButtonUp(Button(1.0f, 1.0f, false));
            snapshot.answers[name + "|press elsewhere"] = std::string(dropdown.IsOpen() ? "open" : "closed") + (consumed ? " consumed" : " passed") + " " + Joined(recorder.emitted);
            Draw(kLargeWidth, kLargeHeight);

            // A disabled dropdown never opens
            dropdown.disabled = true;
            Click(header);
            snapshot.answers[name + "|disabled click"] = dropdown.IsOpen() ? "open" : "closed";
            dropdown.disabled = false;
            Park();
            Draw(kLargeWidth, kLargeHeight);
        }

        /// What the layout validator finds on the size matrix, as "case | kind | element | other" lines, with every list closed and then open
        void CaptureValidator(Scene& scene, const CanvasRef& ref, const std::string& canvasKey, Snapshot& snapshot) {
            UILayoutValidatorOptions options;
            options.minTouchSize = kTouchSize;
            UILayoutContext context;
            context.sceneViewWidth = scene.GetViewWidth();
            context.sceneViewHeight = scene.GetViewHeight();
            const auto collect = [&](const char* state) {
                for (const UIMatrixFailure& failure : ValidateOnMatrix(ref.canvas->GetLayout(), options, context)) {
                    for (const UILayoutIssue& issue : failure.issues) {
                        snapshot.issues.insert(std::string(state) + " | " + canvasKey + " | " + Text(failure.label) + " | " + std::string(EnumUtils<UILayoutIssueKind>::ToString(issue.kind).data()) +
                                               " | " + issue.element.CStr() + " | " + issue.other.CStr());
                    }
                }
            };
            collect(kStateClosed);
            std::vector<UIDropdown*> opened;
            WalkElements(ref.canvas->GetRoot(), [&](UIElement& element) {
                if (auto* dropdown = element.Cast<UIDropdown>(); dropdown != nullptr && !dropdown->IsOpen()) {
                    dropdown->Open();
                    opened.push_back(dropdown);
                }
            });
            collect(kStateOpen);
            for (UIDropdown* dropdown : opened) {
                dropdown->Close();
            }
        }
    };

    TEST(ContentMigrationGroupFixtures, TheFixturePairsAreThere) {
        ASSERT_TRUE(std::filesystem::is_directory(FixtureFolder("ui_group"))) << FixtureFolder("ui_group").string();
        const std::vector<std::string> names = CaseNames(FixtureFolder("ui_group"));
        for (const char* required : {"mahjong_setup", "segmented_plain", "segmented_icon", "dropdown_options", "dropdown_disabled_option", "plain_button_beside_a_group", "already_composed"}) {
            EXPECT_TRUE(std::find(names.begin(), names.end(), required) != names.end()) << "no pair for " << required;
        }
    }

    TEST_P(ContentMigrationGroupTest, TheMigratedGroupsBehaveLikeTheOldOnes) {
        const std::string name = GetParam();
        const std::string before = SceneManagerXml(ReadFile(FixtureFolder("ui_group") / (name + ".before.xml")));
        const std::string after = SceneManagerXml(ReadFile(FixtureFolder("ui_group") / (name + ".after.xml")));
        ASSERT_FALSE(before.empty());
        ASSERT_FALSE(after.empty());

        const bool iconCase = std::find(std::begin(kIconCases), std::end(kIconCases), name) != std::end(kIconCases);
        const float tolerance = iconCase ? kIconTolerance : kTolerance;
        Snapshot old;
        Snapshot now;
        ASSERT_TRUE(Capture(before, old));
        ASSERT_TRUE(Capture(after, now));
        ASSERT_FALSE(old.rects.empty()) << name << ": no element with a name";
        ASSERT_FALSE(old.draws.empty()) << name << ": no group in the screen";

        for (const auto& [key, rects] : old.rects) {
            const auto found = now.rects.find(key);
            ASSERT_NE(found, now.rects.end()) << name << ": the element " << key << " is gone";
            ASSERT_EQ(found->second.size(), rects.size());
            for (size_t i = 0; i < rects.size(); ++i) {
                const Rect& a = rects[i];
                const Rect& b = found->second[i];
                EXPECT_TRUE(std::abs(a.x - b.x) <= kTolerance && std::abs(a.y - b.y) <= kTolerance && std::abs(a.w - b.w) <= kTolerance && std::abs(a.h - b.h) <= kTolerance)
                    << name << " " << key << " at output size " << i << ": the rect was (" << a.x << ", " << a.y << ", " << a.w << " x " << a.h << ") and is (" << b.x << ", " << b.y << ", " << b.w
                    << " x " << b.h << ")";
            }
        }

        if (iconCase) {
            // The test would pass without an icon quad in either form: some state of the screen draws one
            for (const Snapshot* form : {&old, &now}) {
                const bool drawsIcon = std::any_of(form->draws.begin(), form->draws.end(), [this](const auto& entry) {
                    return std::any_of(entry.second.begin(), entry.second.end(), [this](const DrawCall& call) { return call.kind == DrawKind::Texture && call.texture == _icon.Get(); });
                });
                EXPECT_TRUE(drawsIcon) << name << ": no icon quad in any draw" << (form == &old ? " as UIButtons" : " composed");
            }
        }
        for (const auto& [key, calls] : old.draws) {
            const auto found = now.draws.find(key);
            ASSERT_NE(found, now.draws.end()) << name << ": no draw of " << key;
            const std::string difference = DrawDifference(calls, found->second, tolerance);
            EXPECT_TRUE(difference.empty()) << name << " " << key << ": " << difference;
        }

        for (const auto& [key, fields] : old.trees) {
            const auto found = now.trees.find(key);
            ASSERT_NE(found, now.trees.end()) << name << ": ui_tree no longer lists " << key;
            EXPECT_EQ(fields, found->second) << name << " in the tree: " << key;
        }
        for (const auto& [key, emitted] : old.clicks) {
            EXPECT_EQ(emitted, now.clicks[key]) << name << " a click: " << key;
        }
        for (const auto& [key, answer] : old.answers) {
            EXPECT_EQ(answer, now.answers[key]) << name << ": " << key;
        }
        // The comparisons would pass on a screen where nothing was clicked or opened: some click went out, and every open list has a place
        EXPECT_TRUE(std::any_of(old.clicks.begin(), old.clicks.end(), [](const auto& entry) { return !entry.second.empty(); })) << name << ": no click emitted an action";
        for (const auto& [key, rects] : old.popups) {
            EXPECT_TRUE(std::any_of(rects.begin(), rects.end(), [](const Rect& rect) { return !rect.IsEmpty(); })) << name << ": the list of " << key << " has no place";
        }
        for (const auto& [key, rects] : old.popups) {
            const auto found = now.popups.find(key);
            ASSERT_NE(found, now.popups.end()) << name << ": no list of " << key;
            ASSERT_EQ(found->second.size(), rects.size()) << name << " " << key;
            for (size_t i = 0; i < rects.size(); ++i) {
                const Rect& a = rects[i];
                const Rect& b = found->second[i];
                EXPECT_TRUE(std::abs(a.x - b.x) <= kTolerance && std::abs(a.y - b.y) <= kTolerance && std::abs(a.w - b.w) <= kTolerance && std::abs(a.h - b.h) <= kTolerance)
                    << name << " " << key << " option " << i << ": the rect was (" << a.x << ", " << a.y << ", " << a.w << " x " << a.h << ") and is (" << b.x << ", " << b.y << ", " << b.w << " x "
                    << b.h << ")";
            }
        }
        for (const std::string& issue : old.issues) {
            EXPECT_TRUE(now.issues.contains(issue)) << name << ": the validator found on the size matrix only before: " << issue;
        }
        for (const std::string& issue : now.issues) {
            EXPECT_TRUE(old.issues.contains(issue)) << name << ": the validator finds on the size matrix only now: " << issue;
        }
    }

    INSTANTIATE_TEST_SUITE_P(Fixtures, ContentMigrationGroupTest, ::testing::ValuesIn(CaseNames(FixtureFolder("ui_group"))));

}  // namespace BECore::Tests
