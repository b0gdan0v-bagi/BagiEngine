#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/MainWindow/WindowSimulation.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
        constexpr float kInf = std::numeric_limits<float>::infinity();

        WindowSimulation Make(int width, int height, float density = 1.0f, WindowInsets insets = {}) {
            WindowSimulation simulation;
            simulation.width = width;
            simulation.height = height;
            simulation.density = density;
            simulation.safeArea = insets;
            return simulation;
        }

        bool Accepts(const WindowSimulation& simulation) {
            return ValidateWindowSimulation(simulation).has_value();
        }

        /// The message of a refusal, empty when the simulation is accepted
        String Refusal(const WindowSimulation& simulation) {
            const auto result = ValidateWindowSimulation(simulation);
            return result ? String() : result.error();
        }

        bool Mentions(const String& message, const char* field) {
            return message.find(field) != String::npos;
        }

        /// The least an IMainWindow has to say: everything else is the default
        class PlainWindow : public IMainWindow {
        public:
            bool Initialize(StringView /*configPath*/) override {
                return true;
            }

            void Destroy() override {}

            bool IsValid() const override {
                return true;
            }

            int GetWidth() const override {
                return 320;
            }

            int GetHeight() const override {
                return 200;
            }
        };

    }  // namespace

    TEST(WindowSimulationTest, SizeIsCheckedAtItsEdges) {
        for (const int accepted : {WindowLimits::kMinSize, 100, WindowLimits::kMaxSize}) {
            EXPECT_TRUE(Accepts(Make(accepted, 100))) << "width " << accepted;
            EXPECT_TRUE(Accepts(Make(100, accepted))) << "height " << accepted;
        }
        for (const int refused : {-1, 0, WindowLimits::kMinSize - 1, WindowLimits::kMaxSize + 1}) {
            EXPECT_TRUE(Mentions(Refusal(Make(refused, 100)), "width")) << "width " << refused;
            EXPECT_TRUE(Mentions(Refusal(Make(100, refused)), "height")) << "height " << refused;
        }
    }

    TEST(WindowSimulationTest, RefusalSaysTheRange) {
        const String message = Refusal(Make(63, 100));
        EXPECT_TRUE(Mentions(message, "64")) << message;
        EXPECT_TRUE(Mentions(message, "4096")) << message;
        EXPECT_TRUE(Mentions(message, "63")) << message;
    }

    TEST(WindowSimulationTest, DensityIsCheckedAtItsEdges) {
        EXPECT_TRUE(Accepts(Make(200, 200, WindowLimits::kMinDensity)));
        EXPECT_TRUE(Accepts(Make(200, 200, 1.0f)));
        EXPECT_TRUE(Accepts(Make(200, 200, WindowLimits::kMaxDensity)));
        for (const float refused : {0.49f, 4.01f, 0.0f, -1.0f, kNaN, kInf, -kInf}) {
            EXPECT_TRUE(Mentions(Refusal(Make(200, 200, refused)), "density")) << "density " << refused;
        }
    }

    TEST(WindowSimulationTest, OutputSideIsCheckedAtItsEdge) {
        EXPECT_EQ(SimulatedOutputPixels(4096, 2.0f), 8192);
        EXPECT_TRUE(Accepts(Make(4096, 64, 2.0f)));
        EXPECT_TRUE(Accepts(Make(64, 4096, 2.0f)));
        EXPECT_TRUE(Accepts(Make(2048, 2048, 4.0f)));

        EXPECT_EQ(SimulatedOutputPixels(2731, 3.0f), 8193);
        EXPECT_TRUE(Mentions(Refusal(Make(2731, 64, 3.0f)), "width"));
        EXPECT_TRUE(Mentions(Refusal(Make(64, 2731, 3.0f)), "height"));
        EXPECT_TRUE(Mentions(Refusal(Make(2731, 64, 3.0f)), "8192"));
    }

    TEST(WindowSimulationTest, OutputPixelsRoundToTheNearest) {
        EXPECT_EQ(SimulatedOutputPixels(100, 1.0f), 100);
        EXPECT_EQ(SimulatedOutputPixels(333, 1.5f), 500);  // 499.5
        EXPECT_EQ(SimulatedOutputPixels(101, 0.5f), 51);   // 50.5
        EXPECT_EQ(SimulatedOutputPixels(100, 1.004f), 100);
    }

    TEST(WindowSimulationTest, InsetsMustBeFiniteAndNotNegative) {
        EXPECT_TRUE(Accepts(Make(200, 200, 1.0f, {0.0f, 0.0f, 0.0f, 0.0f})));
        EXPECT_TRUE(Accepts(Make(200, 200, 1.0f, {40.0f, 10.0f, 20.0f, 30.0f})));

        EXPECT_TRUE(Mentions(Refusal(Make(200, 200, 1.0f, {-1.0f, 0.0f, 0.0f, 0.0f})), "top"));
        EXPECT_TRUE(Mentions(Refusal(Make(200, 200, 1.0f, {0.0f, -0.5f, 0.0f, 0.0f})), "right"));
        EXPECT_TRUE(Mentions(Refusal(Make(200, 200, 1.0f, {0.0f, 0.0f, -1.0f, 0.0f})), "bottom"));
        EXPECT_TRUE(Mentions(Refusal(Make(200, 200, 1.0f, {0.0f, 0.0f, 0.0f, -1.0f})), "left"));
        EXPECT_TRUE(Mentions(Refusal(Make(200, 200, 1.0f, {kNaN, 0.0f, 0.0f, 0.0f})), "top"));
        EXPECT_TRUE(Mentions(Refusal(Make(200, 200, 1.0f, {0.0f, kNaN, 0.0f, 0.0f})), "right"));
        EXPECT_TRUE(Mentions(Refusal(Make(200, 200, 1.0f, {0.0f, 0.0f, kInf, 0.0f})), "bottom"));
        EXPECT_TRUE(Mentions(Refusal(Make(200, 200, 1.0f, {0.0f, 0.0f, 0.0f, kInf})), "left"));
    }

    TEST(WindowSimulationTest, InsetsNeverFillTheWindow) {
        // The widest safe area that is not empty leaves one unit
        EXPECT_TRUE(Accepts(Make(200, 100, 1.0f, {0.0f, 100.0f, 0.0f, 99.0f})));
        EXPECT_TRUE(Accepts(Make(200, 100, 1.0f, {50.0f, 0.0f, 49.0f, 0.0f})));

        EXPECT_TRUE(Mentions(Refusal(Make(200, 100, 1.0f, {0.0f, 100.0f, 0.0f, 100.0f})), "left + safeArea.right"));
        EXPECT_TRUE(Mentions(Refusal(Make(200, 100, 1.0f, {50.0f, 0.0f, 50.0f, 0.0f})), "top + safeArea.bottom"));
        EXPECT_TRUE(Mentions(Refusal(Make(200, 100, 1.0f, {0.0f, 200.0f, 0.0f, 0.0f})), "left + safeArea.right"));
        EXPECT_TRUE(Mentions(Refusal(Make(200, 100, 1.0f, {500.0f, 0.0f, 0.0f, 0.0f})), "top + safeArea.bottom"));
    }

    TEST(WindowSimulationTest, SafeRectIsTheWindowMinusTheInsets) {
        const Rect rect = SimulatedSafeRect(Make(300, 200, 2.0f, {40.0f, 10.0f, 20.0f, 30.0f}));
        EXPECT_FLOAT_EQ(rect.x, 30.0f);
        EXPECT_FLOAT_EQ(rect.y, 40.0f);
        EXPECT_FLOAT_EQ(rect.w, 260.0f);
        EXPECT_FLOAT_EQ(rect.h, 140.0f);
    }

    TEST(WindowSimulationTest, SafeRectWithoutInsetsIsTheWholeWindowInUnits) {
        const Rect rect = SimulatedSafeRect(Make(300, 200, 2.0f));
        EXPECT_FLOAT_EQ(rect.x, 0.0f);
        EXPECT_FLOAT_EQ(rect.y, 0.0f);
        EXPECT_FLOAT_EQ(rect.w, 300.0f);
        EXPECT_FLOAT_EQ(rect.h, 200.0f);
    }

    TEST(WindowSimulationTest, SimulationsCompareByEveryField) {
        const WindowSimulation base = Make(300, 200, 2.0f, {1.0f, 2.0f, 3.0f, 4.0f});
        EXPECT_EQ(base, Make(300, 200, 2.0f, {1.0f, 2.0f, 3.0f, 4.0f}));
        EXPECT_NE(base, Make(301, 200, 2.0f, {1.0f, 2.0f, 3.0f, 4.0f}));
        EXPECT_NE(base, Make(300, 200, 1.0f, {1.0f, 2.0f, 3.0f, 4.0f}));
        EXPECT_NE(base, Make(300, 200, 2.0f, {1.0f, 2.0f, 3.0f, 5.0f}));
    }

    TEST(WindowSimulationTest, AWindowIsNotHeadlessAndRefusesASimulationByDefault) {
        PlainWindow window;
        EXPECT_FALSE(window.IsHeadless());

        const auto result = window.Simulate(Make(200, 200));
        ASSERT_FALSE(result.has_value());
        EXPECT_TRUE(Mentions(result.error(), "cannot be simulated")) << result.error();

        // Nothing changed
        EXPECT_EQ(window.GetWidth(), 320);
        EXPECT_EQ(window.GetHeight(), 200);
    }

    TEST(WindowSimulationTest, CompileTimeChecks) {
        static_assert(WindowLimits::kMinSize < WindowLimits::kMaxSize);
        static_assert(WindowLimits::kMinDensity < WindowLimits::kMaxDensity);
        static_assert(WindowLimits::kMaxSize <= WindowLimits::kMaxOutputSide);
    }

}  // namespace BECore::Tests
