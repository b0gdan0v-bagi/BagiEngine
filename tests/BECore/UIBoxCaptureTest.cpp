#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElement.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreSDL/SDLRendererBackend.h>
#include <CoreSDL/SDLScreenshot.h>
#include <SDL3_image/SDL_image.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kWindowConfig = "config/SDLWindowConfig.xml";
        constexpr const char* kSavePngVariable = "BE_UI_BOX_PNG";  // set it to a folder to keep the frames of this test as PNG files for a look
        constexpr int kBlendTolerance = 4;

        using Pixel = std::array<int, 3>;

        const Color kBackground{10, 20, 30, 255};
        const Pixel kBackgroundPixel{10, 20, 30};

        // The sample screen in UI units: a panel (rounded 16, a 3 unit white border, a vertical gradient red to blue, a black shadow 12 units down, a glow) and a rounded button
        constexpr Rect kPanel{40.0f, 40.0f, 160.0f, 100.0f};
        constexpr Rect kButton{260.0f, 40.0f, 120.0f, 48.0f};
        constexpr float kPanelRadius = 16.0f;
        constexpr float kButtonRadius = 12.0f;
        constexpr float kBorderWidth = 3.0f;
        constexpr float kShadowOffsetY = 12.0f;
        constexpr float kBlur = 6.0f;
        const Pixel kBorderPixel{255, 255, 255};
        const Pixel kButtonPixel{40, 90, 60};

        constexpr const char* kSampleXml = R"(<root><component><elements>
  <element type="UIPanel" name="Panel" color="200,40,40,255" cornerRadius="16" borderWidth="3" borderColor="255,255,255,255" gradientDirection="Vertical" gradientColor="40,40,200,255"
           shadow="0,12,6,0,#000000C8" glow="0,0,6,0,#FFD70050"/>
  <element type="UIButton" name="Button" color="40,90,60,255" cornerRadius="12"/>
