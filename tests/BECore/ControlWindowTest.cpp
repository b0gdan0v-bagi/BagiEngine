#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/ControlWindow.h>
#include <BECore/Control/ControlWindowStatus.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/MainWindow/MainWindowManager.h>
#include <BECore/Renderer/RendererManager.h>
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

        /// The params of `set_window` with a size and @p more, a text of further members that starts with a comma
        String SizeOf(int64_t width, int64_t height, StringView more = {}) {
            return Format(R"({{"height":{},"width":{}{}}})", height, width, more);
        }

        String SafeAreaOf(int64_t top, int64_t right, int64_t bottom, int64_t left) {
            return Format(R"({{"bottom":{},"left":{},"right":{},"top":{}}})", bottom, left, right, top);
        }

        /// The members after the size: `,"safeArea":<insets>`
        String WithSafeArea(const String& insets) {
            return Format(R"(,"safeArea":{})", insets);
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
        bool Send(StringView method, const String& params = String()) {
            const size_t before = responses.size();
            service.Post(MakeRequestText(static_cast<int64_t>(before) + 1, method, StringView(params.c_str(), params.size())),
                         [this](String reply) { responses.emplace_back(StringView(reply.c_str(), reply.size())); });
            service.Drain();
            return responses.size() > before;
        }

        /// Ends the frame that is running, which completes a call deferred by one frame
        void EndFrame() {
            service.EndFrame();
        }

        /// A call that waits one frame; its answer is in `Last()` afterwards
        void Call(StringView method, const String& params = String()) {
            if (!Send(method, params)) {
                EndFrame();
            }
        }

        [[nodiscard]] const ControlReply& Last() const {
            static const ControlReply kNone;
            return responses.empty() ? kNone : responses.back();
        }

        [[nodiscard]] bool LastIsOk() const {
            return Last().IsOk();
        }

        [[nodiscard]] String LastErrorCode() const {
            return Last().ErrorCode();
        }

        [[nodiscard]] String LastErrorMessage() const {
            return Last().ErrorMessage();
        }

        [[nodiscard]] JsonView LastResult() const {
            return Last().Result();
        }

        [[nodiscard]] static float UserScale() {
            return CoreManager::GetUIEnvironment().GetSettings().userScale;
        }

        ControlService service;
        IntrusivePtr<FakeWindow> window;
        IntrusivePtr<RecordingRenderer> renderer;
        Vector<ControlReply> responses;

    private:
        IntrusivePtr<IMainWindow> _previousWindow;
        IntrusivePtr<IRenderer> _previousRenderer;
        UISettings _previousSettings;
    };

    TEST_F(ControlWindowTest, AValidCallGivesTheWindowTheMergedValuesAndAnswersAfterOneFrame) {
        const String params = SizeOf(480, 320, Format(R"(,"density":2.0,"safeArea":{},"userScale":1.5)", SafeAreaOf(40, 10, 20, 30)));

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
        const JsonView result = LastResult();
        ASSERT_TRUE(result.IsObject());
        EXPECT_EQ(NumberOf(result, "width"), 480.0);
        EXPECT_EQ(NumberOf(result, "height"), 320.0);
        EXPECT_EQ(NumberOf(result, "density"), 2.0);
        EXPECT_EQ(NumberOf(result, "userScale"), 1.5);
        const JsonView safeArea = MemberOf(result, "safeArea");
        EXPECT_EQ(NumberOf(safeArea, "top"), 40.0);
        EXPECT_EQ(NumberOf(safeArea, "right"), 10.0);
        EXPECT_EQ(NumberOf(safeArea, "bottom"), 20.0);
        EXPECT_EQ(NumberOf(safeArea, "left"), 30.0);
        EXPECT_TRUE(BoolOf(result, "headless").value_or(false));
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

        Call("set_window", SizeOf(600, 400, WithSafeArea(SafeAreaOf(0, 0, 0, 0))));
        ASSERT_TRUE(LastIsOk()) << LastErrorMessage().c_str();
        EXPECT_EQ(window->Simulations().back().safeArea, WindowInsets{});
    }

    TEST_F(ControlWindowTest, EachBoundIsInvalidParamsAndAppliesNothing) {
        struct Bad {
            const char* what;
            String params;
            const char* field;
        };
        Vector<Bad> bad;
        bad.push_back({"width 0", SizeOf(0, 300), "width"});
        bad.push_back({"width 63", SizeOf(63, 300), "width"});
        bad.push_back({"width 4097", SizeOf(4097, 300), "width"});
        bad.push_back({"height 4097", SizeOf(300, 4097), "height"});
        bad.push_back({"a width that does not fit an int", SizeOf(int64_t{1} << 40, 300), "width"});
        bad.push_back({"density 0.49", SizeOf(300, 300, R"(,"density":0.49)"), "density"});
        bad.push_back({"density 4.01", SizeOf(300, 300, R"(,"density":4.01)"), "density"});
        bad.push_back({"an output of 12288 pixels", SizeOf(4096, 300, R"(,"density":3.0)"), "width"});
        bad.push_back({"user scale 0.49", SizeOf(300, 300, R"(,"userScale":0.49)"), "userScale"});
        bad.push_back({"user scale 3.01", SizeOf(300, 300, R"(,"userScale":3.01)"), "userScale"});
        bad.push_back({"a negative inset", SizeOf(300, 300, WithSafeArea(SafeAreaOf(-1, 0, 0, 0))), "safeArea.top"});
        bad.push_back({"insets that fill the width", SizeOf(300, 300, WithSafeArea(SafeAreaOf(0, 150, 0, 150))), "safeArea.left"});
        bad.push_back({"insets that fill the height", SizeOf(300, 300, WithSafeArea(SafeAreaOf(150, 0, 150, 0))), "safeArea.top"});

        for (const Bad& entry : bad) {
            // A user scale in range would be applied if the window part were the only one checked late
            String params = entry.params;
            if (params.find("userScale") == String::npos) {
                params.insert(params.size() - 1, R"(,"userScale":2.0)");
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
        const String missingMember = SizeOf(480, 300, R"(,"safeArea":{"bottom":3,"right":2,"top":1})");
        const String requests[] = {R"({})",
                                   R"({"height":300,"width":480.5})",
                                   R"({"height":300,"width":"480"})",
                                   SizeOf(480, 300, R"(,"density":"2")"),
                                   SizeOf(480, 300, R"(,"userScale":true)"),
                                   SizeOf(480, 300, R"(,"safeArea":40)"),
                                   missingMember,
                                   SizeOf(480, 300, R"(,"safeArea":{"bottom":3,"left":"4","right":2,"top":1})")};
        for (const String& params : requests) {
            Call("set_window", params);
            EXPECT_EQ(LastErrorCode(), "invalid_params") << params.c_str();
            EXPECT_TRUE(window->Simulations().empty()) << params.c_str();
        }

        Call("set_window", missingMember);
        EXPECT_NE(LastErrorMessage().find("left"), String::npos) << LastErrorMessage().c_str();
        EXPECT_FLOAT_EQ(UserScale(), kStartUserScale);
    }

    TEST_F(ControlWindowTest, AnInsetPastADoubleIsInvalidParamsNotAParseError) {
        service.Post(R"({"id":1,"method":"set_window","params":{"width":480,"height":300,"safeArea":{"top":1e999,"right":0,"bottom":0,"left":0}}})",
                     [this](String reply) { responses.emplace_back(StringView(reply.c_str(), reply.size())); });
        service.Drain();

        EXPECT_EQ(LastErrorCode(), "invalid_params");
        EXPECT_NE(LastErrorMessage().find("safeArea.top"), String::npos) << LastErrorMessage().c_str();
        EXPECT_TRUE(window->Simulations().empty());
    }

    TEST_F(ControlWindowTest, AHeightThatIsMissingIsInvalidParams) {
        Call("set_window", R"({"width":480})");
        EXPECT_EQ(LastErrorCode(), "invalid_params");
        EXPECT_NE(LastErrorMessage().find("height"), String::npos) << LastErrorMessage().c_str();
    }

    TEST_F(ControlWindowTest, AWindowThatRefusesIsInvalidParamsAndKeepsTheUserScale) {
        window->RefuseWith("the driver said no");
        Call("set_window", SizeOf(480, 300, R"(,"userScale":2.0)"));
        EXPECT_EQ(LastErrorCode(), "invalid_params");
        EXPECT_NE(LastErrorMessage().find("the driver said no"), String::npos) << LastErrorMessage().c_str();
        EXPECT_FLOAT_EQ(UserScale(), kStartUserScale);
    }

    TEST_F(ControlWindowTest, ARealWindowIsNotHeadlessAndNothingChanges) {
        UseWindow(false);
        Call("set_window", SizeOf(480, 300, R"(,"userScale":2.0)"));
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
        const JsonView status = LastResult();
        ASSERT_TRUE(status.IsObject());
        const JsonView shown = MemberOf(status, "window");
        ASSERT_TRUE(shown.IsObject());
        EXPECT_EQ(NumberOf(shown, "width"), 480.0);
        EXPECT_EQ(NumberOf(shown, "height"), 320.0);
        EXPECT_EQ(NumberOf(shown, "density"), 2.0);
        EXPECT_EQ(NumberOf(shown, "displayScale"), 1.0);
        EXPECT_EQ(NumberOf(shown, "outputWidth"), 960.0);
        EXPECT_EQ(NumberOf(shown, "outputHeight"), 640.0);
        EXPECT_EQ(NumberOf(shown, "userScale"), static_cast<double>(kStartUserScale));
        EXPECT_TRUE(BoolOf(shown, "headless").value_or(false));
        const JsonView safeArea = MemberOf(shown, "safeArea");
        EXPECT_EQ(NumberOf(safeArea, "top"), 40.0);
        EXPECT_EQ(NumberOf(safeArea, "right"), 10.0);
        EXPECT_EQ(NumberOf(safeArea, "bottom"), 20.0);
        EXPECT_EQ(NumberOf(safeArea, "left"), 30.0);
    }

    TEST_F(ControlWindowTest, TheWrittenWindowStatusKeepsItsBytes) {
        WindowSimulation current;
        current.width = 480;
        current.height = 320;
        current.density = 2.0f;
        current.safeArea = {40.0f, 10.0f, 20.0f, 30.0f};
        window->SetCurrent(current);
        renderer->width = 960.0f;
        renderer->height = 640.0f;

        String written;
        JsonWriter writer(written);
        ControlWindowStatus::Read().WriteJson(writer);

        EXPECT_EQ(
            written,
            R"({"density":2.0,"displayScale":1.0,"headless":true,"height":320,"outputHeight":640,"outputWidth":960,"safeArea":{"bottom":20.0,"left":30.0,"right":10.0,"top":40.0},"userScale":1.25,"width":480})");
    }

    TEST_F(ControlWindowTest, StatusTellsARealWindowToo) {
        UseWindow(false);
        ASSERT_TRUE(Send("status"));
        const JsonView shown = MemberOf(LastResult(), "window");
        ASSERT_TRUE(shown.IsObject());
        EXPECT_EQ(NumberOf(shown, "width"), 800.0);
        EXPECT_FALSE(BoolOf(shown, "headless").value_or(true));
    }

    TEST_F(ControlWindowTest, StatusWithoutAWindowOrRendererKeepsTheShape) {
        CoreManager::GetMainWindowManager().SetMainWindow({});
        CoreManager::GetRendererManager().SetRenderer({});
        ASSERT_TRUE(Send("status"));
        const JsonView shown = MemberOf(LastResult(), "window");
        ASSERT_TRUE(shown.IsObject());
        EXPECT_EQ(NumberOf(shown, "width"), 0.0);
        EXPECT_EQ(NumberOf(shown, "outputWidth"), 0.0);
        EXPECT_EQ(NumberOf(shown, "density"), 1.0);
        EXPECT_FALSE(BoolOf(shown, "headless").value_or(true));
        EXPECT_TRUE(shown.Find("safeArea").has_value());
    }

    TEST_F(ControlWindowTest, ListMethodsListsSetWindowWithItsParameters) {
        ASSERT_TRUE(Send("list_methods"));
        const JsonView methods = MemberOf(LastResult(), "methods");
        ASSERT_TRUE(methods.IsArray());

        JsonView found;
        for (const JsonView method : methods.Items()) {
            if (StringOf(method, "name") == "set_window") {
                found = method;
            }
        }
        ASSERT_TRUE(found.IsObject());
        EXPECT_FALSE(StringOf(found, "summary").empty());
        const JsonView params = MemberOf(found, "params");
        ASSERT_TRUE(params.IsArray());
        Vector<String> names;
        Vector<String> required;
        for (const JsonView param : params.Items()) {
            names.push_back(StringOf(param, "name"));
            if (BoolOf(param, "required").value_or(false)) {
                required.push_back(StringOf(param, "name"));
            }
        }
        EXPECT_EQ(names, (Vector<String>{"width", "height", "density", "userScale", "safeArea"}));
        EXPECT_EQ(required, (Vector<String>{"width", "height"}));
    }

    TEST_F(ControlWindowTest, CompileTimeChecks) {
        static_assert(WindowControlErrors::kNotHeadless == "not_headless");
    }

}  // namespace BECore::Tests
