#include <BECore/MainWindow/IMainWindow.h>
#include <Common/EngineEnvironment.h>
#include <CoreSDL/SDLMainWindow.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kWindowConfig = "config/SDLWindowConfig.xml";

        /// A window that only has a size, like a test double or a platform that knows no more
        class SizeOnlyWindow : public IMainWindow {
        public:
            SizeOnlyWindow(int width, int height) : _width(width), _height(height) {}

            bool Initialize(eastl::string_view /*configPath*/) override {
                return true;
            }

            void Destroy() override {}

            bool IsValid() const override {
                return true;
            }

            int GetWidth() const override {
                return _width;
            }

            int GetHeight() const override {
                return _height;
            }

        private:
            int _width;
            int _height;
        };

    }  // namespace

    TEST(WindowMetricsTest, AWindowThatKnowsOnlyItsSizeHasNeutralMetrics) {
        const SizeOnlyWindow window(1280, 720);

        EXPECT_FLOAT_EQ(window.GetPixelDensity(), 1.0f);
        EXPECT_FLOAT_EQ(window.GetDisplayScale(), 1.0f);
        const Rect safeArea = window.GetSafeArea();
        EXPECT_FLOAT_EQ(safeArea.x, 0.0f);
        EXPECT_FLOAT_EQ(safeArea.y, 0.0f);
        EXPECT_FLOAT_EQ(safeArea.w, 1280.0f);
        EXPECT_FLOAT_EQ(safeArea.h, 720.0f);
    }

    TEST(WindowMetricsTest, AnSDLWindowWithoutAHandleAnswersNeutralMetrics) {
        const SDLMainWindow window;

        EXPECT_FLOAT_EQ(window.GetPixelDensity(), 1.0f);
        EXPECT_FLOAT_EQ(window.GetDisplayScale(), 1.0f);
        EXPECT_TRUE(window.GetSafeArea().IsEmpty());
    }

    /// A headless window: the dummy video driver has no notch, no scaling and no high-density mode
    class SDLWindowMetricsTest : public ::testing::Test {
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
        }

        void TearDown() override {
            if (window) {
                window->Destroy();
            }
        }

        IntrusivePtr<SDLMainWindow> window;
    };

    TEST_F(SDLWindowMetricsTest, ADummyDriverWindowIsOneToOneWithItsPixels) {
        EXPECT_FLOAT_EQ(window->GetPixelDensity(), 1.0f);
        EXPECT_FLOAT_EQ(window->GetDisplayScale(), 1.0f);
    }

    TEST_F(SDLWindowMetricsTest, TheSafeAreaOfAWindowWithoutInsetsIsTheWholeWindow) {
        const Rect safeArea = window->GetSafeArea();

        EXPECT_FLOAT_EQ(safeArea.x, 0.0f);
        EXPECT_FLOAT_EQ(safeArea.y, 0.0f);
        EXPECT_FLOAT_EQ(safeArea.w, static_cast<float>(window->GetWidth()));
        EXPECT_FLOAT_EQ(safeArea.h, static_cast<float>(window->GetHeight()));
    }

    TEST_F(SDLWindowMetricsTest, AfterDestroyTheMetricsAreNeutralAgain) {
        window->Destroy();

        EXPECT_FLOAT_EQ(window->GetPixelDensity(), 1.0f);
        EXPECT_FLOAT_EQ(window->GetDisplayScale(), 1.0f);
        EXPECT_TRUE(window->GetSafeArea().IsEmpty());
    }

}  // namespace BECore::Tests
