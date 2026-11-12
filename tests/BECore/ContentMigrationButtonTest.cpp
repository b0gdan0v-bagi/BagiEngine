#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIControl.h>
#include <Common/MigrationTwin.h>
#include <Common/UILayoutMatrix.h>
#include <gtest/gtest.h>
#include <map>
#include <set>

namespace BECore::Tests {

    using namespace MigrationTwin;

    /*
     * The twin test of the migration step 1 to 2 (`tools/migrate/steps/s0001_ui_button_to_components.py`): a `UIButton` rewritten as a composed element must behave like the
     * button it was. Every `<case>.before.xml` / `<case>.after.xml` pair of `tools/migrate/tests/fixtures/ui_button/` (the files the step's pytest checks the output against; a
     * failure fixture has no `after`) is loaded into a canvas on a `RecordingRenderer`, once as it was and once as it is now, and for every element of the same name the test
     * compares: the laid-out rect at two output sizes, the draw of the whole scene with that element normal, hovered, pressed, selected and disabled, what `ui_tree` says about
     * it (`type` and `components` differ by design), the `ActionEvent` of a click (none when disabled), its tooltip, and the findings of the layout validator on the size matrix.
     * When a case differs the mapping of the step is wrong and the step is fixed, not this test.
     *
     * A button with an icon becomes an image child (and a caption child): the sprite of every `UIButton` icon and of every `UIImageComponent` is the one texture of the test, so the
     * icon quad is in the draws, and an icon case (`icon_only`, `icon_text`) compares the draws within `kIconTolerance` (the icon and the caption are other elements now and may land
     * on a rounding of the layout) with the colours and the number of calls exact.
     *
     * The two forms are loaded one after the other, never together: both would register their canvases with the one `UIInput` and answer the same pointer.
     */

    namespace {

        constexpr const char* kIconCases[] = {"icon_only", "icon_text"};  // the pairs whose buttons have an icon
        constexpr const char* kStateNormal = "normal";
        constexpr const char* kStateHover = "hovered";
        constexpr const char* kStatePressed = "pressed";
        constexpr const char* kStateSelected = "selected";
        constexpr const char* kStateDisabled = "disabled by its group";
        constexpr const char* kTreeNormal = "normal";
        constexpr const char* kTreeFlags = "selected and disabled by the group";

        /// What one form of a screen did, by the key "scene|node|element" (the first element of that name in the canvas)
        struct Snapshot {
            std::map<std::string, std::vector<Rect>> rects;                               // one per output size
            std::map<std::string, std::map<std::string, eastl::vector<DrawCall>>> draws;  // state -> the whole scene's draw
            std::map<std::string, std::map<std::string, std::string>> trees;              // state of the tree -> what `ui_tree` says
            std::map<std::string, std::vector<std::string>> clicks;                       // "normal" / "disabled" -> the actions emitted
            std::map<std::string, std::string> tooltips;
            std::set<std::string> issues;
        };

        /// What `ui_tree` says about an element that a migration must keep: a missing key and an empty or false one are the same answer
        std::string TreeFields(const Json& element) {
            std::string fields;
            for (const char* key : {"text", "icon", "action", "value", "tooltip"}) {
                fields += std::string(key) + "=" + JsonText(element, key) + "; ";
            }
            for (const char* key : {"disabled", "selected", "interactive", "visible", "blocksInput"}) {
                fields += std::string(key) + "=" + JsonFlag(element, key) + "; ";
            }
            return fields;
        }

    }  // namespace

    class ContentMigrationButtonTest : public Fixture {
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

