#include <BECore/Config/XmlConfig.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <Common/EngineEnvironment.h>
#include <CoreSDL/SDLEvents.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreSDL/SDLUtils.h>
#include <Events/SubscriptionHolder.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kWindowConfig = "config/SDLWindowConfig.xml";

        /// A window that only has a size, like a test double or a platform that knows no more
        class SizeOnlyWindow : public IMainWindow {
        public:
            SizeOnlyWindow(int width, int height) : _width(width), _height(height) {}

            bool Initialize(StringView /*configPath*/) override {
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

        /// Counts SafeAreaChangedEvent, which SDLMainWindow::Simulate() emits when the safe area moves
        class SafeAreaCounter : public SubscriptionHolder {
        public:
            SafeAreaCounter() {
                Subscribe<SDLEvents::SafeAreaChangedEvent, &SafeAreaCounter::OnChanged>(this);
            }

            int changes = 0;

        private:
            void OnChanged(const SDLEvents::SafeAreaChangedEvent&) {
                ++changes;
            }
        };

        WindowSimulation MakeSimulation(int width, int height, float density, WindowInsets safeArea = {}) {
            return WindowSimulation{.width = width, .height = height, .density = density, .safeArea = safeArea};
        }

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

    TEST_F(SDLWindowMetricsTest, TheSizeIsWhatWasSimulatedAndTheSDLWindowIsInPixels) {
        ASSERT_TRUE(window->Simulate(MakeSimulation(300, 200, 2.0f)));

        EXPECT_EQ(window->GetWidth(), 300);
        EXPECT_EQ(window->GetHeight(), 200);
        EXPECT_FLOAT_EQ(window->GetPixelDensity(), 2.0f);
        EXPECT_FLOAT_EQ(window->GetDisplayScale(), 1.0f);
        int pixelWidth = 0;
        int pixelHeight = 0;
        ASSERT_TRUE(SDL_GetWindowSizeInPixels(window->GetSDLWindow(), &pixelWidth, &pixelHeight));
        EXPECT_EQ(pixelWidth, 600);
        EXPECT_EQ(pixelHeight, 400);
    }

    TEST_F(SDLWindowMetricsTest, AFractionalOutputSizeIsRounded) {
        ASSERT_TRUE(window->Simulate(MakeSimulation(333, 100, 1.5f)));

        int pixelWidth = 0;
        int pixelHeight = 0;
        ASSERT_TRUE(SDL_GetWindowSizeInPixels(window->GetSDLWindow(), &pixelWidth, &pixelHeight));
        EXPECT_EQ(window->GetWidth(), 333);
        EXPECT_EQ(pixelWidth, 500);
        EXPECT_EQ(pixelHeight, 150);
    }

    TEST_F(SDLWindowMetricsTest, TheSimulatedSafeAreaIsTheWindowLessTheInsets) {
        ASSERT_TRUE(window->Simulate(MakeSimulation(400, 300, 1.0f, WindowInsets{.top = 40.0f, .right = 10.0f, .bottom = 20.0f, .left = 30.0f})));

        const Rect safeArea = window->GetSafeArea();
        EXPECT_FLOAT_EQ(safeArea.x, 30.0f);
        EXPECT_FLOAT_EQ(safeArea.y, 40.0f);
        EXPECT_FLOAT_EQ(safeArea.w, 360.0f);
        EXPECT_FLOAT_EQ(safeArea.h, 240.0f);

        ASSERT_TRUE(window->Simulate(MakeSimulation(400, 300, 1.0f)));
        EXPECT_FLOAT_EQ(window->GetSafeArea().w, 400.0f);
        EXPECT_FLOAT_EQ(window->GetSafeArea().h, 300.0f);
    }

    TEST_F(SDLWindowMetricsTest, TheSafeAreaEventFiresOncePerChange) {
        SafeAreaCounter counter;
        const WindowInsets notch{.top = 40.0f};

        ASSERT_TRUE(window->Simulate(MakeSimulation(400, 300, 1.0f, notch)));
        EXPECT_EQ(counter.changes, 1);

        ASSERT_TRUE(window->Simulate(MakeSimulation(400, 300, 1.0f, notch)));
        EXPECT_EQ(counter.changes, 1);

        ASSERT_TRUE(window->Simulate(MakeSimulation(400, 300, 1.0f)));
        EXPECT_EQ(counter.changes, 2);
    }

    TEST_F(SDLWindowMetricsTest, ASimulationBeforeInitializeGivesTheStartSize) {
        window->Destroy();
        window->SetHeadlessSimulation(480, 320, 2.0f);
        ASSERT_TRUE(window->Initialize(kWindowConfig));

        int pixelWidth = 0;
        int pixelHeight = 0;
        ASSERT_TRUE(SDL_GetWindowSizeInPixels(window->GetSDLWindow(), &pixelWidth, &pixelHeight));
        EXPECT_EQ(window->GetWidth(), 480);
        EXPECT_EQ(window->GetHeight(), 320);
        EXPECT_FLOAT_EQ(window->GetPixelDensity(), 2.0f);
        EXPECT_EQ(pixelWidth, 960);
        EXPECT_EQ(pixelHeight, 640);
    }

    TEST_F(SDLWindowMetricsTest, ASecondWindowStartsFromTheConfigAgainAfterDestroy) {
        ASSERT_TRUE(window->Simulate(MakeSimulation(300, 200, 2.0f, WindowInsets{.top = 40.0f})));
        window->Destroy();
        ASSERT_TRUE(window->Initialize(kWindowConfig));

        EXPECT_NE(window->GetWidth(), 300);
        EXPECT_FLOAT_EQ(window->GetPixelDensity(), 1.0f);
        EXPECT_FLOAT_EQ(window->GetSafeArea().y, 0.0f);
    }

    TEST_F(SDLWindowMetricsTest, SimulateAfterDestroyIsAnError) {
        window->Destroy();

        EXPECT_FALSE(window->Simulate(MakeSimulation(300, 200, 1.0f)));
    }

    TEST(WindowMetricsTest, AWindowThatIsNotHeadlessCannotBeSimulated) {
        SDLMainWindow window;

        EXPECT_FALSE(window.IsHeadless());
        const auto simulated = window.Simulate(MakeSimulation(300, 200, 1.0f));
        ASSERT_FALSE(simulated);
        EXPECT_NE(simulated.error().find("headless"), String::npos);
    }

    TEST_F(SDLWindowMetricsTest, TheHeadlessWindowStartsAtTheSizeOfTheConfig) {
        auto config = XmlConfig::Create();
        ASSERT_TRUE(config.LoadFromFile(CoreManager::GetFileSystem().ResolvePath(kWindowConfig)));
        const XmlNode node = config.GetRoot().GetChild("window");

        EXPECT_EQ(window->GetWidth(), node.ParseAttribute<int>("width").value());
        EXPECT_EQ(window->GetHeight(), node.ParseAttribute<int>("height").value());
    }

    TEST_F(SDLWindowMetricsTest, AfterDestroyTheSizeIsNeutralAgain) {
        ASSERT_TRUE(window->Simulate(MakeSimulation(300, 200, 2.0f)));
        window->Destroy();

        EXPECT_EQ(window->GetWidth(), 0);
        EXPECT_EQ(window->GetHeight(), 0);
    }

    TEST(WindowMetricsTest, TheShippedWindowConfigAsksForTheHighDensityMode) {
        EngineEnvironment::EnsureCoreServices();
        auto config = XmlConfig::Create();
        ASSERT_TRUE(config.LoadFromFile(CoreManager::GetFileSystem().ResolvePath(kWindowConfig)));
        const auto flagsText = config.GetRoot().GetChild("window").GetAttribute("windowFlags");
        ASSERT_TRUE(flagsText.has_value());

        const SDL_WindowFlags flags = SDLUtils::ParseWindowFlags(*flagsText);

        EXPECT_NE(flags & SDL_WINDOW_HIGH_PIXEL_DENSITY, 0u);
        EXPECT_NE(flags & SDL_WINDOW_RESIZABLE, 0u);
    }

    TEST_F(SDLWindowMetricsTest, AfterDestroyTheMetricsAreNeutralAgain) {
        ASSERT_TRUE(window->Simulate(MakeSimulation(300, 200, 2.0f, WindowInsets{.top = 40.0f})));
        window->Destroy();

        EXPECT_FLOAT_EQ(window->GetPixelDensity(), 1.0f);
        EXPECT_FLOAT_EQ(window->GetDisplayScale(), 1.0f);
        EXPECT_TRUE(window->GetSafeArea().IsEmpty());
    }

}  // namespace BECore::Tests
