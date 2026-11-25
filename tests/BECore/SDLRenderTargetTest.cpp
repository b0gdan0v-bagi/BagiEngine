#include <BECore/Renderer/IRenderTarget.h>
#include <BECore/Renderer/IRenderer.h>
#include <Common/EngineEnvironment.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreSDL/SDLRenderTarget.h>
#include <CoreSDL/SDLRendererBackend.h>
#include <SDL3/SDL.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kWindowConfig = "config/SDLWindowConfig.xml";
        constexpr uint32_t kTargetSize = 40;
        constexpr float kShownAtX = 200.0f;  // where a test draws a target onto the back buffer
        constexpr float kShownAtY = 50.0f;
        constexpr float kInside = 20.0f;  // from the corner of the shown target: well inside it

        const Color kBackground{10, 20, 30, 255};
        const Color kRed{255, 0, 0, 255};
        const Color kGreen{0, 255, 0, 255};
        const Color kBlue{0, 0, 255, 255};
        const Color kBlack{0, 0, 0, 255};

        using Pixel = Array<uint8_t, 4>;

        Pixel AsPixel(const Color& color) {
            return {color.r, color.g, color.b, color.a};
        }

        Pixel PixelAt(const PixelBuffer& image, float x, float y) {
            const size_t offset = (static_cast<size_t>(y) * image.width + static_cast<size_t>(x)) * 4;
            if (offset + 4 > image.rgba.size()) {
                ADD_FAILURE() << "no pixel (" << x << ", " << y << ") in an image of " << image.width << " x " << image.height;
                return {};
            }
            return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2], image.rgba[offset + 3]};
        }

        /// A headless window with the SDL renderer on it; a machine without the dummy video driver skips the test
        class SDLRenderTargetTest : public ::testing::Test {
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

            /// Draws the target onto the back buffer at (@p x, @p y) with SDL itself, so the test reads what the target holds
            void Show(const IntrusivePtr<IRenderTarget>& target, float x, float y) {
                auto* sdlTarget = dynamic_cast<SDLRenderTarget*>(target.Get());
                ASSERT_NE(sdlTarget, nullptr);
                const SDL_FRect destination{x, y, static_cast<float>(target->GetWidth()), static_cast<float>(target->GetHeight())};
                SDL_RenderTexture(renderer->GetSDLRenderer(), sdlTarget->GetSDLTexture(), nullptr, &destination);
            }

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

            IntrusivePtr<SDLMainWindow> window;
            IntrusivePtr<SDLRendererBackend> renderer;
        };

    }  // namespace

    TEST_F(SDLRenderTargetTest, TargetHasItsSizeAndATextureId) {
        const auto target = renderer->CreateRenderTarget(64, 32);
        ASSERT_TRUE(target);
        EXPECT_EQ(target->GetWidth(), 64u);
        EXPECT_EQ(target->GetHeight(), 32u);
        EXPECT_NE(target->GetImGuiTextureId(), nullptr);
    }

    TEST_F(SDLRenderTargetTest, ZeroSideGivesNoTarget) {
        EXPECT_FALSE(renderer->CreateRenderTarget(0, 32));
        EXPECT_FALSE(renderer->CreateRenderTarget(64, 0));
    }

    TEST_F(SDLRenderTargetTest, DrawsBetweenSetAndUnsetGoIntoTheTargetOnly) {
        const auto target = renderer->CreateRenderTarget(kTargetSize, kTargetSize);
        ASSERT_TRUE(target);

        renderer->Clear(kBackground);
        renderer->SetRenderTarget(target.Get());
        renderer->DrawFilledRect(0.0f, 0.0f, static_cast<float>(kTargetSize), static_cast<float>(kTargetSize), kRed);
        renderer->UnsetRenderTarget();
        Show(target, kShownAtX, kShownAtY);
        const PixelBuffer image = PresentAndRead();

        EXPECT_EQ(PixelAt(image, kInside, kInside), AsPixel(kBackground)) << "the rect drawn into the target reached the back buffer";
        EXPECT_EQ(PixelAt(image, kShownAtX + kInside, kShownAtY + kInside), AsPixel(kRed));
    }

    TEST_F(SDLRenderTargetTest, ClearBeforeSetColoursTheTargetAndTheNextPassStartsBlack) {
        const auto first = renderer->CreateRenderTarget(kTargetSize, kTargetSize);
        const auto second = renderer->CreateRenderTarget(kTargetSize, kTargetSize);
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);

        renderer->Clear(kGreen);
        renderer->SetRenderTarget(first.Get());
        renderer->UnsetRenderTarget();
        renderer->SetRenderTarget(second.Get());  // no Clear() named a colour for this pass since the last one: opaque black
        renderer->UnsetRenderTarget();
        renderer->Clear(kBackground);
        Show(first, kShownAtX, kShownAtY);
        Show(second, kShownAtX + 100.0f, kShownAtY);
        const PixelBuffer image = PresentAndRead();

        EXPECT_EQ(PixelAt(image, kShownAtX + kInside, kShownAtY + kInside), AsPixel(kGreen));
        EXPECT_EQ(PixelAt(image, kShownAtX + 100.0f + kInside, kShownAtY + kInside), AsPixel(kBlack));
        EXPECT_EQ(PixelAt(image, kInside, kInside), AsPixel(kBackground)) << "the back buffer keeps the colour of its own Clear()";
    }

    TEST_F(SDLRenderTargetTest, OutputSizeIsTheTargetsBetweenSetAndUnset) {
        const auto target = renderer->CreateRenderTarget(64, 32);
        ASSERT_TRUE(target);
        const float windowWidth = renderer->GetOutputWidth();
        const float windowHeight = renderer->GetOutputHeight();
        ASSERT_GT(windowWidth, 64.0f);

        renderer->SetRenderTarget(target.Get());
        EXPECT_EQ(renderer->GetOutputWidth(), 64.0f);
        EXPECT_EQ(renderer->GetOutputHeight(), 32.0f);
        renderer->UnsetRenderTarget();
        EXPECT_EQ(renderer->GetOutputWidth(), windowWidth);
        EXPECT_EQ(renderer->GetOutputHeight(), windowHeight);
    }

    TEST_F(SDLRenderTargetTest, TargetReleasedAfterTheRendererIsDestroyedIsSafe) {
        auto target = renderer->CreateRenderTarget(kTargetSize, kTargetSize);
        ASSERT_TRUE(target);
        renderer->SetRenderTarget(target.Get());

        renderer->Destroy();

        EXPECT_EQ(target->GetImGuiTextureId(), nullptr);
        EXPECT_EQ(target->GetWidth(), kTargetSize);
        target.Reset();  // would destroy the texture a second time without the release in Destroy()
    }

    TEST_F(SDLRenderTargetTest, TargetReleasedBeforeTheRendererIsSafe) {
        {
            const auto target = renderer->CreateRenderTarget(kTargetSize, kTargetSize);
            ASSERT_TRUE(target);
        }
        renderer->Destroy();
        SUCCEED();
    }

    TEST_F(SDLRenderTargetTest, TargetDestroyedWhileCurrentLeavesTheBackBufferDrawable) {
        auto target = renderer->CreateRenderTarget(kTargetSize, kTargetSize);
        ASSERT_TRUE(target);
        renderer->Clear(kBackground);
        renderer->SetRenderTarget(target.Get());
        target.Reset();

        renderer->DrawFilledRect(kInside, kInside, 10.0f, 10.0f, kRed);
        const PixelBuffer image = PresentAndRead();

        EXPECT_EQ(PixelAt(image, kInside + 5.0f, kInside + 5.0f), AsPixel(kRed));
    }

    TEST_F(SDLRenderTargetTest, ForeignOrReleasedTargetKeepsTheBackBuffer) {
        auto target = renderer->CreateRenderTarget(kTargetSize, kTargetSize);
        ASSERT_TRUE(target);
        auto* sdlTarget = dynamic_cast<SDLRenderTarget*>(target.Get());
        ASSERT_NE(sdlTarget, nullptr);
        renderer->Clear(kBackground);
        const float windowWidth = renderer->GetOutputWidth();

        renderer->SetRenderTarget(nullptr);
        EXPECT_EQ(renderer->GetOutputWidth(), windowWidth);

        renderer->Destroy();
        renderer->SetRenderTarget(target.Get());  // its texture is gone with the renderer
        EXPECT_EQ(sdlTarget->GetSDLTexture(), nullptr);
    }

    TEST_F(SDLRenderTargetTest, TwoTargetsDrawIndependentlyAndAResizeIsANewTarget) {
        auto first = renderer->CreateRenderTarget(kTargetSize, kTargetSize);
        const auto second = renderer->CreateRenderTarget(kTargetSize * 2, kTargetSize);
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);

        renderer->SetRenderTarget(first.Get());
        renderer->DrawFilledRect(0.0f, 0.0f, static_cast<float>(kTargetSize), static_cast<float>(kTargetSize), kRed);
        renderer->SetRenderTarget(second.Get());
        renderer->DrawFilledRect(0.0f, 0.0f, static_cast<float>(kTargetSize * 2), static_cast<float>(kTargetSize), kBlue);
        renderer->UnsetRenderTarget();

        first = renderer->CreateRenderTarget(kTargetSize / 2, kTargetSize / 2);  // the viewport's resize: the old target goes, a new one comes
        ASSERT_TRUE(first);
        renderer->SetRenderTarget(first.Get());
        renderer->DrawFilledRect(0.0f, 0.0f, static_cast<float>(kTargetSize), static_cast<float>(kTargetSize), kGreen);
        renderer->UnsetRenderTarget();

        renderer->Clear(kBackground);
        Show(first, kShownAtX, kShownAtY);
        Show(second, kShownAtX + 100.0f, kShownAtY);
        const PixelBuffer image = PresentAndRead();

        EXPECT_EQ(PixelAt(image, kShownAtX + 10.0f, kShownAtY + 10.0f), AsPixel(kGreen));
        EXPECT_EQ(PixelAt(image, kShownAtX + 100.0f + 10.0f, kShownAtY + 10.0f), AsPixel(kBlue));
        EXPECT_EQ(PixelAt(image, kShownAtX + 100.0f + 70.0f, kShownAtY + 10.0f), AsPixel(kBlue)) << "the wider target keeps its whole width";
    }

}  // namespace BECore::Tests