            // The rect of every named element at two output sizes
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
                const std::string canvasKey = sceneName + "|" + ref.node;
                CaptureElements(sceneName, ref, snapshot);
                CaptureValidator(*scene, ref, canvasKey, snapshot);
            }
            Park();
            Draw(kLargeWidth, kLargeHeight);
            CaptureTree(sceneName, kTreeNormal, snapshot);
            SetFlags(canvases, true);
            Draw(kLargeWidth, kLargeHeight);
            CaptureTree(sceneName, kTreeFlags, snapshot);
            SetFlags(canvases, false);
            Draw(kLargeWidth, kLargeHeight);
        }

        /// `selected` and `groupDisabled` on every element that takes clicks: the state of a segment of a group
        static void SetFlags(const std::vector<CanvasRef>& canvases, bool on) {
            for (const CanvasRef& ref : canvases) {
                WalkElements(ref.canvas->GetRoot(), [on](UIElement& element) {
                    if (element.IsInteractive()) {
                        element.selected = on;
                        element.groupDisabled = on;
                    }
                });
            }
        }

        void CaptureTree(const std::string& sceneName, const char* state, Snapshot& snapshot) {
            const Json list = CallTree(sceneName);
            std::map<std::string, std::string> fields;
            for (const Json& canvas : list) {
                const std::string canvasKey = sceneName + "|" + JsonText(canvas, "node");
                std::set<std::string> seen;
                CollectTree(canvas, canvasKey, seen, fields, TreeFields);
            }
            for (const auto& [key, text] : fields) {
                snapshot.trees[key][state] = text;
            }
        }

        /// The draw of the scene, the answer to a click and the tooltip of every element that takes clicks, in each state
        void CaptureElements(const std::string& sceneName, const CanvasRef& ref, Snapshot& snapshot) {
            std::vector<UIElement*> targets;
            std::set<std::string> seen;
            WalkElements(ref.canvas->GetRoot(), [&](UIElement& element) {
                if (!element.name.Empty() && element.IsInteractive() && !element.rect.IsEmpty() && seen.insert(element.name.CStr()).second) {
                    targets.push_back(&element);
                }
            });
            for (UIElement* element : targets) {
                const std::string key = Where(sceneName, ref.node, element->name.CStr());
                snapshot.tooltips[key] = Text(element->GetTooltipText());
                const Vec2 at = OutputCenter(*ref.canvas, *element);
                auto& draws = snapshot.draws[key];

                Park();
                Draw(kLargeWidth, kLargeHeight);
                draws[kStateNormal] = _renderer->calls;

                CoreManager::GetUIInput().DispatchMouseMotion(Motion(at.x, at.y));
                Draw(kLargeWidth, kLargeHeight);
                draws[kStateHover] = _renderer->calls;

                CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true));
                Draw(kLargeWidth, kLargeHeight);
                draws[kStatePressed] = _renderer->calls;
                CoreManager::GetUIInput().DispatchMouseButtonUp(Button(kOutside, kOutside, false));  // let go elsewhere: no click
                Park();

                element->selected = true;
                Draw(kLargeWidth, kLargeHeight);
                draws[kStateSelected] = _renderer->calls;
                element->selected = false;

                element->groupDisabled = true;
                Draw(kLargeWidth, kLargeHeight);
                draws[kStateDisabled] = _renderer->calls;
                snapshot.clicks[key + "|disabled"] = Click(at);
                element->groupDisabled = false;

                Draw(kLargeWidth, kLargeHeight);
                snapshot.clicks[key + "|normal"] = Click(at);
                Park();
                Draw(kLargeWidth, kLargeHeight);
            }
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
                    snapshot.issues.insert(canvasKey + " | " + Text(failure.label) + " | " + std::string(EnumUtils<UILayoutIssueKind>::ToString(issue.kind).data()) + " | " + issue.element.CStr() +
                                           " | " + issue.other.CStr());
                }
            }
        }
    };

    TEST(ContentMigrationButtonFixtures, TheFixturePairsAreThere) {
        ASSERT_TRUE(std::filesystem::is_directory(FixtureFolder("ui_button"))) << FixtureFolder("ui_button").string();
        const std::vector<std::string> names = CaseNames(FixtureFolder("ui_button"));
        for (const char* required : {"mahjong_hints_button", "mahjong_hand_over_action", "mahjong_setup", "match3_menu_hud", "blocker", "text_attributes", "disabled_tooltip", "with_children",
                                     "already_composed", "icon_attributes_without_icon", "long_caption", "icon_only", "icon_text"}) {
            EXPECT_TRUE(std::find(names.begin(), names.end(), required) != names.end()) << "no pair for " << required;
        }
    }

    TEST_P(ContentMigrationButtonTest, TheMigratedButtonsBehaveLikeTheOldOnes) {
        const std::string name = GetParam();
        const std::string before = SceneManagerXml(ReadFile(FixtureFolder("ui_button") / (name + ".before.xml")));
        const std::string after = SceneManagerXml(ReadFile(FixtureFolder("ui_button") / (name + ".after.xml")));
        ASSERT_FALSE(before.empty());
        ASSERT_FALSE(after.empty());

        const bool iconCase = std::find(std::begin(kIconCases), std::end(kIconCases), name) != std::end(kIconCases);
        const float tolerance = iconCase ? kIconTolerance : kTolerance;
        Snapshot old;
        Snapshot now;
        ASSERT_TRUE(Capture(before, old));
        ASSERT_TRUE(Capture(after, now));
        ASSERT_FALSE(old.rects.empty()) << name << ": no element with a name";

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

        EXPECT_FALSE(old.draws.empty()) << name << ": no element that takes clicks, nothing to draw in a state";
        if (iconCase) {
            // The test would pass without an icon quad in either form: every button of an icon case draws the icon in its normal state
            for (const Snapshot* form : {&old, &now}) {
                for (const auto& [key, states] : form->draws) {
                    const bool drawsIcon = std::any_of(states.at(kStateNormal).begin(), states.at(kStateNormal).end(),
                                                       [this](const DrawCall& call) { return call.kind == DrawKind::Texture && call.texture == _icon.Get(); });
                    EXPECT_TRUE(drawsIcon) << name << " " << key << " draws no icon quad" << (form == &old ? " as a UIButton" : " composed");
                }
            }
        }
        for (const auto& [key, states] : old.draws) {
            const auto found = now.draws.find(key);
            ASSERT_NE(found, now.draws.end()) << name << ": " << key << " no longer takes clicks";
            for (const auto& [state, calls] : states) {
                const std::string difference = DrawDifference(calls, found->second.at(state), tolerance);
                EXPECT_TRUE(difference.empty()) << name << " " << key << " " << state << ": " << difference;
            }
        }

        for (const auto& [key, states] : old.trees) {
            const auto found = now.trees.find(key);
            ASSERT_NE(found, now.trees.end()) << name << ": ui_tree no longer lists " << key;
            for (const auto& [state, fields] : states) {
                EXPECT_EQ(fields, found->second.at(state)) << name << " " << key << " in the tree, state " << state;
            }
        }

        for (const auto& [key, emitted] : old.clicks) {
            const auto found = now.clicks.find(key);
            ASSERT_NE(found, now.clicks.end()) << name << " " << key;
            EXPECT_EQ(emitted, found->second) << name << " a click on " << key;
        }
        for (const auto& [key, tooltip] : old.tooltips) {
            EXPECT_EQ(tooltip, now.tooltips[key]) << name << " the tooltip of " << key;
        }
        for (const std::string& issue : old.issues) {
            EXPECT_TRUE(now.issues.contains(issue)) << name << ": the validator found on the size matrix only before: " << issue;
        }
        for (const std::string& issue : now.issues) {
            EXPECT_TRUE(old.issues.contains(issue)) << name << ": the validator finds on the size matrix only now: " << issue;
        }
    }

    INSTANTIATE_TEST_SUITE_P(Fixtures, ContentMigrationButtonTest, ::testing::ValuesIn(CaseNames(FixtureFolder("ui_button"))));

}  // namespace BECore::Tests