</elements></component></root>)";

        Pixel PixelAtOutput(const PixelBuffer& image, uint32_t x, uint32_t y) {
            const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
            return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2]};
        }

        /// The output pixel that holds the point (@p unitX, @p unitY) of the UI at @p scale
        Pixel PixelAt(const PixelBuffer& image, float unitX, float unitY, float scale) {
            return PixelAtOutput(image, static_cast<uint32_t>(unitX * scale), static_cast<uint32_t>(unitY * scale));
        }

        bool IsNear(const Pixel& actual, const Pixel& expected, int tolerance) {
            for (size_t i = 0; i < actual.size(); ++i) {
                if (std::abs(actual[i] - expected[i]) > tolerance) {
                    return false;
                }
            }
            return true;
        }

        int Sum(const Pixel& pixel) {
            return pixel[0] + pixel[1] + pixel[2];
        }

        /// A headless window with the SDL renderer on it, and the sample screen loaded through the XML path of a screen. A machine without the dummy video driver skips the test.
        class UIBoxCaptureTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
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

                XmlDeserializer deserializer;
                ASSERT_TRUE(deserializer.LoadFromString(kSampleXml));
                canvas = New<UICanvasComponent>();
                ASSERT_TRUE(deserializer.BeginObject("component"));
                canvas->Deserialize(deserializer);
                deserializer.EndObject();
                ASSERT_FALSE(deserializer.HasErrors());
                ASSERT_EQ(canvas->_elements.size(), 2u);
                canvas->_elements[0]->rect = kPanel;
                canvas->_elements[1]->rect = kButton;
            }

            void TearDown() override {
                if (renderer) {
                    renderer->Destroy();
                }
                if (window) {
                    window->Destroy();
                }
            }

            /// Draws the sample screen at @p scale (output pixels per UI unit) and returns the frame
            PixelBuffer DrawSample(float scale) {
                renderer->Clear(kBackground);
                renderer->SetViewTransform({scale, 0.0f, 0.0f});
                for (const auto& element : canvas->_elements) {
                    element->DrawSelf(*renderer);
                }
                renderer->SetViewTransform({});
                std::optional<CaptureResult> captured;
                renderer->RequestCapture([&captured](CaptureResult result) { captured = std::move(result); });
                renderer->Present();
                if (!captured || !captured->has_value()) {
                    ADD_FAILURE() << "the frame was not captured";
                    return {};
                }
                PixelBuffer image = std::move(**captured);
                SaveForALook(image, scale);
                return image;
            }

            /// Writes the frame to `<folder of kSavePngVariable>/<name>_<tag>.png` when the variable is set; never into the repository
            static void SaveForALook(const PixelBuffer& image, float scale) {
                const char* folder = std::getenv(kSavePngVariable);
                if (folder == nullptr || image.rgba.empty()) {
                    return;
                }
                const std::filesystem::path path = std::filesystem::path(folder) / TestTempPath(Format("UIBoxCapture_x{}", static_cast<int>(scale)).c_str()).filename().replace_extension(".png");
                SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(image.width), static_cast<int>(image.height), SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t*>(image.rgba.data()),
                                                             static_cast<int>(image.width) * 4);
                if (surface != nullptr) {
                    IMG_SavePNG(surface, path.string().c_str());
                    SDL_DestroySurface(surface);
                }
            }

            IntrusivePtr<SDLMainWindow> window;
            IntrusivePtr<SDLRendererBackend> renderer;
            IntrusivePtr<UICanvasComponent> canvas;
        };

        class UIBoxCaptureScaleTest : public UIBoxCaptureTest, public ::testing::WithParamInterface<float> {};

    }  // namespace

    TEST_P(UIBoxCaptureScaleTest, ThePanelIsRoundedBorderedGradedAndShadowed) {
        const float scale = GetParam();
        const PixelBuffer image = DrawSample(scale);
        ASSERT_FALSE(image.rgba.empty());
        ASSERT_GT(image.width, static_cast<uint32_t>(400.0f * scale));

        // The very corner of the rect is cut off by the radius: nothing of the panel is there (the glow is out of reach of a point this far from the arc)
        EXPECT_TRUE(IsNear(PixelAt(image, kPanel.x, kPanel.y, scale), kBackgroundPixel, kBlendTolerance));
        // The border on the straight left edge is full strength, the fill inside the border is the gradient
        const float middleY = kPanel.y + kPanel.h * 0.5f;
        EXPECT_TRUE(IsNear(PixelAt(image, kPanel.x + 1.0f, middleY, scale), kBorderPixel, kBlendTolerance));
        // The gradient: red at the top, blue at the bottom (the same column, inside the border)
        const float centerX = kPanel.x + kPanel.w * 0.5f;
        const Pixel top = PixelAt(image, centerX, kPanel.y + kBorderWidth + 4.0f, scale);
        const Pixel bottom = PixelAt(image, centerX, kPanel.y + kPanel.h - kBorderWidth - 4.0f, scale);
        EXPECT_GT(top[0], bottom[0] + 50);
        EXPECT_LT(top[2], bottom[2] - 50);
        // The shadow is darker than the background below the panel, where the glow has faded; beyond the blur there is the background again
        const Pixel shadowed = PixelAt(image, centerX, kPanel.y + kPanel.h + 8.0f, scale);
        EXPECT_LT(Sum(shadowed), Sum(kBackgroundPixel) - 20);
        EXPECT_TRUE(IsNear(PixelAt(image, centerX, kPanel.y + kPanel.h + kShadowOffsetY + 2.0f * kBlur + 8.0f, scale), kBackgroundPixel, kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, kPanel.x - 2.0f * kBlur - 4.0f, middleY, scale), kBackgroundPixel, kBlendTolerance));
    }

    TEST_P(UIBoxCaptureScaleTest, TheRoundedButtonHasAFullEdgeABlendedCurveAndABackgroundCorner) {
        const float scale = GetParam();
        const PixelBuffer image = DrawSample(scale);
        ASSERT_FALSE(image.rgba.empty());

        EXPECT_TRUE(IsNear(PixelAt(image, kButton.x, kButton.y, scale), kBackgroundPixel, kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, kButton.x + 1.0f, kButton.y + kButton.h * 0.5f, scale), kButtonPixel, kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, kButton.x + kButton.w * 0.5f, kButton.y + 1.0f, scale), kButtonPixel, kBlendTolerance));

        // In the square of the top-left corner (the size of the radius) the curve is anti-aliased: some pixels are partly blended (every channel strictly between the background and
        // the fill), and the pixel at the rect's corner is background and the one at the center of the arc is the fill. (The software rasterizer samples a triangle at the pixel's
        // corner, so which pixels of a given diagonal fall on the ramp depends on the scale: the whole square is counted.)
        const auto cornerX = static_cast<uint32_t>(kButton.x * scale);
        const auto cornerY = static_cast<uint32_t>(kButton.y * scale);
        const auto size = static_cast<uint32_t>(kButtonRadius * scale);
        int blendedCount = 0;
        for (uint32_t row = 0; row <= size; ++row) {
            for (uint32_t column = 0; column <= size; ++column) {
                const Pixel pixel = PixelAtOutput(image, cornerX + column, cornerY + row);
                bool partly = true;
                for (size_t channel = 0; channel < pixel.size(); ++channel) {
                    partly = partly && pixel[channel] > kBackgroundPixel[channel] + 2 && pixel[channel] < kButtonPixel[channel] - 2;
                }
                blendedCount += partly ? 1 : 0;
            }
        }
        EXPECT_GE(blendedCount, 2);
        EXPECT_TRUE(IsNear(PixelAtOutput(image, cornerX + size, cornerY + size), kButtonPixel, kBlendTolerance));
    }

    INSTANTIATE_TEST_SUITE_P(Scales, UIBoxCaptureScaleTest, ::testing::Values(1.0f, 2.0f));

}  // namespace BECore::Tests
