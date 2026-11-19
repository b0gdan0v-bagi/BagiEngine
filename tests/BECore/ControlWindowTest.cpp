#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/ControlWindow.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/UI/UIEnvironment.h>
#include <Common/ControlTestSupport.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// A window that keeps what it was told, like a headless SDL window would, and records every Simulate() it got
        class FakeWindow : public IMainWindow {
        public:
            explicit FakeWindow(bool headless) : _headless(headless) {
                _current.width = 800;
                _current.height = 600;
            }

            bool Initialize(StringView /*configPath*/) override {
                return true;
            }

            void Destroy() override {}

            bool IsValid() const override {
                return true;
            }

            int GetWidth() const override {
                return _current.width;
            }

            int GetHeight() const override {
                return _current.height;
            }

            float GetPixelDensity() const override {
                return _current.density;
            }

            float GetDisplayScale() const override {
                return 1.0f;
            }

            Rect GetSafeArea() const override {
                return SimulatedSafeRect(_current);
            }

            bool IsHeadless() const override {
                return _headless;
            }

            std::expected<void, String> Simulate(const WindowSimulation& simulation) override {
                if (!_headless) {
                    return IMainWindow::Simulate(simulation);
                }
                if (!_refusal.empty()) {
                    return std::unexpected(_refusal);
                }
                _simulations.push_back(simulation);
                _current = simulation;
                return {};
            }

            void SetCurrent(const WindowSimulation& simulation) {
                _current = simulation;
            }

            void RefuseWith(String message) {
                _refusal = std::move(message);
            }

            [[nodiscard]] const Vector<WindowSimulation>& Simulations() const {
                return _simulations;
            }

        private:
            bool _headless = false;
            WindowSimulation _current;
            String _refusal;
            Vector<WindowSimulation> _simulations;
        };

        Json SizeOf(int64_t width, int64_t height) {
            Json params = Json::object();
            params["width"] = width;
            params["height"] = height;
            return params;
        }

        Json SafeAreaOf(const Json& top, const Json& right, const Json& bottom, const Json& left) {
            Json safeArea = Json::object();
            safeArea["top"] = top;
            safeArea["right"] = right;
            safeArea["bottom"] = bottom;
            safeArea["left"] = left;
            return safeArea;
        }

        constexpr float kStartUserScale = 1.25f;

    }  // namespace

    /// A service with the window control over a fake window and renderer in the shared CoreManager, which the fixture puts back
    class ControlWindowTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _previousWindow = CoreManager::GetMainWindow();
            _previousRenderer = CoreManager::GetRenderer();
            _previousSettings = CoreManager::GetUIEnvironment().GetSettings();

            UISettings settings = _previousSettings;
            settings.userScale = kStartUserScale;
            CoreManager::GetUIEnvironment().SetSettings(settings);

            renderer = New<RecordingRenderer>();
            renderer->width = 800.0f;
            renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(renderer);
            UseWindow(true);
            RegisterControlBuiltins(service);
        }

        void TearDown() override {
            CoreManager::GetMainWindowManager().SetMainWindow(_previousWindow);
            CoreManager::GetRendererManager().SetRenderer(_previousRenderer);
            CoreManager::GetUIEnvironment().SetSettings(_previousSettings);
        }

        void UseWindow(bool headless) {
            window = New<FakeWindow>(headless);
            CoreManager::GetMainWindowManager().SetMainWindow(window);
        }

        /// Posts the request and runs the frame it arrives in; true when the answer came with it, so a deferred call is not
        bool Send(StringView method, const Json& params = Json()) {
            const size_t before = responses.size();
            service.Post(MakeRequest(static_cast<int64_t>(before) + 1, method, params), [this](String reply) {
                auto parsed = ParseJson(reply);
                responses.push_back(parsed ? std::move(*parsed) : Json());
            });
            service.Drain();
            return responses.size() > before;
        }

        /// Ends the frame that is running, which completes a call deferred by one frame
        void EndFrame() {
            service.EndFrame();
        }

        /// A call that waits one frame; its answer is in `Last()` afterwards
        void Call(StringView method, const Json& params = Json()) {
            if (!Send(method, params)) {
                EndFrame();
            }
        }

        [[nodiscard]] const Json& Last() const {
            static const Json kNone = Json::object();
            return responses.empty() ? kNone : responses.back();
        }

        [[nodiscard]] bool LastIsOk() const {
            return GetJsonBool(Last(), "ok").value_or(false);
        }

        [[nodiscard]] String LastErrorCode() const {
            return StringOf(FindJsonMember(Last(), "error"), "code");
        }

        [[nodiscard]] String LastErrorMessage() const {
            return StringOf(FindJsonMember(Last(), "error"), "message");
        }

        [[nodiscard]] const Json* LastResult() const {
            return FindJsonMember(Last(), "result");
        }

        [[nodiscard]] static float UserScale() {
            return CoreManager::GetUIEnvironment().GetSettings().userScale;
        }

        ControlService service;
        IntrusivePtr<FakeWindow> window;
        IntrusivePtr<RecordingRenderer> renderer;
        Vector<Json> responses;

    private:
        IntrusivePtr<IMainWindow> _previousWindow;
        IntrusivePtr<IRenderer> _previousRenderer;
        UISettings _previousSettings;
    };

    TEST_F(ControlWindowTest, AValidCallGivesTheWindowTheMergedValuesAndAnswersAfterOneFrame) {
        Json params = SizeOf(480, 320);
        params["density"] = 2.0;
        params["userScale"] = 1.5;
        params["safeArea"] = SafeAreaOf(40, 10, 20, 30);

        EXPECT_FALSE(Send("set_window", params));
        EXPECT_TRUE(responses.empty());
        ASSERT_EQ(window->Simulations().size(), 1u);
        const WindowSimulation& applied = window->Simulations().front();
        EXPECT_EQ(applied.width, 480);
        EXPECT_EQ(applied.height, 320);
        EXPECT_FLOAT_EQ(applied.density, 2.0f);
        EXPECT_EQ(applied.safeArea, (WindowInsets{40.0f, 10.0f, 20.0f, 30.0f}));
        EXPECT_FLOAT_EQ(UserScale(), 1.5f);

        EndFrame();
        ASSERT_EQ(responses.size(), 1u);
        ASSERT_TRUE(LastIsOk());
        const Json* result = LastResult();
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(NumberOf(result, "width"), 480.0);
        EXPECT_EQ(NumberOf(result, "height"), 320.0);
        EXPECT_EQ(NumberOf(result, "density"), 2.0);
        EXPECT_EQ(NumberOf(result, "userScale"), 1.5);
        const Json* safeArea = FindJsonMember(*result, "safeArea");
        EXPECT_EQ(NumberOf(safeArea, "top"), 40.0);
        EXPECT_EQ(NumberOf(safeArea, "right"), 10.0);
        EXPECT_EQ(NumberOf(safeArea, "bottom"), 20.0);
        EXPECT_EQ(NumberOf(safeArea, "left"), 30.0);
        EXPECT_TRUE(GetJsonBool(*result, "headless").value_or(false));
    }

    TEST_F(ControlWindowTest, LeftOutValuesKeepTheirCurrentValue) {
        WindowSimulation current;
        current.width = 600;
        current.height = 400;
        current.density = 2.0f;
        current.safeArea = {10.0f, 20.0f, 30.0f, 40.0f};
        window->SetCurrent(current);

        Call("set_window", SizeOf(500, 300));
        ASSERT_TRUE(LastIsOk()) << LastErrorMessage().c_str();
        ASSERT_EQ(window->Simulations().size(), 1u);
        const WindowSimulation& applied = window->Simulations().front();
        EXPECT_EQ(applied.width, 500);
        EXPECT_EQ(applied.height, 300);
        EXPECT_FLOAT_EQ(applied.density, 2.0f);
        EXPECT_EQ(applied.safeArea, current.safeArea);
        EXPECT_FLOAT_EQ(UserScale(), kStartUserScale);
    }

    TEST_F(ControlWindowTest, AllZeroInsetsClearTheSafeArea) {
        WindowSimulation current;
        current.width = 600;
        current.height = 400;
        current.safeArea = {10.0f, 20.0f, 30.0f, 40.0f};
        window->SetCurrent(current);

        Json params = SizeOf(600, 400);
        params["safeArea"] = SafeAreaOf(0, 0, 0, 0);
        Call("set_window", params);
        ASSERT_TRUE(LastIsOk()) << LastErrorMessage().c_str();
        EXPECT_EQ(window->Simulations().back().safeArea, WindowInsets{});
    }

    TEST_F(ControlWindowTest, EachBoundIsInvalidParamsAndAppliesNothing) {
        struct Bad {
            const char* what;
            Json params;
            const char* field;
        };
        Vector<Bad> bad;
        bad.push_back({"width 0", SizeOf(0, 300), "width"});
        bad.push_back({"width 63", SizeOf(63, 300), "width"});
        bad.push_back({"width 4097", SizeOf(4097, 300), "width"});
        bad.push_back({"height 4097", SizeOf(300, 4097), "height"});
        bad.push_back({"a width that does not fit an int", SizeOf(int64_t{1} << 40, 300), "width"});
        {
            Json params = SizeOf(300, 300);
            params["density"] = 0.49;
            bad.push_back({"density 0.49", params, "density"});
            params["density"] = 4.01;
            bad.push_back({"density 4.01", params, "density"});
        }
        {
            Json params = SizeOf(4096, 300);
            params["density"] = 3.0;
            bad.push_back({"an output of 12288 pixels", params, "width"});
        }
        {
            Json params = SizeOf(300, 300);
            params["userScale"] = 0.49;
            bad.push_back({"user scale 0.49", params, "userScale"});
            params["userScale"] = 3.01;
            bad.push_back({"user scale 3.01", params, "userScale"});
        }
        {
            Json params = SizeOf(300, 300);
            params["safeArea"] = SafeAreaOf(-1, 0, 0, 0);
            bad.push_back({"a negative inset", params, "safeArea.top"});
            params["safeArea"] = SafeAreaOf(0, 150, 0, 150);
            bad.push_back({"insets that fill the width", params, "safeArea.left"});
            params["safeArea"] = SafeAreaOf(150, 0, 150, 0);
            bad.push_back({"insets that fill the height", params, "safeArea.top"});
        }

        for (const Bad& entry : bad) {
            // A user scale in range would be applied if the window part were the only one checked late
            Json params = entry.params;
            if (FindJsonMember(params, "userScale") == nullptr) {
                params["userScale"] = 2.0;
            }
            Call("set_window", params);
            EXPECT_FALSE(LastIsOk()) << entry.what;
            EXPECT_EQ(LastErrorCode(), "invalid_params") << entry.what;
            EXPECT_NE(LastErrorMessage().find(entry.field), String::npos) << entry.what << ": " << LastErrorMessage().c_str();
            EXPECT_TRUE(window->Simulations().empty()) << entry.what;
            EXPECT_FLOAT_EQ(UserScale(), kStartUserScale) << entry.what;
        }
    }

    TEST_F(ControlWindowTest, ABadShapeIsInvalidParams) {
        Json fractional = Json::object();
        fractional["width"] = 480.5;
        fractional["height"] = 300;
        Json textual = SizeOf(480, 300);
        textual["width"] = "480";
        Json badDensity = SizeOf(480, 300);
        badDensity["density"] = "2";
        Json badScale = SizeOf(480, 300);
        badScale["userScale"] = true;
        Json notAnObject = SizeOf(480, 300);
        notAnObject["safeArea"] = 40;
        Json missingMember = SizeOf(480, 300);
        missingMember["safeArea"] = SafeAreaOf(1, 2, 3, 4);
        missingMember["safeArea"].erase("left");
        Json textualInset = SizeOf(480, 300);
        textualInset["safeArea"] = SafeAreaOf(1, 2, 3, "4");

        const Json requests[] = {Json::object(), fractional, textual, badDensity, badScale, notAnObject, missingMember, textualInset};
        for (const Json& params : requests) {
            Call("set_window", params);
            EXPECT_EQ(LastErrorCode(), "invalid_params") << DumpJson(params).c_str();
            EXPECT_TRUE(window->Simulations().empty()) << DumpJson(params).c_str();
        }

        Call("set_window", missingMember);
        EXPECT_NE(LastErrorMessage().find("left"), String::npos) << LastErrorMessage().c_str();
        EXPECT_FLOAT_EQ(UserScale(), kStartUserScale);
    }

    TEST_F(ControlWindowTest, AHeightThatIsMissingIsInvalidParams) {
        Json params = Json::object();
        params["width"] = 480;
        Call("set_window", params);
        EXPECT_EQ(LastErrorCode(), "invalid_params");
        EXPECT_NE(LastErrorMessage().find("height"), String::npos) << LastErrorMessage().c_str();
    }

    TEST_F(ControlWindowTest, AWindowThatRefusesIsInvalidParamsAndKeepsTheUserScale) {
        window->RefuseWith("the driver said no");
        Json params = SizeOf(480, 300);
        params["userScale"] = 2.0;
        Call("set_window", params);
        EXPECT_EQ(LastErrorCode(), "invalid_params");
        EXPECT_NE(LastErrorMessage().find("the driver said no"), String::npos) << LastErrorMessage().c_str();
        EXPECT_FLOAT_EQ(UserScale(), kStartUserScale);
    }

    TEST_F(ControlWindowTest, ARealWindowIsNotHeadlessAndNothingChanges) {
        UseWindow(false);
        Json params = SizeOf(480, 300);
        params["userScale"] = 2.0;
        Call("set_window", params);
        EXPECT_EQ(LastErrorCode(), "not_headless");
        EXPECT_NE(LastErrorMessage().find("headless"), String::npos) << LastErrorMessage().c_str();
        EXPECT_TRUE(window->Simulations().empty());
        EXPECT_FLOAT_EQ(UserScale(), kStartUserScale);
    }

    TEST_F(ControlWindowTest, NoWindowIsNotHeadless) {
        CoreManager::GetMainWindowManager().SetMainWindow({});
        Call("set_window", SizeOf(480, 300));
        EXPECT_EQ(LastErrorCode(), "not_headless");
        EXPECT_FLOAT_EQ(UserScale(), kStartUserScale);
    }

    TEST_F(ControlWindowTest, StatusHasTheWindow) {
        WindowSimulation current;
        current.width = 480;
        current.height = 320;
        current.density = 2.0f;
        current.safeArea = {40.0f, 10.0f, 20.0f, 30.0f};
        window->SetCurrent(current);
        renderer->width = 960.0f;
        renderer->height = 640.0f;

        ASSERT_TRUE(Send("status"));
        ASSERT_TRUE(LastIsOk());
        const Json* status = LastResult();
        ASSERT_NE(status, nullptr);
        const Json* shown = FindJsonMember(*status, "window");
        ASSERT_NE(shown, nullptr);
        EXPECT_EQ(NumberOf(shown, "width"), 480.0);
        EXPECT_EQ(NumberOf(shown, "height"), 320.0);
        EXPECT_EQ(NumberOf(shown, "density"), 2.0);
        EXPECT_EQ(NumberOf(shown, "displayScale"), 1.0);
        EXPECT_EQ(NumberOf(shown, "outputWidth"), 960.0);
        EXPECT_EQ(NumberOf(shown, "outputHeight"), 640.0);
        EXPECT_EQ(NumberOf(shown, "userScale"), static_cast<double>(kStartUserScale));
        EXPECT_TRUE(GetJsonBool(*shown, "headless").value_or(false));
        const Json* safeArea = FindJsonMember(*shown, "safeArea");
        EXPECT_EQ(NumberOf(safeArea, "top"), 40.0);
        EXPECT_EQ(NumberOf(safeArea, "right"), 10.0);
        EXPECT_EQ(NumberOf(safeArea, "bottom"), 20.0);
        EXPECT_EQ(NumberOf(safeArea, "left"), 30.0);
    }

    TEST_F(ControlWindowTest, StatusTellsARealWindowToo) {
        UseWindow(false);
        ASSERT_TRUE(Send("status"));
        const Json* shown = FindJsonMember(*LastResult(), "window");
        ASSERT_NE(shown, nullptr);
        EXPECT_EQ(NumberOf(shown, "width"), 800.0);
        EXPECT_FALSE(GetJsonBool(*shown, "headless").value_or(true));
    }

    TEST_F(ControlWindowTest, StatusWithoutAWindowOrRendererKeepsTheShape) {
        CoreManager::GetMainWindowManager().SetMainWindow({});
        CoreManager::GetRendererManager().SetRenderer({});
        ASSERT_TRUE(Send("status"));
        const Json* shown = FindJsonMember(*LastResult(), "window");
        ASSERT_NE(shown, nullptr);
        EXPECT_EQ(NumberOf(shown, "width"), 0.0);
        EXPECT_EQ(NumberOf(shown, "outputWidth"), 0.0);
        EXPECT_EQ(NumberOf(shown, "density"), 1.0);
        EXPECT_FALSE(GetJsonBool(*shown, "headless").value_or(true));
        EXPECT_NE(FindJsonMember(*shown, "safeArea"), nullptr);
    }

    TEST_F(ControlWindowTest, ListMethodsListsSetWindowWithItsParameters) {
        ASSERT_TRUE(Send("list_methods"));
        const Json* methods = FindJsonMember(*LastResult(), "methods");
        ASSERT_NE(methods, nullptr);
        ASSERT_TRUE(methods->is_array());

        const Json* found = nullptr;
        for (const Json& method : *methods) {
            if (StringOf(&method, "name") == "set_window") {
                found = &method;
            }
        }
        ASSERT_NE(found, nullptr);
        EXPECT_FALSE(StringOf(found, "summary").empty());
        const Json* params = FindJsonMember(*found, "params");
        ASSERT_NE(params, nullptr);
        Vector<String> names;
        Vector<String> required;
        for (const Json& param : *params) {
            names.push_back(StringOf(&param, "name"));
            if (GetJsonBool(param, "required").value_or(false)) {
                required.push_back(StringOf(&param, "name"));
            }
        }
        EXPECT_EQ(names, (Vector<String>{"width", "height", "density", "userScale", "safeArea"}));
        EXPECT_EQ(required, (Vector<String>{"width", "height"}));
    }

    TEST_F(ControlWindowTest, CompileTimeChecks) {
        static_assert(WindowControlErrors::kNotHeadless == "not_headless");
    }

}  // namespace BECore::Tests
