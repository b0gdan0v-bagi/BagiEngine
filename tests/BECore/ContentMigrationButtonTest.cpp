#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIControl.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SubscriptionHolder.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace BECore::Tests {

    /*
     * The twin test of the migration step 1 to 2 (`tools/migrate/steps/s0001_ui_button_to_components.py`): a `UIButton` rewritten as a composed element must behave like the
     * button it was. Every `<case>.before.xml` / `<case>.after.xml` pair of `tools/migrate/tests/fixtures/ui_button/` (the files the step's pytest checks the output against; a
     * failure fixture has no `after`) is loaded into a canvas on a `RecordingRenderer`, once as it was and once as it is now, and for every element of the same name the test
     * compares: the laid-out rect at two output sizes, the draw of the whole scene with that element normal, hovered, pressed, selected and disabled, what `ui_tree` says about
     * it (`type` and `components` differ by design), the `ActionEvent` of a click (none when disabled), its tooltip, and the findings of the layout validator on the size matrix.
     * When a case differs the mapping of the step is wrong and the step is fixed, not this test.
     *
     * The two forms are loaded one after the other, never together: both would register their canvases with the one `UIInput` and answer the same pointer.
     */

    namespace {

        constexpr float kLargeWidth = 1280.0f;
        constexpr float kLargeHeight = 720.0f;
        constexpr float kSmallWidth = 600.0f;  // narrow and tall: the portrait variants apply and a stack of buttons is squeezed
        constexpr float kSmallHeight = 900.0f;
        constexpr float kDefaultViewWidth = 1280.0f;  // the view of the scene a node file is put into
        constexpr float kDefaultViewHeight = 720.0f;
        constexpr float kTolerance = 1e-3f;
        constexpr float kTouchSize = 48.0f;
        constexpr float kOutside = -100.0f;  // a point on no element, to park the pointer
        constexpr const char* kStateNormal = "normal";
        constexpr const char* kStateHover = "hovered";
        constexpr const char* kStatePressed = "pressed";
        constexpr const char* kStateSelected = "selected";
        constexpr const char* kStateDisabled = "disabled by its group";
        constexpr const char* kTreeNormal = "normal";
        constexpr const char* kTreeFlags = "selected and disabled by the group";
        constexpr const char* kNodeSceneName = "Main";

        std::filesystem::path FixtureFolder() {
            return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "tools" / "migrate" / "tests" / "fixtures" / "ui_button";
        }

        std::string ReadFile(const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary);
            std::ostringstream text;
            text << file.rdbuf();
            return text.str();
        }

        /// The names of the cases: every `<case>.before.xml` that has an `<case>.after.xml`
        std::vector<std::string> CaseNames() {
            std::vector<std::string> names;
            const std::string suffix = ".before.xml";
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(FixtureFolder(), error)) {
                const std::string file = entry.path().filename().string();
                if (file.size() > suffix.size() && file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    const std::string name = file.substr(0, file.size() - suffix.size());
                    if (std::filesystem::exists(FixtureFolder() / (name + ".after.xml"))) {
                        names.push_back(name);
                    }
                }
            }
            std::sort(names.begin(), names.end());
            return names;
        }

        /// A content file as a scene manager the loader reads: a `SceneManager` is itself, a `node` goes into a scene of its own
        std::string SceneManagerXml(std::string text) {
            const size_t declaration = text.find("<?xml");
            if (declaration != std::string::npos) {
                text.erase(declaration, text.find("?>", declaration) + 2 - declaration);
            }
            static const std::regex root(R"(<([A-Za-z][A-Za-z0-9_]*))");
            std::string stripped;
            for (size_t at = 0; at < text.size();) {  // the first element that is not a comment: skip the comments, which may mention tags
                const size_t comment = text.find("<!--", at);
                const size_t tag = text.find('<', at);
                if (comment != std::string::npos && comment == tag) {
                    at = text.find("-->", comment) + 3;
                    continue;
                }
                stripped = text.substr(tag);
                break;
            }
            std::smatch match;
            if (std::regex_search(stripped, match, root) && match[1] == "node") {
                return "<SceneManager><scenes><scene type=\"Scene\" name=\"" + std::string(kNodeSceneName) + "\" viewWidth=\"" + std::to_string(static_cast<int>(kDefaultViewWidth)) +
                       "\" viewHeight=\"" + std::to_string(static_cast<int>(kDefaultViewHeight)) + "\"><nodes>" + text + "</nodes></scene></scenes></SceneManager>";
            }
            return text;
        }

        std::vector<std::string> SceneNames(const std::string& xml) {
            std::vector<std::string> names;
            static const std::regex scene(R"re(<scene\b[^>]*?\bname="([^"]*)")re");
            for (std::sregex_iterator it(xml.begin(), xml.end(), scene), end; it != end; ++it) {
                names.push_back((*it)[1]);
            }
            return names;
        }

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        SDL_MouseMotionEvent Motion(float x, float y) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.x = x;
            event.y = y;
            return event;
        }

        /// The actions that went out, as "action=value" lines
        class ActionRecorder : public SubscriptionHolder {
        public:
            ActionRecorder() {
                Subscribe<InputEvents::ActionEvent, &ActionRecorder::OnAction>(this);
            }
            std::vector<std::string> emitted;

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                emitted.push_back(std::string(event.action.CStr()) + "=" + std::string(event.value.CStr()));
            }
        };

        /// What one form of a screen did, by the key "scene|node|element" (the first element of that name in the canvas)
        struct Snapshot {
            std::map<std::string, std::vector<Rect>> rects;                               // one per output size
            std::map<std::string, std::map<std::string, eastl::vector<DrawCall>>> draws;  // state -> the whole scene's draw
            std::map<std::string, std::map<std::string, std::string>> trees;              // state of the tree -> what `ui_tree` says
            std::map<std::string, std::vector<std::string>> clicks;                       // "normal" / "disabled" -> the actions emitted
            std::map<std::string, std::string> tooltips;
            std::set<std::string> issues;
        };

        std::string Text(const eastl::string& text) {
            return std::string(text.c_str(), text.size());
        }

        std::string Where(const std::string& scene, const std::string& node, const std::string& element) {
            return scene + "|" + node + "|" + element;
        }

        /// The first difference between two draws of a scene, empty when they are the same
        std::string DrawDifference(const eastl::vector<DrawCall>& before, const eastl::vector<DrawCall>& after) {
            if (before.size() != after.size()) {
                return "the draws have " + std::to_string(before.size()) + " and " + std::to_string(after.size()) + " calls";
            }
            const auto differs = [](float a, float b) { return std::abs(a - b) > kTolerance; };
            for (size_t i = 0; i < before.size(); ++i) {
                const DrawCall& a = before[i];
                const DrawCall& b = after[i];
                const std::string call = "call " + std::to_string(i) + ": ";
                if (a.kind != b.kind) {
                    return call + "a different kind of draw";
                }
                if (differs(a.rect.x, b.rect.x) || differs(a.rect.y, b.rect.y) || differs(a.rect.w, b.rect.w) || differs(a.rect.h, b.rect.h)) {
                    return call + "the rect is (" + std::to_string(a.rect.x) + ", " + std::to_string(a.rect.y) + ", " + std::to_string(a.rect.w) + " x " + std::to_string(a.rect.h) + ") and (" +
                           std::to_string(b.rect.x) + ", " + std::to_string(b.rect.y) + ", " + std::to_string(b.rect.w) + " x " + std::to_string(b.rect.h) + ")";
                }
                if (a.color.r != b.color.r || a.color.g != b.color.g || a.color.b != b.color.b || a.color.a != b.color.a) {
                    return call + "the colour is (" + std::to_string(a.color.r) + "," + std::to_string(a.color.g) + "," + std::to_string(a.color.b) + "," + std::to_string(a.color.a) + ") and (" +
                           std::to_string(b.color.r) + "," + std::to_string(b.color.g) + "," + std::to_string(b.color.b) + "," + std::to_string(b.color.a) + ")";
                }
                if (a.texture != b.texture) {
                    return call + "another texture";
                }
                if (a.source.has_value() != b.source.has_value() ||
                    (a.source && (differs(a.source->x, b.source->x) || differs(a.source->y, b.source->y) || differs(a.source->w, b.source->w) || differs(a.source->h, b.source->h)))) {
                    return call + "another source rect";
                }
                if (a.blend != b.blend || a.clip.has_value() != b.clip.has_value()) {
                    return call + "another blend or clip";
                }
                if (a.vertices.size() != b.vertices.size() || a.indices != b.indices) {
                    return call + "another geometry";
                }
                for (size_t v = 0; v < a.vertices.size(); ++v) {
                    const GeometryVertex& p = a.vertices[v];
                    const GeometryVertex& q = b.vertices[v];
                    if (differs(p.x, q.x) || differs(p.y, q.y) || differs(p.u, q.u) || differs(p.v, q.v) || p.color.r != q.color.r || p.color.g != q.color.g || p.color.b != q.color.b ||
                        p.color.a != q.color.a) {
                        return call + "vertex " + std::to_string(v) + " differs";
                    }
                }
            }
            return {};
        }

        void WalkElements(UIElement& parent, const std::function<void(UIElement&)>& visit) {
            for (const auto& child : parent.children) {
                visit(*child);
                WalkElements(*child, visit);
            }
        }

        std::string JsonText(const Json& object, eastl::string_view key) {
            const auto value = GetJsonString(object, key);
            return value ? Text(*value) : std::string();
        }

        std::string JsonFlag(const Json& object, eastl::string_view key) {
            return GetJsonBool(object, key).value_or(false) ? "true" : "false";
        }

        /// What `ui_tree` says about an element that a migration must keep: a missing key and an empty or false one are the same answer
        std::string TreeFields(const Json& element) {
            std::string fields;
            for (const char* key : {"text", "action", "value", "tooltip"}) {
                fields += std::string(key) + "=" + JsonText(element, key) + "; ";
            }
            for (const char* key : {"disabled", "selected", "interactive", "visible", "blocksInput"}) {
                fields += std::string(key) + "=" + JsonFlag(element, key) + "; ";
            }
            return fields;
        }

        void CollectTree(const Json& parent, const std::string& canvasKey, std::set<std::string>& seen, std::map<std::string, std::string>& out) {
            const Json* list = FindJsonMember(parent, parent.contains("elements") ? "elements" : "children");
            if (list == nullptr || !list->is_array()) {
                return;
            }
            for (const Json& element : *list) {
                const std::string name = JsonText(element, "name");
                const std::string key = canvasKey + "|" + name;
                if (!name.empty() && seen.insert(key).second) {
                    out[key] = TreeFields(element);
                }
                CollectTree(element, canvasKey, seen, out);
            }
        }

    }  // namespace

    class ContentMigrationButtonTest : public ::testing::TestWithParam<std::string> {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = kLargeWidth;
            _renderer->height = kLargeHeight;
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            RegisterUIControl(_service);
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        /// Loads a scene manager from @p xml and records what it does into @p snapshot
        bool Capture(const std::string& xml, Snapshot& snapshot) {
            XmlDeserializer deserializer;
            if (!deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size()))) {
                ADD_FAILURE() << "the scene does not parse";
                return false;
            }
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            if (deserializer.HasErrors()) {
                ADD_FAILURE() << "the scene has errors (the log says which)";
                return false;
            }
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);
            for (const std::string& scene : SceneNames(xml)) {
                CaptureScene(scene, snapshot);
            }
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            return true;
        }

        struct CanvasRef {
            std::string node;
            IntrusivePtr<UICanvasComponent> canvas;
        };

        std::vector<CanvasRef> CanvasesOf(Scene& scene) const {
            std::vector<CanvasRef> canvases;
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                if (auto canvas = node->GetComponent<UICanvasComponent>()) {
                    canvases.push_back({std::string(node->GetName().CStr()), canvas});
                }
            }
            return canvases;
        }

        void Draw(float width, float height) {
            _renderer->width = width;
            _renderer->height = height;
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        static Vec2 OutputCenter(const UICanvasComponent& canvas, const UIElement& element) {
            const Rect output = canvas.GetLayout().GetResolved().transform.ToOutput(element.rect.x, element.rect.y, element.rect.w, element.rect.h);
            return {output.x + output.w * 0.5f, output.y + output.h * 0.5f};
        }

        static void Park() {
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(kOutside, kOutside));
        }

        void CaptureScene(const std::string& sceneName, Snapshot& snapshot) {
            _manager->SetActiveScene(PoolString::Intern(sceneName.c_str()));
            Scene* scene = _manager->GetSceneByName(PoolString::Intern(sceneName.c_str()));
            ASSERT_NE(scene, nullptr) << sceneName;
            const std::vector<CanvasRef> canvases = CanvasesOf(*scene);

            // Everything is shown: a hidden canvas or element has no rect and nothing to compare
            for (const CanvasRef& ref : canvases) {
                UICanvasComponent::SetVisibleByName(*scene, PoolString::Intern(ref.node.c_str()), true);
                WalkElements(ref.canvas->GetRoot(), [&](UIElement& element) {
                    if (!element.name.Empty() && !element.style.visible) {
                        ref.canvas->SetElementVisible(element.name, true);
                    }
                });
                ref.canvas->Invalidate();
            }

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
            Json request = Json::object();
            request["id"] = ++_nextId;
            request["method"] = "ui_tree";
            Json response;
            _service.Post(DumpJson(request), [&response](eastl::string text) {
                auto parsed = ParseJson(text);
                response = parsed ? std::move(*parsed) : Json();
            });
            _service.Drain();
            const Json* result = FindJsonMember(response, "result");
            const Json* list = result != nullptr ? FindJsonMember(*result, "canvases") : nullptr;
            if (list == nullptr || !list->is_array()) {
                ADD_FAILURE() << "ui_tree answers no canvases in scene " << sceneName;
                return;
            }
            std::map<std::string, std::string> fields;
            for (const Json& canvas : *list) {
                const std::string canvasKey = sceneName + "|" + JsonText(canvas, "node");
                std::set<std::string> seen;
                CollectTree(canvas, canvasKey, seen, fields);
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

        std::vector<std::string> Click(const Vec2& at) {
            ActionRecorder recorder;
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(at.x, at.y));
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true));
            CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at.x, at.y, false));
            return recorder.emitted;
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

        ControlService _service;
        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        int64_t _nextId = 0;
    };

    TEST(ContentMigrationButtonFixtures, TheFixturePairsAreThere) {
        ASSERT_TRUE(std::filesystem::is_directory(FixtureFolder())) << FixtureFolder().string();
        const std::vector<std::string> names = CaseNames();
        for (const char* required : {"mahjong_hints_button", "mahjong_hand_over_action", "mahjong_setup", "match3_menu_hud", "blocker", "text_attributes", "disabled_tooltip", "with_children",
                                     "already_composed", "icon_attributes_without_icon", "long_caption"}) {
            EXPECT_TRUE(std::find(names.begin(), names.end(), required) != names.end()) << "no pair for " << required;
        }
    }

    TEST_P(ContentMigrationButtonTest, TheMigratedButtonsBehaveLikeTheOldOnes) {
        const std::string name = GetParam();
        const std::string before = SceneManagerXml(ReadFile(FixtureFolder() / (name + ".before.xml")));
        const std::string after = SceneManagerXml(ReadFile(FixtureFolder() / (name + ".after.xml")));
        ASSERT_FALSE(before.empty());
        ASSERT_FALSE(after.empty());

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
        for (const auto& [key, states] : old.draws) {
            const auto found = now.draws.find(key);
            ASSERT_NE(found, now.draws.end()) << name << ": " << key << " no longer takes clicks";
            for (const auto& [state, calls] : states) {
                const std::string difference = DrawDifference(calls, found->second.at(state));
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

    INSTANTIATE_TEST_SUITE_P(Fixtures, ContentMigrationButtonTest, ::testing::ValuesIn(CaseNames()));

}  // namespace BECore::Tests
