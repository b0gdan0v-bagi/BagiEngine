#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreSDL/SDLRendererBackend.h>
#include <CoreSDL/SDLScreenshot.h>
#include <Events/RenderEvents.h>
#include <SDL3_image/SDL_image.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kWindowConfig = "config/SDLWindowConfig.xml";
        constexpr StringView kPngBase64Prefix = "iVBORw0KGgo";  // the 8 bytes of a PNG signature in base64

        using Pixel = Array<uint8_t, 4>;

        const Color kBackground{10, 20, 30, 255};
        const Color kRed{255, 0, 0, 255};

        Pixel PixelAt(const PixelBuffer& image, uint32_t x, uint32_t y) {
            const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
            return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2], image.rgba[offset + 3]};
        }

        Pixel AsPixel(const Color& color) {
            return {color.r, color.g, color.b, color.a};
        }

        PixelBuffer MakeImage(uint32_t width, uint32_t height, const Color& fill) {
            PixelBuffer image;
            image.width = width;
            image.height = height;
            image.rgba.resize(static_cast<size_t>(width) * height * 4);
            for (size_t i = 0; i < image.rgba.size(); i += 4) {
                image.rgba[i] = fill.r;
                image.rgba[i + 1] = fill.g;
                image.rgba[i + 2] = fill.b;
                image.rgba[i + 3] = fill.a;
            }
            return image;
        }

        constexpr uint32_t kQuadrantTextureSize = 8;  // texels per side of the test texture of MakeQuadrants()
        constexpr uint32_t kQuadrantBlock = kQuadrantTextureSize / 2;
        constexpr size_t kChannelCount = 4;

        /// A texture image of four blocks of 4 by 4 texels, one color each: top-left, top-right, bottom-left, bottom-right. Asymmetric on both axes, so a turn and a flip show.
        PixelBuffer MakeQuadrants(const Array<Color, 4>& colors) {
            PixelBuffer image;
            image.width = kQuadrantTextureSize;
            image.height = kQuadrantTextureSize;
            image.rgba.resize(static_cast<size_t>(kQuadrantTextureSize) * kQuadrantTextureSize * kChannelCount);
            for (uint32_t row = 0; row < kQuadrantTextureSize; ++row) {
                for (uint32_t column = 0; column < kQuadrantTextureSize; ++column) {
                    const Color& color = colors[(row / kQuadrantBlock) * 2 + column / kQuadrantBlock];
                    const size_t offset = (static_cast<size_t>(row) * kQuadrantTextureSize + column) * kChannelCount;
                    image.rgba[offset] = color.r;
                    image.rgba[offset + 1] = color.g;
                    image.rgba[offset + 2] = color.b;
                    image.rgba[offset + 3] = color.a;
                }
            }
            return image;
        }

        /// The size that the IHDR chunk of a PNG file gives (big-endian, at bytes 16 to 23)
        Pair<uint32_t, uint32_t> PngSize(const Vector<uint8_t>& png) {
            const auto read = [&png](size_t at) {
                return (static_cast<uint32_t>(png[at]) << 24) | (static_cast<uint32_t>(png[at + 1]) << 16) | (static_cast<uint32_t>(png[at + 2]) << 8) | png[at + 3];
            };
            return {read(16), read(20)};
        }

        /// The pixels of a PNG file in memory, as the engine's RGBA layout; empty when SDL cannot read it
        PixelBuffer DecodePng(const Vector<uint8_t>& png) {
            PixelBuffer image;
            SDL_Surface* loaded = IMG_Load_IO(SDL_IOFromConstMem(png.data(), png.size()), true);
            if (loaded == nullptr) {
                return image;
            }
            SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(loaded);
            if (rgba == nullptr) {
                return image;
            }
            image.width = static_cast<uint32_t>(rgba->w);
            image.height = static_cast<uint32_t>(rgba->h);
            const size_t rowBytes = static_cast<size_t>(rgba->w) * 4;
            image.rgba.resize(rowBytes * static_cast<size_t>(rgba->h));
            for (int row = 0; row < rgba->h; ++row) {
                const auto* source = static_cast<const uint8_t*>(rgba->pixels) + static_cast<size_t>(row) * static_cast<size_t>(rgba->pitch);
                std::copy_n(source, rowBytes, image.rgba.begin() + static_cast<ptrdiff_t>(static_cast<size_t>(row) * rowBytes));
            }
            SDL_DestroySurface(rgba);
            return image;
        }

        /// A renderer that draws nothing and keeps the default RequestCapture, like a backend that cannot read its output back
        class BlindRenderer : public RecordingRenderer {
        public:
            BlindRenderer() {
                width = 0.0f;
                height = 0.0f;
            }
        };

        /// A headless window with the SDL renderer on it. A machine without the dummy video driver skips the test.
        class SDLCaptureTest : public ::testing::Test {
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
            }

            void TearDown() override {
                if (renderer) {
                    renderer->Destroy();
                }
                if (window) {
                    window->Destroy();
                }
            }

            /// Draws the background and a red rectangle at (100, 50) of 200 by 100
            void DrawFrame() {
                renderer->Clear(kBackground);
                renderer->DrawFilledRect(100.0f, 50.0f, 200.0f, 100.0f, kRed);
            }

            /// Presents the frame and returns what was on the back buffer
            PixelBuffer PresentAndRead() {
                Optional<CaptureResult> captured;
                renderer->RequestCapture([&captured](CaptureResult result) { captured = std::move(result); });
                renderer->Present();
                if (!captured || !captured->has_value()) {
                    ADD_FAILURE() << "the frame was not captured";
                    return {};
                }
                return std::move(**captured);
            }

            /// A texture of one color, made through the renderer
            IntrusivePtr<ITexture> MakeSolidTexture(uint32_t width, uint32_t height, const Color& color) {
                const PixelBuffer image = MakeImage(width, height, color);
                return renderer->CreateTexture(image.rgba.data(), width, height);
            }

            IntrusivePtr<ITexture> MakeTexture(const PixelBuffer& image) {
                return renderer->CreateTexture(image.rgba.data(), image.width, image.height);
            }

            IntrusivePtr<SDLMainWindow> window;
            IntrusivePtr<SDLRendererBackend> renderer;
        };

        /// True when every channel is within @p tolerance (a software blend rounds differently from the exact value)
        bool IsNear(const Pixel& actual, const Pixel& expected, int tolerance) {
            for (size_t i = 0; i < actual.size(); ++i) {
                if (std::abs(static_cast<int>(actual[i]) - static_cast<int>(expected[i])) > tolerance) {
                    return false;
                }
            }
            return true;
        }

        constexpr int kBlendTolerance = 4;

        String ErrorCode(const Json& response) {
            const Json* error = FindJsonMember(response, "error");
            const auto code = error != nullptr ? GetJsonString(*error, "code") : std::nullopt;
            return code ? String(code->c_str(), code->size()) : String();
        }

        /// A ControlService of its own with the `screenshot` method, answering from the window and renderer of SDLCaptureTest
        class ScreenshotControlTest : public SDLCaptureTest {
        protected:
            void SetUp() override {
                SDLCaptureTest::SetUp();
                if (IsSkipped()) {
                    return;
                }
                previousRenderer = CoreManager::GetRenderer();
                CoreManager::GetRendererManager().SetRenderer(renderer);
                RegisterScreenshotControl(service);
            }

            void TearDown() override {
                CoreManager::GetRendererManager().SetRenderer(previousRenderer);
                SDLCaptureTest::TearDown();
            }

            void Post(const char* method, const Json& params = Json()) {
                Json request = Json::object();
                request["id"] = ++lastId;
                request["method"] = method;
                if (!params.is_null()) {
                    request["params"] = params;
                }
                service.Post(DumpJson(request), [this](String text) {
                    auto parsed = ParseJson(text);
                    responses.push_back(parsed ? std::move(*parsed) : Json());
                });
            }

            const Json* Result(size_t index) const {
                return index < responses.size() ? FindJsonMember(responses[index], "result") : nullptr;
            }

            ControlService service;
            Vector<Json> responses;
            IntrusivePtr<IRenderer> previousRenderer;
            int64_t lastId = 0;
        };

    }  // namespace

    TEST_F(SDLCaptureTest, TheWindowIsHeadlessAndHasNoGpuSurface) {
        EXPECT_TRUE(window->IsHeadless());
        EXPECT_EQ(String(SDL_GetCurrentVideoDriver()), "dummy");
        EXPECT_GT(renderer->GetOutputWidth(), 0.0f);
        EXPECT_GT(renderer->GetOutputHeight(), 0.0f);
    }

    TEST_F(SDLCaptureTest, CaptureReadsWhatWasDrawn) {
        Optional<CaptureResult> captured;
        DrawFrame();
        renderer->RequestCapture([&captured](CaptureResult result) { captured = std::move(result); });
        renderer->Present();

        ASSERT_TRUE(captured.has_value());
        ASSERT_TRUE(captured->has_value()) << captured->error().c_str();
        const PixelBuffer& image = **captured;
        EXPECT_EQ(static_cast<float>(image.width), renderer->GetOutputWidth());
        EXPECT_EQ(static_cast<float>(image.height), renderer->GetOutputHeight());
        ASSERT_EQ(image.rgba.size(), static_cast<size_t>(image.width) * image.height * 4);
        EXPECT_EQ(PixelAt(image, 150, 80), AsPixel(kRed));
        EXPECT_EQ(PixelAt(image, 5, 5), AsPixel(kBackground));
        EXPECT_EQ(PixelAt(image, 400, 300), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, CaptureIsServedAtThePresentAndOnlyOnce) {
        int calls = 0;
        DrawFrame();
        renderer->RequestCapture([&calls](CaptureResult) { ++calls; });
        EXPECT_EQ(calls, 0);  // not before the frame is presented

        renderer->Present();
        EXPECT_EQ(calls, 1);
        DrawFrame();
        renderer->Present();
        EXPECT_EQ(calls, 1);
    }

    TEST_F(SDLCaptureTest, NewerRequestReplacesTheOlderOne) {
        Optional<CaptureResult> first;
        Optional<CaptureResult> second;
        DrawFrame();
        renderer->RequestCapture([&first](CaptureResult result) { first = std::move(result); });
        renderer->RequestCapture([&second](CaptureResult result) { second = std::move(result); });

        ASSERT_TRUE(first.has_value());  // answered at once, with an error
        EXPECT_FALSE(first->has_value());
        EXPECT_FALSE(second.has_value());
        renderer->Present();
        ASSERT_TRUE(second.has_value());
        EXPECT_TRUE(second->has_value());
    }

    TEST_F(SDLCaptureTest, PresentWithoutARequestIsAnOrdinaryPresent) {
        DrawFrame();
        renderer->Present();
        SUCCEED();
    }

    TEST_F(SDLCaptureTest, CaptureStillWorksWhenThePresentComesFromTheRenderEvent) {
        Optional<CaptureResult> captured;
        DrawFrame();
        renderer->RequestCapture([&captured](CaptureResult result) { captured = std::move(result); });
        RenderEvents::RenderPresentEvent::Emit();

        ASSERT_TRUE(captured.has_value());
        EXPECT_TRUE(captured->has_value());
    }

    TEST_F(SDLCaptureTest, DestroyDropsAPendingRequest) {
        int calls = 0;
        renderer->RequestCapture([&calls](CaptureResult) { ++calls; });

        renderer->Destroy();

        EXPECT_EQ(calls, 0);
    }

    TEST_F(SDLCaptureTest, ATranslucentRectBlendsWithWhatIsBehindIt) {
        renderer->Clear(kBackground);
        renderer->DrawFilledRect(100.0f, 50.0f, 200.0f, 100.0f, Color{255, 0, 0, 128});

        const PixelBuffer image = PresentAndRead();

        // 128 / 255 of red over (10, 20, 30)
        EXPECT_TRUE(IsNear(PixelAt(image, 150, 80), {133, 10, 15, 255}, kBlendTolerance));
        EXPECT_EQ(PixelAt(image, 5, 5), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, CreateTextureMakesATextureOfTheGivenSize) {
        auto texture = MakeSolidTexture(8, 4, kRed);

        ASSERT_TRUE(texture);
        EXPECT_FLOAT_EQ(texture->GetWidth(), 8.0f);
        EXPECT_FLOAT_EQ(texture->GetHeight(), 4.0f);
        EXPECT_EQ(texture->GetState(), ResourceState::Loaded);
        EXPECT_NE(texture->GetNativeHandle(), nullptr);
    }

    TEST_F(SDLCaptureTest, CreateTextureRefusesAnEmptyImage) {
        const uint8_t pixel[4] = {};

        EXPECT_FALSE(renderer->CreateTexture(pixel, 0, 1));
        EXPECT_FALSE(renderer->CreateTexture(pixel, 1, 0));
        EXPECT_FALSE(renderer->CreateTexture(nullptr, 1, 1));
    }

    TEST_F(SDLCaptureTest, ATextureFromPixelsDrawsItsColors) {
        auto texture = MakeSolidTexture(4, 4, Color{0, 255, 0, 255});
        ASSERT_TRUE(texture);

        renderer->Clear(kBackground);
        renderer->DrawTexture(*texture, nullptr, 100.0f, 50.0f, 40.0f, 40.0f);
        const PixelBuffer image = PresentAndRead();

        EXPECT_EQ(PixelAt(image, 120, 70), AsPixel(Color{0, 255, 0, 255}));
        EXPECT_EQ(PixelAt(image, 90, 70), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, TheTintMultipliesTheTexelAndDoesNotStick) {
        auto texture = MakeSolidTexture(4, 4, Color{255, 255, 255, 255});
        ASSERT_TRUE(texture);

        renderer->Clear(kBackground);
        renderer->DrawTexture(*texture, nullptr, 0.0f, 0.0f, 40.0f, 40.0f, Color{255, 0, 0, 255});
        renderer->DrawTexture(*texture, nullptr, 100.0f, 0.0f, 40.0f, 40.0f);  // the same texture, untinted
        const PixelBuffer image = PresentAndRead();

        EXPECT_EQ(PixelAt(image, 20, 20), AsPixel(kRed));
        EXPECT_EQ(PixelAt(image, 120, 20), AsPixel(Color{255, 255, 255, 255}));
    }

    TEST_F(SDLCaptureTest, TheTintAlphaFadesTheTexture) {
        auto texture = MakeSolidTexture(4, 4, Color{255, 255, 255, 255});
        ASSERT_TRUE(texture);

        renderer->Clear(kBackground);
        renderer->DrawTexture(*texture, nullptr, 0.0f, 0.0f, 40.0f, 40.0f, Color{255, 255, 255, 128});
        const PixelBuffer image = PresentAndRead();

        // 128 / 255 of white over (10, 20, 30)
        EXPECT_TRUE(IsNear(PixelAt(image, 20, 20), {132, 137, 142, 255}, kBlendTolerance));
    }

    TEST_F(SDLCaptureTest, HalfOpacityFadesARectOverBlack) {
        renderer->Clear(Color{0, 0, 0, 255});
        {
            ScopedOpacity half(*renderer, 0.5f);
            renderer->DrawFilledRect(100.0f, 50.0f, 200.0f, 100.0f, Color{255, 255, 255, 255});
        }
        renderer->DrawFilledRect(0.0f, 0.0f, 40.0f, 40.0f, Color{255, 255, 255, 255});  // the opacity is back to 1
        const PixelBuffer image = PresentAndRead();

        EXPECT_TRUE(IsNear(PixelAt(image, 150, 80), {128, 128, 128, 255}, kBlendTolerance));
        EXPECT_EQ(PixelAt(image, 20, 20), AsPixel(Color{255, 255, 255, 255}));
    }

    TEST_F(SDLCaptureTest, HalfOpacityFadesATextureOverBlackAndDoesNotStick) {
        auto texture = MakeSolidTexture(4, 4, Color{255, 255, 255, 255});
        ASSERT_TRUE(texture);

        renderer->Clear(Color{0, 0, 0, 255});
        renderer->SetOpacity(0.5f);
        renderer->DrawTexture(*texture, nullptr, 0.0f, 0.0f, 40.0f, 40.0f);
        renderer->DrawTransformedTexture(*texture, nullptr, 100.0f, 0.0f, 40.0f, 40.0f, Color{255, 255, 255, 255}, DrawTransform{45.0f, 120.0f, 20.0f});
        renderer->SetOpacity(1.0f);
        renderer->DrawTexture(*texture, nullptr, 200.0f, 0.0f, 40.0f, 40.0f);  // the same texture at full opacity
        const PixelBuffer image = PresentAndRead();

        EXPECT_TRUE(IsNear(PixelAt(image, 20, 20), {128, 128, 128, 255}, kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 120, 20), {128, 128, 128, 255}, kBlendTolerance));  // the turned branch is faded once
        EXPECT_EQ(PixelAt(image, 220, 20), AsPixel(Color{255, 255, 255, 255}));
    }

    TEST_F(SDLCaptureTest, HalfOpacityFadesGeometryOverBlack) {
        const Color white{255, 255, 255, 255};
        const Vector<GeometryVertex> quad = {
            {0.0f, 0.0f, white, 0.0f, 0.0f},
            {100.0f, 0.0f, white, 1.0f, 0.0f},
            {100.0f, 100.0f, white, 1.0f, 1.0f},
            {0.0f, 100.0f, white, 0.0f, 1.0f},
        };
        const Vector<int> indices = {0, 1, 2, 0, 2, 3};
        renderer->Clear(Color{0, 0, 0, 255});

        renderer->SetOpacity(0.5f);
        renderer->DrawGeometry(nullptr, {quad.data(), quad.size()}, {indices.data(), indices.size()}, 0.0f, 0.0f);
        renderer->SetOpacity(1.0f);
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 50, 50), {128, 128, 128, 255}, kBlendTolerance));
    }

    TEST_F(SDLCaptureTest, UntexturedGeometryFillsItsTrianglesWithTheVertexColors) {
        const Vector<GeometryVertex> quad = {
            {100.0f, 50.0f, kRed, 0.0f, 0.0f},
            {200.0f, 50.0f, kRed, 1.0f, 0.0f},
            {200.0f, 150.0f, kRed, 1.0f, 1.0f},
            {100.0f, 150.0f, kRed, 0.0f, 1.0f},
        };
        const Vector<int> indices = {0, 1, 2, 0, 2, 3};
        renderer->Clear(kBackground);

        renderer->DrawGeometry(nullptr, {quad.data(), quad.size()}, {indices.data(), indices.size()}, 20.0f, 10.0f);
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_EQ(PixelAt(image, 170, 100), AsPixel(kRed));         // inside the quad, which the translation moved to (120, 60) to (220, 160)
        EXPECT_EQ(PixelAt(image, 110, 100), AsPixel(kBackground));  // where it was before the translation
        EXPECT_EQ(PixelAt(image, 170, 40), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, GeometryBlendsByTheAlphaOfItsVertices) {
        const Color half{255, 0, 0, 128};
        const Vector<GeometryVertex> quad = {
            {0.0f, 0.0f, half, 0.0f, 0.0f},
            {100.0f, 0.0f, half, 1.0f, 0.0f},
            {100.0f, 100.0f, half, 1.0f, 1.0f},
            {0.0f, 100.0f, half, 0.0f, 1.0f},
        };
        const Vector<int> indices = {0, 1, 2, 0, 2, 3};
        renderer->Clear(kBackground);

        renderer->DrawGeometry(nullptr, {quad.data(), quad.size()}, {indices.data(), indices.size()}, 0.0f, 0.0f);
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        // 128/255 of red over (10, 20, 30)
        EXPECT_TRUE(IsNear(PixelAt(image, 50, 50), {132, 10, 15, 255}, kBlendTolerance));
    }

    TEST_F(SDLCaptureTest, GeometryWithATextureMultipliesTheTexelByTheVertexColor) {
        auto texture = MakeSolidTexture(4, 4, Color{255, 255, 255, 255});
        ASSERT_TRUE(texture);
        const Color green{0, 255, 0, 255};
        const Vector<GeometryVertex> quad = {
            {0.0f, 0.0f, green, 0.0f, 0.0f},
            {100.0f, 0.0f, green, 1.0f, 0.0f},
            {100.0f, 100.0f, green, 1.0f, 1.0f},
            {0.0f, 100.0f, green, 0.0f, 1.0f},
        };
        const Vector<int> indices = {0, 1, 2, 0, 2, 3};
        renderer->Clear(kBackground);

        renderer->DrawGeometry(texture.Get(), {quad.data(), quad.size()}, {indices.data(), indices.size()}, 0.0f, 0.0f);
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 50, 50), {0, 255, 0, 255}, kBlendTolerance));
    }

    TEST_F(SDLCaptureTest, GeometryIsClippedLikeARect) {
        const Vector<GeometryVertex> quad = {
            {0.0f, 0.0f, kRed, 0.0f, 0.0f},
            {200.0f, 0.0f, kRed, 1.0f, 0.0f},
            {200.0f, 200.0f, kRed, 1.0f, 1.0f},
            {0.0f, 200.0f, kRed, 0.0f, 1.0f},
        };
        const Vector<int> indices = {0, 1, 2, 0, 2, 3};
        renderer->Clear(kBackground);

        renderer->PushClipRect({50.0f, 50.0f, 40.0f, 40.0f});
        renderer->DrawGeometry(nullptr, {quad.data(), quad.size()}, {indices.data(), indices.size()}, 0.0f, 0.0f);
        renderer->PopClipRect();
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_EQ(PixelAt(image, 60, 60), AsPixel(kRed));
        EXPECT_EQ(PixelAt(image, 120, 120), AsPixel(kBackground));
        EXPECT_EQ(PixelAt(image, 20, 20), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, GeometryWithAnIndexOutOfRangeDrawsNothing) {
        const Vector<GeometryVertex> triangle = {
            {0.0f, 0.0f, kRed, 0.0f, 0.0f},
            {200.0f, 0.0f, kRed, 1.0f, 0.0f},
            {0.0f, 200.0f, kRed, 0.0f, 1.0f},
        };
        const Vector<int> indices = {0, 1, 7};
        renderer->Clear(kBackground);

        renderer->DrawGeometry(nullptr, {triangle.data(), triangle.size()}, {indices.data(), indices.size()}, 0.0f, 0.0f);
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_EQ(PixelAt(image, 20, 20), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, AnAddRectBrightensTheBackgroundAndSaturates) {
        renderer->Clear(kBackground);
        renderer->SetBlendMode(BlendMode::Add);
        renderer->DrawFilledRect(0.0f, 0.0f, 40.0f, 40.0f, Color{250, 100, 0, 255});
        renderer->DrawFilledRect(100.0f, 0.0f, 40.0f, 40.0f, Color{250, 100, 0, 128});  // half of the color is added
        renderer->SetBlendMode(BlendMode::Alpha);
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 20, 20), {255, 120, 30, 255}, kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 120, 20), {135, 70, 30, 255}, kBlendTolerance));
        EXPECT_EQ(PixelAt(image, 60, 20), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, AMultiplyRectMultipliesTheBackground) {
        renderer->Clear(kBackground);
        renderer->SetBlendMode(BlendMode::Multiply);
        renderer->DrawFilledRect(0.0f, 0.0f, 40.0f, 40.0f, Color{128, 255, 0, 255});
        renderer->DrawFilledRect(100.0f, 0.0f, 40.0f, 40.0f, Color{255, 255, 255, 255});  // white leaves the target as it is
        renderer->DrawFilledRect(200.0f, 0.0f, 40.0f, 40.0f, Color{0, 0, 0, 0});          // alpha 0 leaves it alone as well
        renderer->SetBlendMode(BlendMode::Alpha);
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 20, 20), {5, 20, 0, 255}, kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 120, 20), AsPixel(kBackground), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 220, 20), AsPixel(kBackground), kBlendTolerance));
    }

    TEST_F(SDLCaptureTest, AnAddTextureBrightensTheBackground) {
        auto texture = MakeSolidTexture(4, 4, Color{100, 50, 200, 255});
        ASSERT_TRUE(texture);

        renderer->Clear(kBackground);
        renderer->SetBlendMode(BlendMode::Add);
        renderer->DrawTexture(*texture, nullptr, 0.0f, 0.0f, 40.0f, 40.0f);
        renderer->SetBlendMode(BlendMode::Alpha);
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 20, 20), {110, 70, 230, 255}, kBlendTolerance));
    }

    TEST_F(SDLCaptureTest, AMultiplyTextureMultipliesTheBackground) {
        auto texture = MakeSolidTexture(4, 4, Color{128, 255, 0, 255});
        ASSERT_TRUE(texture);

        renderer->Clear(kBackground);
        renderer->SetBlendMode(BlendMode::Multiply);
        renderer->DrawTexture(*texture, nullptr, 0.0f, 0.0f, 40.0f, 40.0f);
        renderer->SetBlendMode(BlendMode::Alpha);
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 20, 20), {5, 20, 0, 255}, kBlendTolerance));
    }

    TEST_F(SDLCaptureTest, ATextureDoesNotKeepTheBlendModeOfAnEarlierDraw) {
        auto texture = MakeSolidTexture(4, 4, Color{100, 50, 200, 255});
        ASSERT_TRUE(texture);

        renderer->Clear(kBackground);
        renderer->SetBlendMode(BlendMode::Add);
        renderer->DrawTexture(*texture, nullptr, 0.0f, 0.0f, 40.0f, 40.0f);
        renderer->SetBlendMode(BlendMode::Alpha);
        renderer->DrawTexture(*texture, nullptr, 100.0f, 0.0f, 40.0f, 40.0f);  // the same texture, plain alpha again
        renderer->DrawTexture(*texture, nullptr, 200.0f, 0.0f, 40.0f, 40.0f, Color{255, 255, 255, 128});
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 20, 20), {110, 70, 230, 255}, kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 120, 20), {100, 50, 200, 255}, kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 220, 20), {55, 35, 115, 255}, kBlendTolerance));  // 128 / 255 of the texel over (10, 20, 30)
    }

    TEST_F(SDLCaptureTest, AnAddGeometryBrightensTheBackground) {
        const Color color{250, 100, 0, 255};
        const Vector<GeometryVertex> quad = {
            {0.0f, 0.0f, color, 0.0f, 0.0f},
            {40.0f, 0.0f, color, 1.0f, 0.0f},
            {40.0f, 40.0f, color, 1.0f, 1.0f},
            {0.0f, 40.0f, color, 0.0f, 1.0f},
        };
        const Vector<int> indices = {0, 1, 2, 0, 2, 3};
        renderer->Clear(kBackground);

        renderer->SetBlendMode(BlendMode::Add);
        renderer->DrawGeometry(nullptr, {quad.data(), quad.size()}, {indices.data(), indices.size()}, 0.0f, 0.0f);
        renderer->SetBlendMode(BlendMode::Alpha);
        renderer->DrawGeometry(nullptr, {quad.data(), quad.size()}, {indices.data(), indices.size()}, 100.0f, 0.0f);  // after the add draw: opaque
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 20, 20), {255, 120, 30, 255}, kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 120, 20), {250, 100, 0, 255}, kBlendTolerance));
    }

    TEST_F(SDLCaptureTest, AnAddTexturedGeometryBrightensTheBackground) {
        auto texture = MakeSolidTexture(4, 4, Color{100, 50, 200, 255});
        ASSERT_TRUE(texture);
        const Color white{255, 255, 255, 255};
        const Vector<GeometryVertex> quad = {
            {0.0f, 0.0f, white, 0.0f, 0.0f},
            {40.0f, 0.0f, white, 1.0f, 0.0f},
            {40.0f, 40.0f, white, 1.0f, 1.0f},
            {0.0f, 40.0f, white, 0.0f, 1.0f},
        };
        const Vector<int> indices = {0, 1, 2, 0, 2, 3};
        renderer->Clear(kBackground);

        renderer->SetBlendMode(BlendMode::Add);
        renderer->DrawGeometry(texture.Get(), {quad.data(), quad.size()}, {indices.data(), indices.size()}, 0.0f, 0.0f);
        renderer->SetBlendMode(BlendMode::Alpha);
        renderer->DrawGeometry(texture.Get(), {quad.data(), quad.size()}, {indices.data(), indices.size()}, 100.0f, 0.0f);
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 20, 20), {110, 70, 230, 255}, kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 120, 20), {100, 50, 200, 255}, kBlendTolerance));
    }

    TEST_F(SDLCaptureTest, ARectTurnedBy90DegreesCoversTheTurnedArea) {
        // 100 by 50 at (100, 50), turned about its center (150, 75): it becomes 50 wide and 100 high, x 125 to 175, y 25 to 125
        renderer->Clear(kBackground);
        renderer->DrawTransformedRect(100.0f, 50.0f, 100.0f, 50.0f, kRed, DrawTransform{90.0f, 150.0f, 75.0f});
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_EQ(PixelAt(image, 150, 35), AsPixel(kRed));         // above the old rect, inside the turned one
        EXPECT_EQ(PixelAt(image, 150, 115), AsPixel(kRed));        // below it
        EXPECT_EQ(PixelAt(image, 150, 75), AsPixel(kRed));         // the middle
        EXPECT_EQ(PixelAt(image, 105, 55), AsPixel(kBackground));  // the old corner
        EXPECT_EQ(PixelAt(image, 195, 95), AsPixel(kBackground));  // the opposite old corner
    }

    TEST_F(SDLCaptureTest, ARotatedRectBlendsByTheBlendMode) {
        renderer->Clear(kBackground);
        renderer->SetBlendMode(BlendMode::Add);
        renderer->DrawTransformedRect(100.0f, 50.0f, 100.0f, 50.0f, Color{250, 100, 0, 255}, DrawTransform{90.0f, 150.0f, 75.0f});
        renderer->SetBlendMode(BlendMode::Alpha);
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 150, 35), {255, 120, 30, 255}, kBlendTolerance));
    }

    TEST_F(SDLCaptureTest, AFlippedTextureShowsTheMirroredColors) {
        // Four blocks of 4 by 4 texels: red, green on the top; blue, white below. Each draw is 40 by 40, so a block is 20 by 20 and its middle is 10 from the edges.
        // The samples lie well inside the blocks. On the software triangle path (DrawGeometry) the UVs 0 to 1 are spread over the texels 0 to n-1 and rounded down
        // (SDL_triangle.c), so a block edge moves and a 2 by 2 texture shows one texel; a transformed texture goes through SDL_RenderTextureRotated and has no such shift.
        const Color white{255, 255, 255, 255};
        const Array<Color, 4> texels = {kRed, Color{0, 255, 0, 255}, Color{0, 0, 255, 255}, white};
        auto texture = MakeTexture(MakeQuadrants(texels));
        ASSERT_TRUE(texture);

        renderer->Clear(kBackground);
        renderer->DrawTransformedTexture(*texture, nullptr, 0.0f, 0.0f, 40.0f, 40.0f, white, DrawTransform{0.0f, 0.0f, 0.0f, true, false});
        renderer->DrawTransformedTexture(*texture, nullptr, 100.0f, 0.0f, 40.0f, 40.0f, white, DrawTransform{0.0f, 0.0f, 0.0f, false, true});
        renderer->DrawTransformedTexture(*texture, nullptr, 200.0f, 0.0f, 40.0f, 40.0f, white, DrawTransform{0.0f, 0.0f, 0.0f, true, true});
        renderer->DrawTransformedTexture(*texture, nullptr, 300.0f, 0.0f, 40.0f, 40.0f, white, DrawTransform{});  // identity: the plain draw
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        // flipX: green, red / white, blue
        EXPECT_TRUE(IsNear(PixelAt(image, 10, 10), AsPixel(texels[1]), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 30, 10), AsPixel(texels[0]), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 10, 30), AsPixel(texels[3]), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 30, 30), AsPixel(texels[2]), kBlendTolerance));
        // flipY: blue, white / red, green
        EXPECT_TRUE(IsNear(PixelAt(image, 110, 10), AsPixel(texels[2]), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 130, 10), AsPixel(texels[3]), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 110, 30), AsPixel(texels[0]), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 130, 30), AsPixel(texels[1]), kBlendTolerance));
        // both: white, blue / green, red
        EXPECT_TRUE(IsNear(PixelAt(image, 210, 10), AsPixel(texels[3]), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 230, 10), AsPixel(texels[2]), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 210, 30), AsPixel(texels[1]), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 230, 30), AsPixel(texels[0]), kBlendTolerance));
        // no flip: red, green / blue, white
        EXPECT_TRUE(IsNear(PixelAt(image, 310, 10), AsPixel(texels[0]), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 330, 30), AsPixel(texels[3]), kBlendTolerance));
    }

    TEST_F(SDLCaptureTest, ATextureTurnedByExactly90And270DegreesIsTurnedAndNotFlipped) {
        // 80 by 40, the left half red and the right half green, at (140, 140) turned about its center (180, 160): the turned box is x 160 to 200, y 120 to 200.
        // Clockwise by 90 degrees the left half goes to the top; by 270 degrees to the bottom. (On the software renderer two triangles of an upright rectangle are
        // a plain blit that only flips, so DrawGeometry() would draw this unturned.)
        const Color green{0, 255, 0, 255};
        auto texture = MakeTexture(MakeQuadrants({kRed, green, kRed, green}));
        ASSERT_TRUE(texture);
        const Color white{255, 255, 255, 255};

        renderer->Clear(kBackground);
        renderer->DrawTransformedTexture(*texture, nullptr, 140.0f, 140.0f, 80.0f, 40.0f, white, DrawTransform{90.0f, 180.0f, 160.0f});
        renderer->DrawTransformedTexture(*texture, nullptr, 440.0f, 140.0f, 80.0f, 40.0f, white, DrawTransform{270.0f, 480.0f, 160.0f});
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        // 90 degrees: red above, green below, in both columns of the turned box
        EXPECT_TRUE(IsNear(PixelAt(image, 170, 130), AsPixel(kRed), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 190, 130), AsPixel(kRed), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 170, 190), AsPixel(green), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 190, 190), AsPixel(green), kBlendTolerance));
        EXPECT_EQ(PixelAt(image, 150, 160), AsPixel(kBackground));  // the old rect is left
        EXPECT_EQ(PixelAt(image, 210, 160), AsPixel(kBackground));
        // 270 degrees: green above, red below
        EXPECT_TRUE(IsNear(PixelAt(image, 470, 130), AsPixel(green), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 490, 130), AsPixel(green), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 470, 190), AsPixel(kRed), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 490, 190), AsPixel(kRed), kBlendTolerance));
        EXPECT_EQ(PixelAt(image, 450, 160), AsPixel(kBackground));
        EXPECT_EQ(PixelAt(image, 510, 160), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, ATextureTurnedAboutACornerPivotLandsWhereTheTurnPutsIt) {
        // The same 80 by 40 at (140, 140) turned by 90 degrees about its top-left corner: the box is x 100 to 140, y 140 to 220, the red left half on the top
        const Color green{0, 255, 0, 255};
        auto texture = MakeTexture(MakeQuadrants({kRed, green, kRed, green}));
        ASSERT_TRUE(texture);

        renderer->Clear(kBackground);
        renderer->DrawTransformedTexture(*texture, nullptr, 140.0f, 140.0f, 80.0f, 40.0f, Color{255, 255, 255, 255}, DrawTransform{90.0f, 140.0f, 140.0f});
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 120, 150), AsPixel(kRed), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 120, 210), AsPixel(green), kBlendTolerance));
        EXPECT_EQ(PixelAt(image, 180, 160), AsPixel(kBackground));  // the old rect is left
        EXPECT_EQ(PixelAt(image, 160, 160), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, AFlipIsAppliedBeforeTheRotation) {
        // Red, green / blue, white at 80 by 40, turned by 90 degrees about the center, in a box of 40 by 80 whose quadrants are 20 by 40:
        // a turn puts (top-left, top-right, bottom-left, bottom-right) at (top-right, bottom-right, top-left, bottom-left); a flip on X first swaps the columns of the picture
        const Color green{0, 255, 0, 255};
        const Color blue{0, 0, 255, 255};
        const Color white{255, 255, 255, 255};
        auto texture = MakeTexture(MakeQuadrants({kRed, green, blue, white}));
        ASSERT_TRUE(texture);

        renderer->Clear(kBackground);
        renderer->DrawTransformedTexture(*texture, nullptr, 140.0f, 140.0f, 80.0f, 40.0f, white, DrawTransform{90.0f, 180.0f, 160.0f});
        renderer->DrawTransformedTexture(*texture, nullptr, 440.0f, 140.0f, 80.0f, 40.0f, white, DrawTransform{90.0f, 480.0f, 160.0f, true, false});
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        // turned only: blue, red / white, green (top row, bottom row)
        EXPECT_TRUE(IsNear(PixelAt(image, 170, 140), AsPixel(blue), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 190, 140), AsPixel(kRed), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 170, 180), AsPixel(white), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 190, 180), AsPixel(green), kBlendTolerance));
        // flipped on X, then turned: white, green / blue, red
        EXPECT_TRUE(IsNear(PixelAt(image, 470, 140), AsPixel(white), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 490, 140), AsPixel(green), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 470, 180), AsPixel(blue), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 490, 180), AsPixel(kRed), kBlendTolerance));
    }

    TEST_F(SDLCaptureTest, ATurnedTextureUnderAViewTransformKeepsItsOrientation) {
        // Left half red, right half green, 80 by 40 at (20, 20) turned by 90 degrees about its center (60, 40): units x 40 to 80, y 0 to 80, red on the top.
        // The view maps (x, y) to (2x + 20, 2y + 10), so the box is x 100 to 180, y 10 to 170 in pixels and the old rect x 60 to 220, y 50 to 130.
        const Color green{0, 255, 0, 255};
        auto texture = MakeTexture(MakeQuadrants({kRed, green, kRed, green}));
        ASSERT_TRUE(texture);

        renderer->Clear(kBackground);
        renderer->SetViewTransform({2.0f, 20.0f, 10.0f});
        renderer->DrawTransformedTexture(*texture, nullptr, 20.0f, 20.0f, 80.0f, 40.0f, Color{255, 255, 255, 255}, DrawTransform{90.0f, 60.0f, 40.0f});
        renderer->SetViewTransform({});
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 120, 30), AsPixel(kRed), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 160, 30), AsPixel(kRed), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 120, 150), AsPixel(green), kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, 160, 150), AsPixel(green), kBlendTolerance));
        EXPECT_EQ(PixelAt(image, 80, 90), AsPixel(kBackground));  // the old rect, outside the turned box
        EXPECT_EQ(PixelAt(image, 200, 90), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, ARotatedTextureUnderAViewTransformIsTurnedAndScaled) {
        auto texture = MakeSolidTexture(4, 4, Color{0, 255, 0, 255});
        ASSERT_TRUE(texture);

        // 100 by 50 at (0, 0) turned by 90 degrees about (50, 25) covers x 25 to 75, y -25 to 75; the view maps (x, y) to (2x + 20, 2y + 10)
        renderer->Clear(kBackground);
        renderer->SetViewTransform({2.0f, 20.0f, 10.0f});
        renderer->DrawTransformedTexture(*texture, nullptr, 0.0f, 0.0f, 100.0f, 50.0f, Color{255, 255, 255, 255}, DrawTransform{90.0f, 50.0f, 25.0f});
        renderer->SetViewTransform({});
        const PixelBuffer image = PresentAndRead();

        ASSERT_FALSE(image.rgba.empty());
        EXPECT_TRUE(IsNear(PixelAt(image, 120, 150), {0, 255, 0, 255}, kBlendTolerance));  // scene (50, 70): in the turned area, below the old rect
        EXPECT_TRUE(IsNear(PixelAt(image, 120, 30), {0, 255, 0, 255}, kBlendTolerance));   // scene (50, 10)
        EXPECT_EQ(PixelAt(image, 30, 20), AsPixel(kBackground));                           // scene (5, 5): in the old rect, outside the turned one
        EXPECT_EQ(PixelAt(image, 200, 100), AsPixel(kBackground));                         // scene (90, 45): the same on the right
    }

    TEST_F(SDLCaptureTest, AClippedRectDrawsOnlyInsideTheClip) {
        renderer->Clear(kBackground);
        renderer->PushClipRect({100.0f, 50.0f, 50.0f, 50.0f});
        renderer->DrawFilledRect(0.0f, 0.0f, 400.0f, 300.0f, kRed);
        renderer->PopClipRect();
        const PixelBuffer image = PresentAndRead();

        EXPECT_EQ(PixelAt(image, 120, 70), AsPixel(kRed));
        EXPECT_EQ(PixelAt(image, 99, 70), AsPixel(kBackground));
        EXPECT_EQ(PixelAt(image, 151, 70), AsPixel(kBackground));
        EXPECT_EQ(PixelAt(image, 120, 49), AsPixel(kBackground));
        EXPECT_EQ(PixelAt(image, 120, 101), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, ATextureIsClippedLikeARect) {
        auto texture = MakeSolidTexture(4, 4, Color{255, 255, 255, 255});
        ASSERT_TRUE(texture);

        renderer->Clear(kBackground);
        renderer->PushClipRect({0.0f, 0.0f, 20.0f, 100.0f});
        renderer->DrawTexture(*texture, nullptr, 0.0f, 0.0f, 40.0f, 40.0f);
        renderer->PopClipRect();
        const PixelBuffer image = PresentAndRead();

        EXPECT_EQ(PixelAt(image, 10, 20), AsPixel(Color{255, 255, 255, 255}));
        EXPECT_EQ(PixelAt(image, 30, 20), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, PoppingTheClipDrawsEverywhereAgain) {
        renderer->Clear(kBackground);
        renderer->PushClipRect({0.0f, 0.0f, 10.0f, 10.0f});
        renderer->PopClipRect();
        renderer->DrawFilledRect(100.0f, 50.0f, 50.0f, 50.0f, kRed);
        const PixelBuffer image = PresentAndRead();

        EXPECT_EQ(PixelAt(image, 120, 70), AsPixel(kRed));
    }

    TEST_F(SDLCaptureTest, AnEmptyClipHidesTheDraws) {
        renderer->Clear(kBackground);
        renderer->PushClipRect({0.0f, 0.0f, 10.0f, 10.0f});
        renderer->PushClipRect({200.0f, 200.0f, 10.0f, 10.0f});
        renderer->DrawFilledRect(0.0f, 0.0f, 400.0f, 300.0f, kRed);
        renderer->PopClipRect();
        renderer->PopClipRect();
        const PixelBuffer image = PresentAndRead();

        EXPECT_EQ(PixelAt(image, 5, 5), AsPixel(kBackground));
        EXPECT_EQ(PixelAt(image, 205, 205), AsPixel(kBackground));
    }

    TEST_F(SDLCaptureTest, TheClipIsInOutputPixelsAndNotMovedByTheViewTransform) {
        renderer->Clear(kBackground);
        renderer->SetViewTransform({2.0f, 0.0f, 0.0f});
        renderer->PushClipRect({100.0f, 0.0f, 50.0f, 50.0f});
        renderer->DrawFilledRect(0.0f, 0.0f, 200.0f, 100.0f, kRed);  // 0..400 by 0..200 in pixels
        renderer->PopClipRect();
        renderer->SetViewTransform({});
        const PixelBuffer image = PresentAndRead();

        EXPECT_EQ(PixelAt(image, 120, 20), AsPixel(kRed));
        EXPECT_EQ(PixelAt(image, 80, 20), AsPixel(kBackground));
    }

    TEST(RendererCaptureDefaultTest, ARendererThatCannotReadItsOutputAnswersWithAnError) {
        BlindRenderer renderer;
        Optional<CaptureResult> captured;

        renderer.RequestCapture([&captured](CaptureResult result) { captured = std::move(result); });

        ASSERT_TRUE(captured.has_value());
        ASSERT_FALSE(captured->has_value());
        EXPECT_FALSE(captured->error().empty());
    }

    TEST(EncodePngTest, PngHasTheSizeOfTheImage) {
        const auto png = EncodePng(MakeImage(64, 32, kRed), 0);

        ASSERT_TRUE(png.has_value()) << png.error().c_str();
        EXPECT_EQ(png->width, 64u);
        EXPECT_EQ(png->height, 32u);
        ASSERT_GT(png->bytes.size(), 24u);
        const Array<uint8_t, 8> signature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        EXPECT_TRUE(std::equal(signature.begin(), signature.end(), png->bytes.begin()));
        EXPECT_EQ(PngSize(png->bytes), (Pair<uint32_t, uint32_t>{64, 32}));
    }

    TEST(EncodePngTest, PixelsSurviveTheRoundTrip) {
        PixelBuffer image = MakeImage(8, 4, kBackground);
        const size_t marked = (static_cast<size_t>(2) * image.width + 5) * 4;  // pixel (5, 2)
        image.rgba[marked] = 200;
        image.rgba[marked + 1] = 100;
        image.rgba[marked + 2] = 50;
        image.rgba[marked + 3] = 255;

        const auto png = EncodePng(image, 0);
        ASSERT_TRUE(png.has_value()) << png.error().c_str();
        const PixelBuffer decoded = DecodePng(png->bytes);

        ASSERT_EQ(decoded.width, 8u);
        ASSERT_EQ(decoded.height, 4u);
        EXPECT_EQ(PixelAt(decoded, 5, 2), (Pixel{200, 100, 50, 255}));
        EXPECT_EQ(PixelAt(decoded, 0, 0), AsPixel(kBackground));
        EXPECT_EQ(decoded.rgba, image.rgba);
    }

    TEST(EncodePngTest, ALargeImageIsScaledDownKeepingItsProportions) {
        const auto wide = EncodePng(MakeImage(1920, 1080, kBackground), 1024);
        ASSERT_TRUE(wide.has_value()) << wide.error().c_str();
        EXPECT_EQ(wide->width, 1024u);
        EXPECT_EQ(wide->height, 576u);
        EXPECT_EQ(PngSize(wide->bytes), (Pair<uint32_t, uint32_t>{1024, 576}));

        const auto tall = EncodePng(MakeImage(100, 400, kBackground), 200);
        ASSERT_TRUE(tall.has_value()) << tall.error().c_str();
        EXPECT_EQ(tall->width, 50u);
        EXPECT_EQ(tall->height, 200u);
    }

    TEST(EncodePngTest, ScalingKeepsTheColourOfAFlatImage) {
        const auto png = EncodePng(MakeImage(400, 200, kRed), 100);
        ASSERT_TRUE(png.has_value()) << png.error().c_str();

        const PixelBuffer decoded = DecodePng(png->bytes);
        ASSERT_EQ(decoded.width, 100u);
        ASSERT_EQ(decoded.height, 50u);
        EXPECT_EQ(PixelAt(decoded, 50, 25), AsPixel(kRed));
    }

    TEST(EncodePngTest, AnImageIsNeverScaledUp) {
        const auto png = EncodePng(MakeImage(64, 64, kRed), 1024);
        ASSERT_TRUE(png.has_value()) << png.error().c_str();
        EXPECT_EQ(png->width, 64u);
        EXPECT_EQ(png->height, 64u);
    }

    TEST(EncodePngTest, ZeroMaxSideMeansNoScaling) {
        const auto png = EncodePng(MakeImage(300, 200, kRed), 0);
        ASSERT_TRUE(png.has_value()) << png.error().c_str();
        EXPECT_EQ(png->width, 300u);
        EXPECT_EQ(png->height, 200u);
    }

    TEST(EncodePngTest, ASideIsNeverScaledToNothing) {
        const auto png = EncodePng(MakeImage(4000, 1, kRed), 100);
        ASSERT_TRUE(png.has_value()) << png.error().c_str();
        EXPECT_EQ(png->width, 100u);
        EXPECT_EQ(png->height, 1u);
    }

    TEST(EncodePngTest, EmptyAndTruncatedImagesAreRefused) {
        EXPECT_FALSE(EncodePng(PixelBuffer{}, 0).has_value());

        PixelBuffer truncated = MakeImage(4, 4, kRed);
        truncated.rgba.resize(truncated.rgba.size() - 1);
        EXPECT_FALSE(EncodePng(truncated, 0).has_value());

        PixelBuffer noWidth = MakeImage(4, 4, kRed);
        noWidth.width = 0;
        EXPECT_FALSE(EncodePng(noWidth, 0).has_value());
    }

    TEST_F(ScreenshotControlTest, ScreenshotAnswersAtTheEndOfTheFrameWithAPng) {
        DrawFrame();
        Post("screenshot");
        service.Drain();
        EXPECT_TRUE(responses.empty());  // the picture is taken when the frame is presented

        renderer->Present();
        EXPECT_TRUE(responses.empty());  // and the answer goes out at the end of the frame
        service.EndFrame();

        ASSERT_EQ(responses.size(), 1u);
        ASSERT_NE(Result(0), nullptr);
        const auto base64 = GetJsonString(*Result(0), "png_base64");
        ASSERT_TRUE(base64.has_value());
        EXPECT_TRUE(StringView(base64->data(), base64->size()).starts_with(kPngBase64Prefix));
        const int64_t width = GetJsonInt(*Result(0), "width").value_or(0);
        const int64_t height = GetJsonInt(*Result(0), "height").value_or(0);
        EXPECT_EQ(width, 1024);  // the window of the engine config is 1920 by 1080, cut to the default 1024
        EXPECT_EQ(height, 576);
    }

    TEST_F(ScreenshotControlTest, MaxSizeSetsTheLongerSide) {
        DrawFrame();
        Json params = Json::object();
        params["maxSize"] = 200;
        Post("screenshot", params);
        service.Drain();
        renderer->Present();
        service.EndFrame();

        ASSERT_EQ(responses.size(), 1u);
        ASSERT_NE(Result(0), nullptr);
        EXPECT_EQ(GetJsonInt(*Result(0), "width").value_or(0), 200);
        EXPECT_EQ(GetJsonInt(*Result(0), "height").value_or(0), 113);
    }

    TEST_F(ScreenshotControlTest, MaxSizeOutOfRangeIsRefusedAndAsksForNoFrame) {
        service.SetManualClock(true);
        Json tooSmall = Json::object();
        tooSmall["maxSize"] = static_cast<int64_t>(kMinScreenshotSize) - 1;
        Json tooLarge = Json::object();
        tooLarge["maxSize"] = static_cast<int64_t>(kMaxScreenshotSize) + 1;
        Json fractional = Json::object();
        fractional["maxSize"] = 100.5;
        Json text = Json::object();
        text["maxSize"] = "big";
        Json nothing = Json::object();
        nothing["maxSize"] = nullptr;

        for (const Json& params : {tooSmall, tooLarge, fractional, text, nothing}) {
            Post("screenshot", params);
        }
        service.Drain();

        ASSERT_EQ(responses.size(), 5u);
        for (const Json& response : responses) {
            EXPECT_EQ(ErrorCode(response), "invalid_params");
        }
        EXPECT_FALSE(service.IsStepDue());
    }

    TEST_F(ScreenshotControlTest, TheLargestAndSmallestSizesAreAccepted) {
        for (const uint32_t size : {kMinScreenshotSize, kMaxScreenshotSize}) {
            DrawFrame();
            Json params = Json::object();
            params["maxSize"] = size;
            Post("screenshot", params);
            service.Drain();
            renderer->Present();
            service.EndFrame();
        }

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(GetJsonInt(*Result(0), "width").value_or(0), static_cast<int64_t>(kMinScreenshotSize));
        EXPECT_NE(Result(1), nullptr);
    }

    TEST_F(ScreenshotControlTest, OnTheManualClockAScreenshotIsOneFrameOfZeroSeconds) {
        service.SetManualClock(true);
        Post("screenshot");
        service.Drain();

        ASSERT_TRUE(service.IsStepDue());
        EXPECT_EQ(service.GetStepSeconds(), 0.0);

        DrawFrame();
        renderer->Present();
        service.EndFrame();
        EXPECT_FALSE(service.IsStepDue());
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_NE(Result(0), nullptr);
    }

    TEST_F(ScreenshotControlTest, ScreenshotFailsWhenNoFrameWasPresented) {
        Post("screenshot");
        service.Drain();

        service.EndFrame();  // the frame ended without a present

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ErrorCode(responses[0]), "capture_failed");
    }

    TEST_F(ScreenshotControlTest, ScreenshotFailsAtOnceWhereTheRendererCannotCapture) {
        CoreManager::GetRendererManager().SetRenderer(New<BlindRenderer>());
        Post("screenshot");
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);  // refused in the handler: no frame is waited for
        EXPECT_EQ(ErrorCode(responses[0]), "capture_failed");
        EXPECT_FALSE(service.IsStepDue());
    }

    TEST_F(ScreenshotControlTest, ScreenshotFailsWithoutARenderer) {
        CoreManager::GetRendererManager().SetRenderer({});
        Post("screenshot");
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ErrorCode(responses[0]), "capture_failed");
    }

    TEST_F(ScreenshotControlTest, ARequestBehindAScreenshotWaitsForItsFrame) {
        service.RegisterHandler("after", [](ControlCall& call) { call.Reply(Json::object()); });
        DrawFrame();
        Post("screenshot");
        Post("after");
        service.Drain();
        EXPECT_TRUE(responses.empty());

        renderer->Present();
        service.EndFrame();
        service.Drain();

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_NE(Result(0), nullptr);
        EXPECT_EQ(GetJsonInt(responses[1], "id").value_or(0), 2);
    }

}  // namespace BECore::Tests
