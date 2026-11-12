#pragma once

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIControl.h>
#include <BECore/UI/UIImageComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <set>

namespace BECore::Tests::MigrationTwin {

    /*
     * What the twin tests of the migration steps share (`ContentMigrationButtonTest.cpp` for step 1, `ContentMigrationGroupTest.cpp` for step 2): the fixture folders of
     * `tools/migrate/tests/fixtures/<step>/`, a content file read as a scene manager, the draw comparison, and a fixture class that loads one form of a screen into canvases on a
     * `RecordingRenderer`. The two forms of a case are loaded one after the other, never together: both would register their canvases with the one `UIInput` and answer the same pointer.
     */

    inline constexpr float kLargeWidth = 1280.0f;
    inline constexpr float kLargeHeight = 720.0f;
    inline constexpr float kSmallWidth = 600.0f;  // narrow and tall: the portrait variants apply and a stack of buttons is squeezed
    inline constexpr float kSmallHeight = 900.0f;
    inline constexpr float kDefaultViewWidth = 1280.0f;  // the view of the scene a node file is put into
    inline constexpr float kDefaultViewHeight = 720.0f;
    inline constexpr float kTolerance = 1e-3f;
    inline constexpr float kIconTolerance = 1.0f;  // the quad of an icon or of its caption, in output pixels
    inline constexpr int kIconPixels = 16;
    inline constexpr float kTouchSize = 48.0f;
    inline constexpr float kOutside = -100.0f;  // a point on no element, to park the pointer
    inline constexpr const char* kNodeSceneName = "Main";

    inline std::filesystem::path FixtureFolder(const char* step) {
        return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "tools" / "migrate" / "tests" / "fixtures" / step;
    }

