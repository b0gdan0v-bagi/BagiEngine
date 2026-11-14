#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreSDL/SDLRendererBackend.h>
#include <CoreSDL/SDLScreenshot.h>
#include <SDL3_image/SDL_image.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The show / hide transitions as pixels of the software renderer: a panel at the start, the half and the end of its run reads the blended values, and two children that overlap inside a faded
     * panel show the documented limit of the group opacity (it is a per-draw multiply, not a composite of the group: the child blends with what is under it, the parent included).
     */

    namespace {

        constexpr const char* kWindowConfig = "config/SDLWindowConfig.xml";
        constexpr const char* kSavePngVariable = "BE_UI_TRANSITION_PNG";  // set it to a folder to keep the frames of this test as PNG files for a look
        constexpr int kBlendTolerance = 4;
        constexpr double kHalf = 0.5;
        constexpr double kStep = 0.125;  // exact in binary and inside the longest frame the clock reports

        using Pixel = Array<int, 3>;

        const Color kBackground{10, 20, 30, 255};
        const Pixel kBackgroundPixel{10, 20, 30};
        const Pixel kPanelPixel{200, 40, 40};
        const Pixel kSpotPixel{40, 40, 200};

        // Panel (a transition of a second, Linear, hidden at load) with a Spot over its left part; the whole canvas has its own transition for the canvas test
        //   Panel (50, 50, 200 x 100); Spot is its child (60, 30, 80 x 40) and sits inside it
        constexpr const char* kSceneXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize" transitionDuration="1" transitionEase="Linear">
        <elements>
          <element type="UIElement" name="Panel" x="50" y="50" width="200" height="100" color="200,40,40,255" visible="false">
            <components><component type="UITransitionComponent" duration="1" ease="Linear"/></components>
            <children>
              <element type="UIElement" name="Spot" x="60" y="30" width="80" height="40" color="40,40,200,255"/>
            </children>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        Pixel PixelAtOutput(const PixelBuffer& image, uint32_t x, uint32_t y) {
            const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
            return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2]};
        }

        bool IsNear(const Pixel& actual, const Pixel& expected, int tolerance) {
            for (size_t i = 0; i < actual.size(); ++i) {
                if (std::abs(actual[i] - expected[i]) > tolerance) {
                    return false;
                }
            }
            return true;
        }

        /// What @p fraction of @p color over @p background gives
        Pixel Mix(const Pixel& background, const Pixel& color, double fraction) {
            Pixel result{};
            for (size_t i = 0; i < result.size(); ++i) {
                result[i] = static_cast<int>(std::lround(background[i] + fraction * (color[i] - background[i])));
            }
            return result;
        }

        String Describe(const Pixel& pixel) {
            return "(" + ToString(pixel[0]) + ", " + ToString(pixel[1]) + ", " + ToString(pixel[2]) + ")";
        }

    }  // namespace

    /// A headless window with the SDL renderer on it and the scene above. A machine without the dummy video driver skips the test.
    class UITransitionCaptureTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetTime().Reset();
            window = New<SDLMainWindow>();
            window->SetHeadless(true);
            if (!window->Initialize(kWindowConfig)) {
                window.Reset();
                GTEST_SKIP() << "no headless window: " << SDL_GetError();
            }
            renderer = New<SDLRendererBackend>();
            if (!renderer->Initialize(*window)) {
                renderer.Reset();
                GTEST_SKIP() << "no software renderer: " << SDL_GetError();
            }
            CoreManager::GetRendererManager().SetRenderer(renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kSceneXml));
            manager = New<SceneManager>();
            manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            manager->Initialize();
            canvas = manager->GetActiveScene()->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(canvas);
        }

        void TearDown() override {
            canvas.Reset();
            manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            if (renderer) {
                renderer->Destroy();
            }
            if (window) {
                window->Destroy();
            }
            CoreManager::GetTime().Reset();
        }

        /// Draws one frame on the background and returns what was on the back buffer; @p tag names the PNG when the variable asks for one
        PixelBuffer Render(const char* tag) {
            renderer->Clear(kBackground);
            manager->DrawAll();
            Optional<CaptureResult> captured;
            renderer->RequestCapture([&captured](CaptureResult result) { captured = std::move(result); });
            renderer->Present();
            if (!captured || !captured->has_value()) {
                ADD_FAILURE() << "the frame was not captured";
                return {};
            }
            PixelBuffer image = std::move(**captured);
            SaveForALook(image, tag);
            return image;
        }

        /// The pixel at the centre of the laid-out rect of an element
        Pixel CentreOf(const PixelBuffer& image, const char* name) const {
            const UIElement* element = canvas->GetRoot().FindByName(PoolString::Intern(name));
            if (element == nullptr || image.rgba.empty()) {
                ADD_FAILURE() << name;
                return {};
            }
            const Rect output = canvas->GetLayout().GetResolved().transform.ToOutput(element->rect.x, element->rect.y, element->rect.w, element->rect.h);
            return PixelAtOutput(image, static_cast<uint32_t>(output.x + output.w * 0.5f), static_cast<uint32_t>(output.y + output.h * 0.5f));
        }

        /// A point of the panel that Spot does not cover: a tenth of its width from the left edge, half way down
        Pixel PanelOnly(const PixelBuffer& image) const {
            const UIElement* panel = canvas->GetRoot().FindByName(PoolString::Intern("Panel"));
            const Rect output = canvas->GetLayout().GetResolved().transform.ToOutput(panel->rect.x, panel->rect.y, panel->rect.w, panel->rect.h);
            return PixelAtOutput(image, static_cast<uint32_t>(output.x + output.w * 0.1f), static_cast<uint32_t>(output.y + output.h * 0.5f));
        }

        /// Writes the frame to `<folder of kSavePngVariable>/<name>.png` when the variable is set; never into the repository
        static void SaveForALook(const PixelBuffer& image, const char* tag) {
            const char* folder = std::getenv(kSavePngVariable);
            if (folder == nullptr || image.rgba.empty()) {
                return;
            }
            const std::filesystem::path path = std::filesystem::path(folder) / TestTempPath(Format("UITransitionCapture_{}", tag).c_str()).filename().replace_extension(".png");
            SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(image.width), static_cast<int>(image.height), SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t*>(image.rgba.data()),
                                                         static_cast<int>(image.width) * 4);
            if (surface != nullptr) {
                IMG_SavePNG(surface, path.string().c_str());
                SDL_DestroySurface(surface);
            }
        }

        /// Lets @p seconds of game time pass without a frame: the clock takes at most a quarter of a second at a time (`Time::kMaxFrameTime`), so a longer wait is several steps
        static void Pass(double seconds) {
            for (double passed = 0.0; passed < seconds - 1e-9; passed += kStep) {
                CoreManager::GetTime().Advance(kStep);
            }
        }

        void ShowPanel(bool visible) {
            ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern("Panel"), visible));
        }

        IntrusivePtr<SDLMainWindow> window;
        IntrusivePtr<SDLRendererBackend> renderer;
        IntrusivePtrAtomic<SceneManager> manager;
        IntrusivePtr<UICanvasComponent> canvas;
    };

    TEST_F(UITransitionCaptureTest, APanelThatIsShownReadsTheBackgroundThenTheBlendThenItsOwnColour) {
        PixelBuffer image = Render("hidden");
        ASSERT_FALSE(image.rgba.empty());
        ShowPanel(true);
        image = Render("shown_0");
        EXPECT_TRUE(IsNear(PanelOnly(image), kBackgroundPixel, kBlendTolerance)) << Describe(PanelOnly(image));

        Pass(kHalf);
        image = Render("shown_half");
        const Pixel half = Mix(kBackgroundPixel, kPanelPixel, kHalf);
        EXPECT_TRUE(IsNear(PanelOnly(image), half, kBlendTolerance)) << Describe(PanelOnly(image)) << " expected " << Describe(half);

        Pass(kHalf);
        image = Render("shown_1");
        EXPECT_TRUE(IsNear(PanelOnly(image), kPanelPixel, kBlendTolerance)) << Describe(PanelOnly(image));
        EXPECT_TRUE(IsNear(CentreOf(image, "Spot"), kSpotPixel, kBlendTolerance)) << Describe(CentreOf(image, "Spot"));
    }

    TEST_F(UITransitionCaptureTest, TwoOverlappingChildrenBlendThroughEachOtherMidFadeAsDocumented) {
        Render("hidden");
        ShowPanel(true);
        Render("shown_0");
        Pass(kHalf);
        const PixelBuffer image = Render("overlap_half");
        ASSERT_FALSE(image.rgba.empty());

        // The limit is pinned, not hidden: the opacity is multiplied into every draw, so at a half the Spot is half of its colour over the half panel under it
        const Pixel halfPanel = Mix(kBackgroundPixel, kPanelPixel, kHalf);
        const Pixel perDraw = Mix(halfPanel, kSpotPixel, kHalf);
        const Pixel composite = Mix(kBackgroundPixel, kSpotPixel, kHalf);  // what a composite of the group would give: the panel does not show through
        const Pixel spot = CentreOf(image, "Spot");
        EXPECT_TRUE(IsNear(spot, perDraw, kBlendTolerance)) << Describe(spot) << " expected " << Describe(perDraw);
        EXPECT_GT(std::abs(spot[0] - composite[0]), 30) << "the panel's red shows through the Spot (a composite would give " << Describe(composite) << ")";
    }

    TEST_F(UITransitionCaptureTest, ACanvasThatIsHiddenFadesFromWhatItShowsToTheBackground) {
        ShowPanel(true);
        Pass(1.0);
        PixelBuffer image = Render("canvas_shown");
        ASSERT_TRUE(IsNear(PanelOnly(image), kPanelPixel, kBlendTolerance));
        canvas->_visible = false;
        image = Render("canvas_hide_0");
        EXPECT_TRUE(IsNear(PanelOnly(image), kPanelPixel, kBlendTolerance)) << "the first frame of the hide is the whole canvas";

        Pass(kHalf);
        image = Render("canvas_hide_half");
        const Pixel half = Mix(kBackgroundPixel, kPanelPixel, kHalf);
        EXPECT_TRUE(IsNear(PanelOnly(image), half, kBlendTolerance)) << Describe(PanelOnly(image)) << " expected " << Describe(half);

        Pass(kHalf);
        image = Render("canvas_hide_1");
        EXPECT_TRUE(IsNear(PanelOnly(image), kBackgroundPixel, kBlendTolerance)) << Describe(PanelOnly(image));
    }

}  // namespace BECore::Tests
