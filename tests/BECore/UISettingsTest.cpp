#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UISettings.h>
#include <Common/EngineEnvironment.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        UISettings Parse(const char* xml) {
            auto config = XmlConfig::Create();
            EXPECT_TRUE(config.LoadFromString(xml));
            return UISettings::FromConfig(config.GetRoot());
        }

        /// A window with chosen metrics, in window units
        class MetricsWindow : public IMainWindow {
        public:
            bool Initialize(eastl::string_view /*configPath*/) override {
                return true;
            }
            void Destroy() override {}
            bool IsValid() const override {
                return true;
            }
            int GetWidth() const override {
                return 1000;
            }
            int GetHeight() const override {
                return 500;
            }
            float GetPixelDensity() const override {
                return density;
            }
            float GetDisplayScale() const override {
                return displayScale;
            }
            Rect GetSafeArea() const override {
                return safeArea;
            }

            float density = 2.0f;
            float displayScale = 1.5f;
            Rect safeArea{0.0f, 20.0f, 1000.0f, 480.0f};
        };

    }  // namespace

    class UISettingsTest : public ::testing::Test {
    public:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST(UISettingsPureTest, AnEmptyNodeGivesTheDefaults) {
        const UISettings settings = UISettings::FromConfig(XmlNode{});

        EXPECT_FLOAT_EQ(settings.userScale, 1.0f);
        EXPECT_FLOAT_EQ(settings.minTouchSize, 32.0f);
        EXPECT_FLOAT_EQ(settings.hotReloadInterval, 0.5f);
    }

    TEST(UISettingsPureTest, AttributesOverrideTheDefaults) {
        const UISettings settings = Parse(R"(<UIConfig userScale="1.5" minTouchSize="44" hotReloadInterval="2"/>)");

        EXPECT_FLOAT_EQ(settings.userScale, 1.5f);
        EXPECT_FLOAT_EQ(settings.minTouchSize, 44.0f);
        EXPECT_FLOAT_EQ(settings.hotReloadInterval, 2.0f);
    }

    TEST(UISettingsPureTest, ZeroTurnsHotReloadOffAndANegativeIntervalIsZero) {
        EXPECT_FLOAT_EQ(Parse(R"(<UIConfig hotReloadInterval="0"/>)").hotReloadInterval, 0.0f);
        EXPECT_FLOAT_EQ(Parse(R"(<UIConfig hotReloadInterval="-3"/>)").hotReloadInterval, 0.0f);
    }

    TEST(UISettingsPureTest, TheUserScaleIsKeptInsideItsRange) {
        EXPECT_FLOAT_EQ(Parse(R"(<UIConfig userScale="0.01"/>)").userScale, UISettings::kMinUserScale);
        EXPECT_FLOAT_EQ(Parse(R"(<UIConfig userScale="50"/>)").userScale, UISettings::kMaxUserScale);
        EXPECT_FLOAT_EQ(Parse(R"(<UIConfig minTouchSize="-4"/>)").minTouchSize, 0.0f);
    }

    TEST(UISettingsPureTest, AMalformedAttributeKeepsTheDefault) {
        const UISettings settings = Parse(R"(<UIConfig userScale="big" minTouchSize="small" hotReloadInterval="often"/>)");

        EXPECT_FLOAT_EQ(settings.userScale, 1.0f);
        EXPECT_FLOAT_EQ(settings.minTouchSize, 32.0f);
        EXPECT_FLOAT_EQ(settings.hotReloadInterval, 0.5f);
    }

    TEST_F(UISettingsTest, TheShippedConfigGivesTheDefaults) {
        const UISettings& settings = CoreManager::GetUIEnvironment().GetSettings();

        EXPECT_FLOAT_EQ(settings.userScale, 1.0f);
        EXPECT_FLOAT_EQ(settings.minTouchSize, 32.0f);
        EXPECT_FLOAT_EQ(settings.hotReloadInterval, 0.5f);
    }

    TEST(UIEnvironmentTest, WithoutAWindowTheMetricsAreNeutral) {
        const UIEnvironment environment;

        const UILayoutContext context = environment.MakeContext(nullptr, 1280.0f, 720.0f);

        EXPECT_FLOAT_EQ(context.outputWidth, 1280.0f);
        EXPECT_FLOAT_EQ(context.outputHeight, 720.0f);
        EXPECT_FLOAT_EQ(context.pixelDensity, 1.0f);
        EXPECT_FLOAT_EQ(context.displayScale, 1.0f);
        EXPECT_TRUE(context.safeArea.IsEmpty());
        EXPECT_FLOAT_EQ(context.userScale, 1.0f);
    }

    TEST(UIEnvironmentTest, TheWindowMetricsComeInOutputPixels) {
        const UIEnvironment environment;
        const MetricsWindow window;

        const UILayoutContext context = environment.MakeContext(&window, 2000.0f, 1000.0f);

        EXPECT_FLOAT_EQ(context.pixelDensity, 2.0f);
        EXPECT_FLOAT_EQ(context.displayScale, 1.5f);
        // The window's safe area is in window units: twice that in pixels at density 2
        EXPECT_FLOAT_EQ(context.safeArea.y, 40.0f);
        EXPECT_FLOAT_EQ(context.safeArea.w, 2000.0f);
        EXPECT_FLOAT_EQ(context.safeArea.h, 960.0f);
    }

    TEST(UIEnvironmentTest, AnOverrideReplacesTheWindowsSafeAreaAndClearsAgain) {
        UIEnvironment environment;
        const MetricsWindow window;

        environment.SetSafeAreaOverride(Rect{10.0f, 20.0f, 300.0f, 200.0f});
        UILayoutContext context = environment.MakeContext(&window, 2000.0f, 1000.0f);
        EXPECT_FLOAT_EQ(context.safeArea.x, 10.0f);
        EXPECT_FLOAT_EQ(context.safeArea.w, 300.0f);  // as given, in output pixels

        environment.SetSafeAreaOverride(std::nullopt);
        context = environment.MakeContext(&window, 2000.0f, 1000.0f);
        EXPECT_FLOAT_EQ(context.safeArea.w, 2000.0f);
    }

    TEST(UIEnvironmentTest, TheUserScaleEntersTheContextAndIsClamped) {
        UIEnvironment environment;

        environment.SetUserScale(1.5f);
        EXPECT_FLOAT_EQ(environment.MakeContext(nullptr, 100.0f, 100.0f).userScale, 1.5f);

        environment.SetUserScale(100.0f);
        EXPECT_FLOAT_EQ(environment.GetSettings().userScale, UISettings::kMaxUserScale);

        UISettings settings;
        settings.userScale = 0.0f;
        environment.SetSettings(settings);
        EXPECT_FLOAT_EQ(environment.GetSettings().userScale, UISettings::kMinUserScale);
    }

}  // namespace BECore::Tests
