#include "RmlUiScreens.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIScreenContent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <Events/InputEvents.h>
#include <RmlUi/Core.h>
#include <RmlUiHost/RmlUiDocumentComponent.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace RmlUiScreens;

    namespace {

        /// The border box of an element in output pixels
        Rect BoxOf(const RmlUiDocumentComponent& doc, const char* id) {
            Rml::Element* element = doc.GetDocument()->GetElementById(id);
            if (!element) {
                ADD_FAILURE() << "no element '" << id << "'";
                return {};
            }
            const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
            return {doc.GetOriginX() + offset.x, doc.GetOriginY() + offset.y, size.x, size.y};
        }

        struct RunReport {
            size_t cases = 0;
            size_t rectMismatches = 0;
            size_t outside = 0;
            size_t tooSmall = 0;
            size_t overflowing = 0;
            eastl::vector<eastl::string> examples;

            void Note(eastl::string text) {
                if (examples.size() < 8) {
                    examples.push_back(std::move(text));
                }
            }

            [[nodiscard]] size_t Problems() const {
                return rectMismatches + outside + tooSmall + overflowing;
            }

            [[nodiscard]] eastl::string Describe() const {
                eastl::string text =
                    Format("{} cases: {} rects differ from the XML, {} outside the content, {} under the touch size, {} overflowing\n", cases, rectMismatches, outside, tooSmall, overflowing);
                for (const eastl::string& example : examples) {
                    text += Format("  {}\n", example);
                }
                return text;
            }
        };

    }  // namespace

    class RmlUiScreensTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetUIEnvironment().SetSafeAreaOverride(std::nullopt);
            CoreManager::GetUIVars().Clear();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        /// Loads the scene of a screen with the RML document in it
        IntrusivePtr<RmlUiDocumentComponent> Load(const Screen& screen) {
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(SceneXml(screen)));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _manager->SetActiveScene("S"_intern);
            auto doc = _manager->GetSceneByName("S"_intern)->GetRootNode().GetChildren()[0]->GetComponent<RmlUiDocumentComponent>();
            EXPECT_TRUE(doc);
            EXPECT_TRUE(doc && doc->SetMarkup(eastl::string(screen.rml)));
            return doc;
        }

        static void SetVars() {
            UIVars& vars = CoreManager::GetUIVars();
            vars.Set("score"_intern, 1280);
            vars.Set("seed"_intern, 1337);
            vars.Set("turn"_intern, "White to move");
            vars.Set("status"_intern, "Check");
        }

        /// Lays the screen out in RmlUi and in the engine's own tree at every case of the size matrix and compares the rects; also checks, on RmlUi's boxes, what
        /// UILayoutValidator checks of the XML screen: an element outside the content, a button under the touch size, a text that does not fit
        RunReport RunMatrix(const Screen& screen, RmlUiDocumentComponent& doc) {
            RunReport report;
            const UIScreenContent shipped = ReadXml(screen.xml);
            for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
                ++report.cases;
                CoreManager::GetUIEnvironment().SetSafeAreaOverride(matrixCase.context.safeArea);
                _renderer->width = matrixCase.context.outputWidth;
                _renderer->height = matrixCase.context.outputHeight;
                _renderer->ClearCalls();
                _manager->DrawAll();

                // The engine's own tree, given the same context
                UICanvasLayout layout;
                layout.root->children = shipped.elements;
                layout.variants = shipped.variants;
                layout.params.space = screen.space;
                layout.params.scaleMode = CanvasScaleMode::ScaleWithScreen;
                layout.params.referenceWidth = 1280.0f;
                layout.params.referenceHeight = 720.0f;
                layout.params.match = 0.5f;
                layout.params.minScale = screen.minScale;
                layout.params.useSafeArea = true;
                UILayoutContext context = CoreManager::GetUIEnvironment().MakeContext(nullptr, _renderer->width, _renderer->height);
                context.sceneViewWidth = screen.space == CanvasSpace::SceneView ? screen.viewWidth : 0.0f;
                context.sceneViewHeight = screen.space == CanvasSpace::SceneView ? screen.viewHeight : 0.0f;
                layout.Update(context);
                const ViewTransform transform = layout.GetResolved().transform;
                const Rect bounds = layout.GetResolved().contentBounds;
                const Rect area = transform.ToOutput(bounds.x, bounds.y, bounds.w, bounds.h);

                for (const char* id : screen.ids) {
                    const UIElement* expected = layout.root->FindByName(PoolString::Intern(id));
                    if (!expected) {
                        ADD_FAILURE() << "the XML screen has no '" << id << "'";
                        continue;
                    }
                    const Rect rml = BoxOf(doc, id);
                    const Rect want = transform.ToOutput(expected->rect.x, expected->rect.y, expected->rect.w, expected->rect.h);
                    if (std::abs(want.x - rml.x) > kRectTolerance || std::abs(want.y - rml.y) > kRectTolerance || std::abs(want.w - rml.w) > kRectTolerance ||
                        std::abs(want.h - rml.h) > kRectTolerance) {
                        ++report.rectMismatches;
                        report.Note(Format("{} {}: XML ({:.1f}, {:.1f}, {:.1f} x {:.1f}) RmlUi ({:.1f}, {:.1f}, {:.1f} x {:.1f})", matrixCase.label, id, want.x, want.y, want.w, want.h, rml.x, rml.y,
                                           rml.w, rml.h));
                    }
                    if (rml.x < area.x - kRectTolerance || rml.y < area.y - kRectTolerance || rml.x + rml.w > area.x + area.w + kRectTolerance || rml.y + rml.h > area.y + area.h + kRectTolerance) {
                        ++report.outside;
                        report.Note(Format("{} {}: outside the content", matrixCase.label, id));
                    }
                    Rml::Element* element = doc.GetDocument()->GetElementById(id);
                    if (element && (element->GetScrollWidth() > element->GetClientWidth() + 1.0f || element->GetScrollHeight() > element->GetClientHeight() + 1.0f)) {
                        ++report.overflowing;
                        report.Note(Format("{} {}: the content does not fit", matrixCase.label, id));
                    }
                }
                for (const char* id : screen.buttons) {
                    const Rect rml = BoxOf(doc, id);
                    if (std::min(rml.w, rml.h) / context.pixelDensity < kMinTouchSize - kRectTolerance) {
                        ++report.tooSmall;
                        report.Note(Format("{} {}: {:.1f} x {:.1f} is under the touch size", matrixCase.label, id, rml.w, rml.h));
                    }
                }
            }
            return report;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(RmlUiScreensTest, TheMatch3MenuLaysOutLikeTheShippedXmlOnTheMatrix) {
        const Screen& screen = kScreens[0];
        auto doc = Load(screen);
        SetVars();

        const RunReport report = RunMatrix(screen, *doc);

        EXPECT_EQ(report.Problems(), 0u) << report.Describe().c_str();
        EXPECT_GE(report.cases, 100u);
    }

    TEST_F(RmlUiScreensTest, TheMatch3HudLaysOutLikeTheShippedXmlOnTheMatrix) {
        const Screen& screen = kScreens[1];
        auto doc = Load(screen);
        SetVars();

        const RunReport report = RunMatrix(screen, *doc);

        EXPECT_EQ(report.Problems(), 0u) << report.Describe().c_str();
    }

    TEST_F(RmlUiScreensTest, TheChessHudLaysOutLikeTheShippedXmlOnTheMatrix) {
        const Screen& screen = kScreens[2];
        auto doc = Load(screen);
        SetVars();

        const RunReport report = RunMatrix(screen, *doc);

        EXPECT_EQ(report.Problems(), 0u) << report.Describe().c_str();
    }

    TEST_F(RmlUiScreensTest, ABrokenScreenIsCaught) {
        // The menu with a Play button of 20 units, a Title that is 40 units too tall and a Play text far too big for its button: the checks must see all three
        std::string markup = kMenuRml;
        markup.replace(markup.find("button { height: 56dp; font-size: 28dp;"), std::string("button { height: 56dp; font-size: 28dp;").size(),
                       "button { height: 56dp; font-size: 90dp; white-space: nowrap;");
        markup.replace(markup.find("button { height: 56dp;"), std::string("button { height: 56dp;").size(), "button { height: 20dp;");  // after the font change above: the text before it is intact
        markup.replace(markup.find("#Title { height: 88dp;"), std::string("#Title { height: 88dp;").size(), "#Title { height: 128dp;");
        Screen broken = kScreens[0];
        broken.rml = markup.c_str();
        auto doc = Load(broken);
        SetVars();

        const RunReport report = RunMatrix(broken, *doc);

        EXPECT_GT(report.rectMismatches, 0u);
        EXPECT_GT(report.tooSmall, 0u);
        EXPECT_GT(report.overflowing, 0u);
    }

    TEST_F(RmlUiScreensTest, TheButtonsOfTheMenuTakeClicksAndTheRestLetsThemThrough) {
        const Screen& screen = kScreens[0];
        auto doc = Load(screen);
        _renderer->width = 1280.0f;
        _renderer->height = 720.0f;
        _manager->DrawAll();
        struct Actions : SubscriptionHolder {
            Actions() {
                Subscribe<InputEvents::ActionEvent, &Actions::On>(this);
            }
            void On(const InputEvents::ActionEvent& event) {
                seen.push_back(event.action);
            }
            eastl::vector<PoolString> seen;
        } actions;
        const Rect play = BoxOf(*doc, "Play");
        const Rect title = BoxOf(*doc, "Title");
        UIInput& input = CoreManager::GetUIInput();

        SDL_MouseButtonEvent event{};
        event.button = SDL_BUTTON_LEFT;
        event.x = play.x + play.w * 0.5f;
        event.y = play.y + play.h * 0.5f;
        EXPECT_TRUE(input.DispatchMouseButtonDown(event));
        EXPECT_TRUE(input.DispatchMouseButtonUp(event));
        ASSERT_EQ(actions.seen.size(), 1u);
        EXPECT_EQ(actions.seen[0], "Match3Play"_intern);

        // The title is text, not a control; the empty corner of the screen belongs to the game
        event.x = title.x + 4.0f;
        event.y = title.y + 4.0f;
        EXPECT_FALSE(input.DispatchMouseButtonDown(event)) << "the title has no box that takes clicks (the body and its columns have pointer-events: none)";
        input.DispatchMouseButtonUp(event);
        event.x = 4.0f;
        event.y = 4.0f;
        EXPECT_FALSE(input.DispatchMouseButtonDown(event));
        input.DispatchMouseButtonUp(event);
        EXPECT_EQ(actions.seen.size(), 1u);
    }

}  // namespace BECore::Tests