    /// The names of the cases of a fixture folder: every `<case>.before.xml` that has an `<case>.after.xml`
    inline std::vector<std::string> CaseNames(const std::filesystem::path& folder) {
        std::vector<std::string> names;
        const std::string suffix = ".before.xml";
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
            const std::string file = entry.path().filename().string();
            if (file.size() > suffix.size() && file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0) {
                const std::string name = file.substr(0, file.size() - suffix.size());
                if (std::filesystem::exists(folder / (name + ".after.xml"))) {
                    names.push_back(name);
                }
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    inline std::string ReadFile(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }

    /// A content file as a scene manager the loader reads: a `SceneManager` is itself, a `node` goes into a scene of its own
    inline std::string SceneManagerXml(std::string text) {
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
            return "<SceneManager><scenes><scene type=\"Scene\" name=\"" + std::string(kNodeSceneName) + "\" viewWidth=\"" + std::to_string(static_cast<int>(kDefaultViewWidth)) + "\" viewHeight=\"" +
                   std::to_string(static_cast<int>(kDefaultViewHeight)) + "\"><nodes>" + text + "</nodes></scene></scenes></SceneManager>";
        }
        return text;
    }

    inline std::vector<std::string> SceneNames(const std::string& xml) {
        std::vector<std::string> names;
        static const std::regex scene(R"re(<scene\b[^>]*?\bname="([^"]*)")re");
        for (std::sregex_iterator it(xml.begin(), xml.end(), scene), end; it != end; ++it) {
            names.push_back((*it)[1]);
        }
        return names;
    }

    inline SDL_MouseButtonEvent Button(float x, float y, bool down) {
        SDL_MouseButtonEvent event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button = SDL_BUTTON_LEFT;
        event.down = down;
        event.x = x;
        event.y = y;
        return event;
    }

    inline SDL_MouseMotionEvent Motion(float x, float y) {
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

    inline std::string Text(const eastl::string& text) {
        return std::string(text.c_str(), text.size());
    }

    inline std::string Where(const std::string& scene, const std::string& node, const std::string& element) {
        return scene + "|" + node + "|" + element;
    }

    /// The first difference between two draws of a scene, empty when they are the same
    inline std::string DrawDifference(const eastl::vector<DrawCall>& before, const eastl::vector<DrawCall>& after, float tolerance) {
        if (before.size() != after.size()) {
            return "the draws have " + std::to_string(before.size()) + " and " + std::to_string(after.size()) + " calls";
        }
        const auto differs = [tolerance](float a, float b) { return std::abs(a - b) > tolerance; };
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

    inline void WalkElements(UIElement& parent, const std::function<void(UIElement&)>& visit) {
        for (const auto& child : parent.children) {
            visit(*child);
            WalkElements(*child, visit);
        }
    }

    inline std::string JsonText(const Json& object, eastl::string_view key) {
        const auto value = GetJsonString(object, key);
        return value ? Text(*value) : std::string();
    }

    inline std::string JsonFlag(const Json& object, eastl::string_view key) {
        return GetJsonBool(object, key).value_or(false) ? "true" : "false";
    }

    /// The named elements of a `ui_tree` canvas (or of an element below it) as "canvasKey|name" -> @p fieldsOf of its entry; the first element of a name wins
    inline void CollectTree(const Json& parent, const std::string& canvasKey, std::set<std::string>& seen, std::map<std::string, std::string>& out,
                            const std::function<std::string(const Json&)>& fieldsOf) {
        const Json* list = FindJsonMember(parent, parent.contains("elements") ? "elements" : "children");
        if (list == nullptr || !list->is_array()) {
            return;
        }
        for (const Json& element : *list) {
            const std::string name = JsonText(element, "name");
            const std::string key = canvasKey + "|" + name;
            if (!name.empty() && seen.insert(key).second) {
                out[key] = fieldsOf(element);
            }
            CollectTree(element, canvasKey, seen, out, fieldsOf);
        }
    }

    /// A screen of one form loaded into canvases on a `RecordingRenderer`; a test derives from it and looks at what the screen draws and answers
    class Fixture : public ::testing::TestWithParam<std::string> {
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
            const eastl::vector<uint8_t> pixels(static_cast<size_t>(kIconPixels) * kIconPixels * 4, 200);
            _icon = _renderer->CreateTexture(pixels.data(), kIconPixels, kIconPixels);
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            _icon.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        /// Loads a scene manager from @p xml, makes it the active one in the services and gives the icons their texture
        bool Load(const std::string& xml) {
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
            GiveTheIconsATexture(xml);
            return true;
        }

        void Unload() {
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
        }

        /// The sprite of every icon, whatever its name, is the texture of the test: the icon quad is then in the draws of both forms
        void GiveTheIconsATexture(const std::string& xml) {
            for (const std::string& sceneName : SceneNames(xml)) {
                Scene* scene = _manager->GetSceneByName(PoolString::Intern(sceneName.c_str()));
                if (scene == nullptr) {
                    continue;
                }
                for (const auto& node : scene->GetRootNode().GetChildren()) {
                    const auto canvas = node->GetComponent<UICanvasComponent>();
                    if (!canvas) {
                        continue;
                    }
                    WalkElements(canvas->GetRoot(), [this](UIElement& element) {
                        if (auto* button = element.Cast<UIButton>(); button != nullptr && !button->icon.Empty()) {
                            button->GetIconSprite().SetTexture(_icon);
                        }
                        if (auto* image = element.GetComponent<UIImageComponent>(); image != nullptr && !image->sprite.Empty()) {
                            image->GetSprite().SetTexture(_icon);
                        }
                    });
                }
            }
        }

        struct CanvasRef {
            std::string node;
            IntrusivePtr<UICanvasComponent> canvas;
        };

        static std::vector<CanvasRef> CanvasesOf(Scene& scene) {
            std::vector<CanvasRef> canvases;
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                if (auto canvas = node->GetComponent<UICanvasComponent>()) {
                    canvases.push_back({std::string(node->GetName().CStr()), canvas});
                }
            }
            return canvases;
        }

        /// Everything is shown: a hidden canvas or element has no rect and nothing to compare
        static void ShowEverything(Scene& scene, const std::vector<CanvasRef>& canvases) {
            for (const CanvasRef& ref : canvases) {
                UICanvasComponent::SetVisibleByName(scene, PoolString::Intern(ref.node.c_str()), true);
                WalkElements(ref.canvas->GetRoot(), [&](UIElement& element) {
                    if (!element.name.Empty() && !element.style.visible) {
                        ref.canvas->SetElementVisible(element.name, true);
                    }
                });
                ref.canvas->Invalidate();
            }
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

        /// A press and a release at @p at; the actions that went out
        static std::vector<std::string> Click(const Vec2& at) {
            ActionRecorder recorder;
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(at.x, at.y));
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true));
            CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at.x, at.y, false));
            return recorder.emitted;
        }

        /// The `canvases` array of the answer of `ui_tree`; empty (and a failure) when it answers none
        Json CallTree(const std::string& sceneName) {
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
                return Json::array();
            }
            return *list;
        }

        ControlService _service;
        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtr<ITexture> _icon;
        IntrusivePtrAtomic<SceneManager> _manager;
        int64_t _nextId = 0;
    };

}  // namespace BECore::Tests::MigrationTwin
