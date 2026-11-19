#include <BECore/Clipboard/Clipboard.h>
#include <BECore/Clipboard/FakeClipboard.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/FingerPointers.h>
#include <BECore/Input/PointerId.h>
#include <BECore/Input/UIInput.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/MainWindow/MainWindowManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Time/GamePause.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLEvents.h>
#include <CoreSDL/SDLEventsProvider.h>
#include <Events/ClipboardEvents.h>
#include <Events/InputEvents.h>
#include <imgui.h>

namespace BECore::Tests {

    namespace {

        /// Collects every MouseButtonDownEvent that reaches game code while it is alive.
        class MouseRecorder : public SubscriptionHolder {
        public:
            MouseRecorder() {
                Subscribe<SDLEvents::MouseButtonDownEvent, &MouseRecorder::OnMouseDown>(this);
            }

            Vector<SDL_MouseButtonEvent> clicks;

        private:
            void OnMouseDown(const SDLEvents::MouseButtonDownEvent& e) {
                clicks.push_back(e.button);
            }
        };

        /// Collects the rest of what the provider converts: keys, text, window, safe area and orientation events
        class InputRecorder : public SubscriptionHolder {
        public:
            InputRecorder() {
                Subscribe<SDLEvents::MouseButtonUpEvent, &InputRecorder::OnMouseUp>(this);
                Subscribe<SDLEvents::MouseMotionEvent, &InputRecorder::OnMouseMotion>(this);
                Subscribe<SDLEvents::MouseWheelEvent, &InputRecorder::OnMouseWheel>(this);
                Subscribe<SDLEvents::KeyDownEvent, &InputRecorder::OnKeyDown>(this);
                Subscribe<SDLEvents::KeyUpEvent, &InputRecorder::OnKeyUp>(this);
                Subscribe<SDLEvents::TextInputEvent, &InputRecorder::OnText>(this);
                Subscribe<SDLEvents::WindowEvent, &InputRecorder::OnWindow>(this);
                Subscribe<SDLEvents::SafeAreaChangedEvent, &InputRecorder::OnSafeArea>(this);
                Subscribe<SDLEvents::DisplayOrientationChangedEvent, &InputRecorder::OnOrientation>(this);
            }

            Vector<SDL_MouseButtonEvent> ups;
            Vector<SDL_MouseMotionEvent> motions;
            Vector<SDL_MouseWheelEvent> wheels;
            Vector<SDLEvents::KeyDownEvent> keyDowns;
            Vector<SDLEvents::KeyUpEvent> keyUps;
            Vector<String> texts;
            Vector<SDL_EventType> windowEvents;
            int safeAreaChanges = 0;
            Vector<SDLEvents::DisplayOrientationChangedEvent> orientations;

        private:
            void OnMouseUp(const SDLEvents::MouseButtonUpEvent& e) {
                ups.push_back(e.button);
            }

            void OnMouseMotion(const SDLEvents::MouseMotionEvent& e) {
                motions.push_back(e.motion);
            }

            void OnMouseWheel(const SDLEvents::MouseWheelEvent& e) {
                wheels.push_back(e.wheel);
            }

            void OnKeyDown(const SDLEvents::KeyDownEvent& e) {
                keyDowns.push_back(e);
            }

            void OnKeyUp(const SDLEvents::KeyUpEvent& e) {
                keyUps.push_back(e);
            }

            void OnText(const SDLEvents::TextInputEvent& e) {
                texts.push_back(e.text);
            }

            void OnWindow(const SDLEvents::WindowEvent& e) {
                windowEvents.push_back(static_cast<SDL_EventType>(e.window.type));
            }

            void OnSafeArea(const SDLEvents::SafeAreaChangedEvent&) {
                ++safeAreaChanges;
            }

            void OnOrientation(const SDLEvents::DisplayOrientationChangedEvent& e) {
                orientations.push_back(e);
            }
        };

        /// Counts the raw events the provider hands out before it converts them (ImGui and the editor chords read these)
        class RawRecorder : public SubscriptionHolder {
        public:
            RawRecorder() {
                Subscribe<SDLEvents::SDLEventWrapper, &RawRecorder::OnRaw>(this);
            }

            Vector<uint32_t> types;

        private:
            void OnRaw(const SDLEvents::SDLEventWrapper& e) {
                types.push_back(e.event.type);
            }
        };

        /// Registers a handler in the global UIInput for its own lifetime, so a case that returns early leaves no dangling handler behind
        class ScopedUIInputHandler {
        public:
            explicit ScopedUIInputHandler(IUIInputHandler& handler) : _handler(handler) {
                CoreManager::GetUIInput().Register(_handler, 0);
            }

            ~ScopedUIInputHandler() {
                CoreManager::GetUIInput().Unregister(_handler);
            }

            ScopedUIInputHandler(const ScopedUIInputHandler&) = delete;
            ScopedUIInputHandler& operator=(const ScopedUIInputHandler&) = delete;

        private:
            IUIInputHandler& _handler;
        };

        /// Consumes every mouse event it is offered
        class ConsumingHandler : public IUIInputHandler {
        public:
            bool OnMouseButtonDown(const SDL_MouseButtonEvent&) override {
                ++downs;
                return true;
            }

            bool OnMouseMotion(const SDL_MouseMotionEvent&) override {
                ++motions;
                return true;
            }

            int downs = 0;
            int motions = 0;
        };

        /// A window that only has a pixel density (2 is a Retina display)
        class DensityWindow : public IMainWindow {
        public:
            explicit DensityWindow(float density) : _density(density) {}

            bool Initialize(StringView /*configPath*/) override {
                return true;
            }

            void Destroy() override {}

            bool IsValid() const override {
                return true;
            }

            int GetWidth() const override {
                return 800;
            }

            int GetHeight() const override {
                return 600;
            }

            float GetPixelDensity() const override {
                return _density;
            }

            void SetDensity(float density) {
                _density = density;
            }

        private:
            float _density;
        };

#if defined(__APPLE__)
        constexpr SDL_Keymod kPasteModifier = SDL_KMOD_LGUI;
#else
        constexpr SDL_Keymod kPasteModifier = SDL_KMOD_LCTRL;
#endif

        void PushEvent(SDL_Event event) {
            ASSERT_TRUE(SDL_PushEvent(&event)) << SDL_GetError();
        }

        void PushMouseButton(SDL_EventType type, float x, float y) {
            SDL_Event event{};
            event.type = type;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.x = x;
            event.button.y = y;
            ASSERT_NO_FATAL_FAILURE(PushEvent(event));
        }

        void PushMouseMotion(float x, float y) {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.motion.x = x;
            event.motion.y = y;
            event.motion.xrel = 1.0f;
            ASSERT_NO_FATAL_FAILURE(PushEvent(event));
        }

        void PushMouseWheel(float scrollY) {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_WHEEL;
            event.wheel.y = scrollY;
            ASSERT_NO_FATAL_FAILURE(PushEvent(event));
        }

        void PushKey(SDL_EventType type, SDL_Keycode key, bool repeat = false) {
            SDL_Event event{};
            event.type = type;
            event.key.key = key;
            event.key.mod = SDL_KMOD_LSHIFT;
            event.key.down = type == SDL_EVENT_KEY_DOWN;
            event.key.repeat = repeat;
            ASSERT_NO_FATAL_FAILURE(PushEvent(event));
        }

        void PushKeyWithMod(SDL_Keycode key, SDL_Keymod mod) {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = key;
            event.key.mod = mod;
            event.key.down = true;
            ASSERT_NO_FATAL_FAILURE(PushEvent(event));
        }

        void PushText(const char* text) {
            SDL_Event event{};
            event.type = SDL_EVENT_TEXT_INPUT;
            event.text.text = text;
            ASSERT_NO_FATAL_FAILURE(PushEvent(event));
        }

        constexpr SDL_TouchID kScreenTouch = 77;  // a touch screen: neither the trackpad nor the pen id
        constexpr SDL_WindowID kWindowId = 1;
        constexpr float kWindowWidth = 800.0f;  // DensityWindow
        constexpr float kWindowHeight = 600.0f;

        void PushFinger(SDL_EventType type, SDL_FingerID finger, float x, float y, float dx = 0.0f, float dy = 0.0f, SDL_TouchID touch = kScreenTouch, SDL_WindowID window = kWindowId) {
            SDL_Event event{};
            event.type = type;
            event.tfinger.touchID = touch;
            event.tfinger.fingerID = finger;
            event.tfinger.x = x;
            event.tfinger.y = y;
            event.tfinger.dx = dx;
            event.tfinger.dy = dy;
            event.tfinger.pressure = 1.0f;
            event.tfinger.windowID = window;
            ASSERT_NO_FATAL_FAILURE(PushEvent(event));
        }

        void PushTouchMouse(SDL_EventType type, float x, float y) {
            SDL_Event event{};
            event.type = type;
            if (type == SDL_EVENT_MOUSE_MOTION) {
                event.motion.which = SDL_TOUCH_MOUSEID;
                event.motion.x = x;
                event.motion.y = y;
            } else {
                event.button.which = SDL_TOUCH_MOUSEID;
                event.button.button = SDL_BUTTON_LEFT;
                event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                event.button.x = x;
                event.button.y = y;
            }
            ASSERT_NO_FATAL_FAILURE(PushEvent(event));
        }

        /// Takes every press and motion and counts what it was told, with the pointer of the last one
        class CountingHandler : public IUIInputHandler {
        public:
            bool OnMouseButtonDown(const SDL_MouseButtonEvent& event) override {
                downs.push_back(event.which);
                return true;
            }

            bool OnMouseButtonUp(const SDL_MouseButtonEvent& event) override {
                ups.push_back(event.which);
                return true;
            }

            bool OnMouseMotion(const SDL_MouseMotionEvent& event) override {
                motions.push_back(event.which);
                return true;
            }

            void OnPointerLeave(uint32_t which) override {
                leaves.push_back(which);
            }

            Vector<uint32_t> downs;
            Vector<uint32_t> ups;
            Vector<uint32_t> motions;
            Vector<uint32_t> leaves;
        };

        /// Turns the finger events of SDL off for one test and back on at its end
        class ScopedFingerEvents {
        public:
            explicit ScopedFingerEvents(bool enabled) {
                Set(enabled);
            }

            ~ScopedFingerEvents() {
                Set(true);
            }

            ScopedFingerEvents(const ScopedFingerEvents&) = delete;
            ScopedFingerEvents& operator=(const ScopedFingerEvents&) = delete;

        private:
            static void Set(bool enabled) {
                SDL_SetEventEnabled(SDL_EVENT_FINGER_DOWN, enabled);
                SDL_SetEventEnabled(SDL_EVENT_FINGER_MOTION, enabled);
                SDL_SetEventEnabled(SDL_EVENT_FINGER_UP, enabled);
                SDL_SetEventEnabled(SDL_EVENT_FINGER_CANCELED, enabled);
            }
        };

    }  // namespace

    /// The provider polls the real SDL queue; the events subsystem needs no window or display.
    class SDLEventsProviderTest : public ::testing::Test {
    protected:
        void SetUp() override {
            ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
            SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
            ASSERT_TRUE(_provider.Initialize());
        }

        void TearDown() override {
            CoreManager::GetPause().SetPaused(false);
            CoreManager::GetMainWindowManager().SetMainWindow(IntrusivePtr<IMainWindow>());
            if (ImGui::GetCurrentContext() != nullptr) {
                ImGui::DestroyContext();
            }
            SDL_Quit();
        }

        void InstallWindow(float density) {
            _window = New<DensityWindow>(density);
            CoreManager::GetMainWindowManager().SetMainWindow(_window);
        }

        IntrusivePtr<DensityWindow> _window;

        SDLEventsProvider _provider;
    };

    TEST_F(SDLEventsProviderTest, GameModeClickReachesTheSceneWithWindowCoordinates) {
        // The Game run mode has no ImGui context at all
        ASSERT_EQ(ImGui::GetCurrentContext(), nullptr);

        MouseRecorder recorder;
        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 123.0f, 45.0f));
        _provider.ProcessEvents();

        ASSERT_EQ(recorder.clicks.size(), 1u);
        EXPECT_EQ(recorder.clicks[0].button, SDL_BUTTON_LEFT);
        EXPECT_FLOAT_EQ(recorder.clicks[0].x, 123.0f);
        EXPECT_FLOAT_EQ(recorder.clicks[0].y, 45.0f);
    }

    TEST_F(SDLEventsProviderTest, EditorModeKeepsTheClickWhileImGuiWantsTheMouse) {
        ImGui::CreateContext();
        ImGui::GetIO().WantCaptureMouse = true;

        MouseRecorder recorder;
        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 10.0f, 10.0f));
        _provider.ProcessEvents();

        EXPECT_TRUE(recorder.clicks.empty());
    }

    TEST_F(SDLEventsProviderTest, EditorModeForwardsTheClickWhenImGuiIgnoresTheMouse) {
        ImGui::CreateContext();
        ImGui::GetIO().WantCaptureMouse = false;

        MouseRecorder recorder;
        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 10.0f, 20.0f));
        _provider.ProcessEvents();

        ASSERT_EQ(recorder.clicks.size(), 1u);
        EXPECT_FLOAT_EQ(recorder.clicks[0].x, 10.0f);
        EXPECT_FLOAT_EQ(recorder.clicks[0].y, 20.0f);
    }

    TEST_F(SDLEventsProviderTest, AUIHandlerConsumesTheClickBeforeGameCode) {
        ConsumingHandler ui;
        MouseRecorder recorder;

        const auto pushAndProcess = [this]() {
            ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 30.0f, 40.0f));
            _provider.ProcessEvents();
        };
        {
            const ScopedUIInputHandler uiRegistered(ui);
            pushAndProcess();
        }

        EXPECT_EQ(ui.downs, 1);
        EXPECT_TRUE(recorder.clicks.empty());

        pushAndProcess();
        ASSERT_EQ(recorder.clicks.size(), 1u);
        EXPECT_FLOAT_EQ(recorder.clicks[0].x, 30.0f);
    }

    TEST_F(SDLEventsProviderTest, ASceneTargetTakesTheConvertedClickAndGameCodeGetsNoRawEvent) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(R"(<SceneManager><scenes><scene type="Scene" name="S"><nodes>
  <node name="Pad"><components>
    <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
    <component type="ClickableComponent" actionName="ProviderPad"/>
  </components></node>
</nodes></scene></scenes></SceneManager>)"));
        IntrusivePtrAtomic<SceneManager> manager = New<SceneManager>();
        manager->Deserialize(deserializer);
        ASSERT_FALSE(deserializer.HasErrors());
        manager->Initialize();
        struct ActionCounter : SubscriptionHolder {
            ActionCounter() {
                Subscribe<InputEvents::ActionEvent, &ActionCounter::OnAction>(this);
            }
            void OnAction(const InputEvents::ActionEvent& event) {
                if (event.action == "ProviderPad"_intern) {
                    ++count;
                }
            }
            int count = 0;
        } actions;
        MouseRecorder downs;
        InputRecorder ups;

        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 30.0f, 40.0f));
        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_UP, 30.0f, 40.0f));
        _provider.ProcessEvents();

        EXPECT_EQ(actions.count, 1);
        EXPECT_TRUE(downs.clicks.empty());
        EXPECT_TRUE(ups.ups.empty()) << "the release of a press that a target took is its own";

        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 500.0f, 400.0f));
        _provider.ProcessEvents();

        EXPECT_EQ(actions.count, 1);
        EXPECT_EQ(downs.clicks.size(), 1u) << "a press outside every target reaches the game";
    }

    TEST_F(SDLEventsProviderTest, GameModeForwardsReleaseMotionAndWheel) {
        InputRecorder recorder;
        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_UP, 5.0f, 6.0f));
        ASSERT_NO_FATAL_FAILURE(PushMouseMotion(7.0f, 8.0f));
        ASSERT_NO_FATAL_FAILURE(PushMouseWheel(-2.0f));
        _provider.ProcessEvents();

        ASSERT_EQ(recorder.ups.size(), 1u);
        EXPECT_FLOAT_EQ(recorder.ups[0].x, 5.0f);
        EXPECT_FLOAT_EQ(recorder.ups[0].y, 6.0f);
        ASSERT_EQ(recorder.motions.size(), 1u);
        EXPECT_FLOAT_EQ(recorder.motions[0].x, 7.0f);
        EXPECT_FLOAT_EQ(recorder.motions[0].y, 8.0f);
        EXPECT_FLOAT_EQ(recorder.motions[0].xrel, 1.0f);
        ASSERT_EQ(recorder.wheels.size(), 1u);
        EXPECT_FLOAT_EQ(recorder.wheels[0].y, -2.0f);
    }

    TEST_F(SDLEventsProviderTest, AUIHandlerConsumesMotionAndTheGameSeesNone) {
        ConsumingHandler ui;
        const ScopedUIInputHandler uiRegistered(ui);
        InputRecorder recorder;

        ASSERT_NO_FATAL_FAILURE(PushMouseMotion(1.0f, 1.0f));
        _provider.ProcessEvents();

        EXPECT_EQ(ui.motions, 1);
        EXPECT_TRUE(recorder.motions.empty());
    }

    TEST_F(SDLEventsProviderTest, OnAHighDensityWindowThePositionsAreOutputPixels) {
        InstallWindow(2.0f);
        MouseRecorder clicks;
        InputRecorder recorder;

        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 10.0f, 20.0f));
        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_UP, 3.0f, 4.0f));
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.x = 5.0f;
        motion.motion.y = 6.0f;
        motion.motion.xrel = 1.5f;
        motion.motion.yrel = -2.0f;
        ASSERT_NO_FATAL_FAILURE(PushEvent(motion));
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.x = 1.0f;
        wheel.wheel.y = -3.0f;
        wheel.wheel.mouse_x = 7.0f;
        wheel.wheel.mouse_y = 8.0f;
        ASSERT_NO_FATAL_FAILURE(PushEvent(wheel));
        _provider.ProcessEvents();

        ASSERT_EQ(clicks.clicks.size(), 1u);
        EXPECT_FLOAT_EQ(clicks.clicks[0].x, 20.0f);
        EXPECT_FLOAT_EQ(clicks.clicks[0].y, 40.0f);
        ASSERT_EQ(recorder.ups.size(), 1u);
        EXPECT_FLOAT_EQ(recorder.ups[0].x, 6.0f);
        EXPECT_FLOAT_EQ(recorder.ups[0].y, 8.0f);
        ASSERT_EQ(recorder.motions.size(), 1u);
        EXPECT_FLOAT_EQ(recorder.motions[0].x, 10.0f);
        EXPECT_FLOAT_EQ(recorder.motions[0].y, 12.0f);
        EXPECT_FLOAT_EQ(recorder.motions[0].xrel, 3.0f);
        EXPECT_FLOAT_EQ(recorder.motions[0].yrel, -4.0f);
        ASSERT_EQ(recorder.wheels.size(), 1u);
        EXPECT_FLOAT_EQ(recorder.wheels[0].mouse_x, 14.0f);
        EXPECT_FLOAT_EQ(recorder.wheels[0].mouse_y, 16.0f);
        // The scroll amount is not a position
        EXPECT_FLOAT_EQ(recorder.wheels[0].x, 1.0f);
        EXPECT_FLOAT_EQ(recorder.wheels[0].y, -3.0f);
    }

    TEST_F(SDLEventsProviderTest, AUIHandlerSeesTheScaledPosition) {
        InstallWindow(2.0f);
        class Recording : public ConsumingHandler {
        public:
            bool OnMouseButtonDown(const SDL_MouseButtonEvent& e) override {
                x = e.x;
                y = e.y;
                return ConsumingHandler::OnMouseButtonDown(e);
            }
            float x = 0.0f;
            float y = 0.0f;
        } ui;
        const ScopedUIInputHandler uiRegistered(ui);

        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 10.0f, 20.0f));
        _provider.ProcessEvents();

        EXPECT_EQ(ui.downs, 1);
        EXPECT_FLOAT_EQ(ui.x, 20.0f);
        EXPECT_FLOAT_EQ(ui.y, 40.0f);
    }

    TEST_F(SDLEventsProviderTest, TheDensityIsReadForEveryEvent) {
        InstallWindow(2.0f);
        MouseRecorder recorder;

        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 10.0f, 10.0f));
        _provider.ProcessEvents();
        _window->SetDensity(1.5f);  // the window moved to another display
        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 10.0f, 10.0f));
        _provider.ProcessEvents();

        ASSERT_EQ(recorder.clicks.size(), 2u);
        EXPECT_FLOAT_EQ(recorder.clicks[0].x, 20.0f);
        EXPECT_FLOAT_EQ(recorder.clicks[1].x, 15.0f);
    }

    TEST_F(SDLEventsProviderTest, ADensityOfOneLeavesThePositionsAsTheyAre) {
        InstallWindow(1.0f);
        MouseRecorder recorder;

        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 10.0f, 20.0f));
        _provider.ProcessEvents();

        ASSERT_EQ(recorder.clicks.size(), 1u);
        EXPECT_FLOAT_EQ(recorder.clicks[0].x, 10.0f);
        EXPECT_FLOAT_EQ(recorder.clicks[0].y, 20.0f);
    }

    TEST_F(SDLEventsProviderTest, ImGuiKeepsReleaseMotionAndWheelWhileItWantsTheMouse) {
        ImGui::CreateContext();
        ImGui::GetIO().WantCaptureMouse = true;

        InputRecorder recorder;
        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_UP, 1.0f, 1.0f));
        ASSERT_NO_FATAL_FAILURE(PushMouseMotion(2.0f, 2.0f));
        ASSERT_NO_FATAL_FAILURE(PushMouseWheel(1.0f));
        _provider.ProcessEvents();

        EXPECT_TRUE(recorder.ups.empty());
        EXPECT_TRUE(recorder.motions.empty());
        EXPECT_TRUE(recorder.wheels.empty());
    }

    TEST_F(SDLEventsProviderTest, GameModeForwardsKeysWithTheirModifiersAndRepeat) {
        ASSERT_EQ(ImGui::GetCurrentContext(), nullptr);

        InputRecorder recorder;
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_DOWN, SDLK_A));
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_DOWN, SDLK_A, true));
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_UP, SDLK_A));
        _provider.ProcessEvents();

        ASSERT_EQ(recorder.keyDowns.size(), 2u);
        EXPECT_EQ(recorder.keyDowns[0].key, SDLK_A);
        EXPECT_EQ(recorder.keyDowns[0].mod, SDL_KMOD_LSHIFT);
        EXPECT_FALSE(recorder.keyDowns[0].repeat);
        EXPECT_TRUE(recorder.keyDowns[1].repeat);
        ASSERT_EQ(recorder.keyUps.size(), 1u);
        EXPECT_EQ(recorder.keyUps[0].key, SDLK_A);
    }

    TEST_F(SDLEventsProviderTest, GameModeForwardsTypedTextAsUtf8) {
        InputRecorder recorder;
        ASSERT_NO_FATAL_FAILURE(PushText("a\xD0\x96"));  // 'a' and the Cyrillic Zhe
        _provider.ProcessEvents();

        ASSERT_EQ(recorder.texts.size(), 1u);
        EXPECT_EQ(recorder.texts[0], String("a\xD0\x96"));
    }

    TEST_F(SDLEventsProviderTest, ImGuiKeepsKeysAndTextWhileItWantsTheKeyboard) {
        ImGui::CreateContext();
        ImGui::GetIO().WantCaptureKeyboard = true;

        InputRecorder recorder;
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_DOWN, SDLK_A));
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_UP, SDLK_A));
        ASSERT_NO_FATAL_FAILURE(PushText("a"));
        _provider.ProcessEvents();

        EXPECT_TRUE(recorder.keyDowns.empty());
        EXPECT_TRUE(recorder.keyUps.empty());
        EXPECT_TRUE(recorder.texts.empty());
    }

    TEST_F(SDLEventsProviderTest, TheMouseAndKeyboardCaptureRulesAreIndependent) {
        ImGui::CreateContext();
        ImGui::GetIO().WantCaptureMouse = true;
        ImGui::GetIO().WantCaptureKeyboard = false;

        InputRecorder recorder;
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_DOWN, SDLK_B));
        ASSERT_NO_FATAL_FAILURE(PushMouseMotion(1.0f, 1.0f));
        _provider.ProcessEvents();

        EXPECT_EQ(recorder.keyDowns.size(), 1u);
        EXPECT_TRUE(recorder.motions.empty());
    }

    TEST_F(SDLEventsProviderTest, APausedGameGetsNoMouseKeyOrText) {
        CoreManager::GetPause().SetPaused(true);
        InputRecorder recorder;
        MouseRecorder clicks;

        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 1.0f, 2.0f));
        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_UP, 1.0f, 2.0f));
        ASSERT_NO_FATAL_FAILURE(PushMouseMotion(3.0f, 4.0f));
        ASSERT_NO_FATAL_FAILURE(PushMouseWheel(1.0f));
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_DOWN, SDLK_A));
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_UP, SDLK_A));
        ASSERT_NO_FATAL_FAILURE(PushText("a"));
        _provider.ProcessEvents();

        EXPECT_TRUE(clicks.clicks.empty());
        EXPECT_TRUE(recorder.ups.empty());
        EXPECT_TRUE(recorder.motions.empty());
        EXPECT_TRUE(recorder.wheels.empty());
        EXPECT_TRUE(recorder.keyDowns.empty());
        EXPECT_TRUE(recorder.keyUps.empty());
        EXPECT_TRUE(recorder.texts.empty());
    }

    TEST_F(SDLEventsProviderTest, APausedGameStillGetsTheRawEventsAndTheWindowEvents) {
        CoreManager::GetPause().SetPaused(true);
        RawRecorder raw;
        InputRecorder recorder;

        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 1.0f, 2.0f));
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_DOWN, SDLK_A));
        SDL_Event resized{};
        resized.type = SDL_EVENT_WINDOW_RESIZED;
        ASSERT_NO_FATAL_FAILURE(PushEvent(resized));
        _provider.ProcessEvents();

        const Vector<uint32_t> expected = {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_KEY_DOWN, SDL_EVENT_WINDOW_RESIZED};
        EXPECT_EQ(raw.types, expected);
        // A viewport resize re-lays the scene while paused
        ASSERT_EQ(recorder.windowEvents.size(), 1u);
    }

    TEST_F(SDLEventsProviderTest, InputPassesAgainAfterTheResumeAndNothingWasKept) {
        CoreManager::GetPause().SetPaused(true);
        InputRecorder recorder;
        MouseRecorder clicks;

        // A click made in the pause: the press is dropped, and the release after the resume is only a release, no click
        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 1.0f, 2.0f));
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_DOWN, SDLK_A));
        _provider.ProcessEvents();
        CoreManager::GetPause().SetPaused(false);
        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_UP, 1.0f, 2.0f));
        _provider.ProcessEvents();

        EXPECT_TRUE(clicks.clicks.empty());
        EXPECT_TRUE(recorder.keyDowns.empty());
        EXPECT_EQ(recorder.ups.size(), 1u);

        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 5.0f, 6.0f));
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_DOWN, SDLK_B));
        ASSERT_NO_FATAL_FAILURE(PushText("b"));
        _provider.ProcessEvents();

        ASSERT_EQ(clicks.clicks.size(), 1u);
        EXPECT_FLOAT_EQ(clicks.clicks[0].x, 5.0f);
        ASSERT_EQ(recorder.keyDowns.size(), 1u);
        EXPECT_EQ(recorder.keyDowns[0].key, SDLK_B);
        EXPECT_EQ(recorder.texts.size(), 1u);
    }

    TEST_F(SDLEventsProviderTest, APausedGameDoesNotReachAUIHandlerEither) {
        CoreManager::GetPause().SetPaused(true);
        ConsumingHandler ui;
        const ScopedUIInputHandler uiRegistered(ui);

        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 30.0f, 40.0f));
        ASSERT_NO_FATAL_FAILURE(PushMouseMotion(1.0f, 1.0f));
        _provider.ProcessEvents();

        // The control API's injected input goes to UIInput directly and is not gated
        SDL_MouseButtonEvent injected{};
        injected.button = SDL_BUTTON_LEFT;
        injected.down = true;
        CoreManager::GetUIInput().DispatchMouseButtonDown(injected);

        EXPECT_EQ(ui.motions, 0);
        EXPECT_EQ(ui.downs, 1);
    }

    TEST_F(SDLEventsProviderTest, WindowEventsAreForwardedWithTheirType) {
        InputRecorder recorder;
        for (const SDL_EventType type : {SDL_EVENT_WINDOW_RESIZED, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED, SDL_EVENT_WINDOW_FOCUS_LOST}) {
            SDL_Event event{};
            event.type = type;
            ASSERT_NO_FATAL_FAILURE(PushEvent(event));
        }
        _provider.ProcessEvents();

        const Vector<SDL_EventType> expected = {SDL_EVENT_WINDOW_RESIZED, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED, SDL_EVENT_WINDOW_FOCUS_LOST};
        EXPECT_EQ(recorder.windowEvents, expected);
        EXPECT_EQ(recorder.safeAreaChanges, 0);
    }

    TEST_F(SDLEventsProviderTest, ASafeAreaChangeIsItsOwnEvent) {
        InputRecorder recorder;
        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_SAFE_AREA_CHANGED;
        ASSERT_NO_FATAL_FAILURE(PushEvent(event));
        _provider.ProcessEvents();

        EXPECT_EQ(recorder.safeAreaChanges, 1);
        EXPECT_TRUE(recorder.windowEvents.empty());
    }

    TEST_F(SDLEventsProviderTest, ADisplayRotationCarriesTheNewOrientation) {
        InputRecorder recorder;
        SDL_Event event{};
        event.type = SDL_EVENT_DISPLAY_ORIENTATION;
        event.display.displayID = 7;
        event.display.data1 = SDL_ORIENTATION_PORTRAIT;
        ASSERT_NO_FATAL_FAILURE(PushEvent(event));
        _provider.ProcessEvents();

        ASSERT_EQ(recorder.orientations.size(), 1u);
        EXPECT_EQ(recorder.orientations[0].display, 7u);
        EXPECT_EQ(recorder.orientations[0].orientation, SDL_ORIENTATION_PORTRAIT);
    }

    TEST_F(SDLEventsProviderTest, ThePasteShortcutGivesOnePasteEventAndStillTheKeyDown) {
        auto fake = New<FakeClipboard>("ABC");
        const ScopedClipboardBackend clipboard{IntrusivePtr<IClipboardBackend>(fake)};
        InputRecorder keys;
        PasteRecorder pastes;

        ASSERT_NO_FATAL_FAILURE(PushKeyWithMod(SDLK_V, kPasteModifier));
        _provider.ProcessEvents();
        EXPECT_TRUE(pastes.pastes.empty());  // the answer comes from Clipboard::Update()
        CoreManager::GetClipboard().Update();

        EXPECT_EQ(pastes.pastes, (Vector<String>{"ABC"}));
        ASSERT_EQ(keys.keyDowns.size(), 1u);
        EXPECT_EQ(keys.keyDowns[0].key, SDLK_V);
    }

    TEST_F(SDLEventsProviderTest, APlainVIsNoPaste) {
        auto fake = New<FakeClipboard>("ABC");
        const ScopedClipboardBackend clipboard{IntrusivePtr<IClipboardBackend>(fake)};
        PasteRecorder pastes;

        ASSERT_NO_FATAL_FAILURE(PushKeyWithMod(SDLK_V, SDL_KMOD_NONE));
        _provider.ProcessEvents();
        CoreManager::GetClipboard().Update();

        EXPECT_TRUE(pastes.pastes.empty());
        EXPECT_EQ(fake->ReadCount(), 0);
    }

    TEST_F(SDLEventsProviderTest, NoPasteWhilePaused) {
        auto fake = New<FakeClipboard>("ABC");
        const ScopedClipboardBackend clipboard{IntrusivePtr<IClipboardBackend>(fake)};
        PasteRecorder pastes;
        CoreManager::GetPause().SetPaused(true);

        ASSERT_NO_FATAL_FAILURE(PushKeyWithMod(SDLK_V, kPasteModifier));
        _provider.ProcessEvents();
        CoreManager::GetClipboard().Update();

        EXPECT_TRUE(pastes.pastes.empty());
        EXPECT_EQ(fake->ReadCount(), 0);
    }

    TEST_F(SDLEventsProviderTest, NoPasteWhileImGuiWantsTheKeyboard) {
        ImGui::CreateContext();
        ImGui::GetIO().WantCaptureKeyboard = true;
        auto fake = New<FakeClipboard>("ABC");
        const ScopedClipboardBackend clipboard{IntrusivePtr<IClipboardBackend>(fake)};
        PasteRecorder pastes;

        ASSERT_NO_FATAL_FAILURE(PushKeyWithMod(SDLK_V, kPasteModifier));
        _provider.ProcessEvents();
        CoreManager::GetClipboard().Update();

        EXPECT_TRUE(pastes.pastes.empty());
        EXPECT_EQ(fake->ReadCount(), 0);
    }

    TEST_F(SDLEventsProviderTest, AFingerIsAPressMotionAndReleaseOfItsOwnPointerInOutputPixels) {
        InstallWindow(2.0f);
        MouseRecorder downs;
        InputRecorder recorder;

        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 5, 0.25f, 0.5f));
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_MOTION, 5, 0.5f, 0.25f, 0.25f, -0.25f));
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_UP, 5, 0.5f, 0.25f));
        _provider.ProcessEvents();

        ASSERT_EQ(downs.clicks.size(), 1u);
        EXPECT_TRUE(IsFingerPointer(downs.clicks[0].which));
        EXPECT_EQ(downs.clicks[0].button, SDL_BUTTON_LEFT);
        EXPECT_EQ(downs.clicks[0].clicks, 1);
        EXPECT_TRUE(downs.clicks[0].down);
        EXPECT_FLOAT_EQ(downs.clicks[0].x, 0.25f * kWindowWidth * 2.0f);
        EXPECT_FLOAT_EQ(downs.clicks[0].y, 0.5f * kWindowHeight * 2.0f);

        ASSERT_EQ(recorder.motions.size(), 2u) << "the arrival before the press, then the move";
        EXPECT_EQ(recorder.motions[0].which, downs.clicks[0].which);
        EXPECT_EQ(recorder.motions[0].state, 0u);
        EXPECT_FLOAT_EQ(recorder.motions[0].x, downs.clicks[0].x);
        EXPECT_EQ(recorder.motions[1].which, downs.clicks[0].which);
        EXPECT_EQ(recorder.motions[1].state, SDL_BUTTON_LMASK);
        EXPECT_FLOAT_EQ(recorder.motions[1].x, 0.5f * kWindowWidth * 2.0f);
        EXPECT_FLOAT_EQ(recorder.motions[1].y, 0.25f * kWindowHeight * 2.0f);
        EXPECT_FLOAT_EQ(recorder.motions[1].xrel, 0.25f * kWindowWidth * 2.0f);
        EXPECT_FLOAT_EQ(recorder.motions[1].yrel, -0.25f * kWindowHeight * 2.0f);

        ASSERT_EQ(recorder.ups.size(), 1u);
        EXPECT_EQ(recorder.ups[0].which, downs.clicks[0].which);
        EXPECT_FALSE(recorder.ups[0].down);
        EXPECT_FLOAT_EQ(recorder.ups[0].x, 0.5f * kWindowWidth * 2.0f);
    }

    TEST_F(SDLEventsProviderTest, TwoFingersKeepTheirOwnPressHoverAndRelease) {
        InstallWindow(1.0f);
        CountingHandler left;
        CountingHandler right;
        // The left half and the right half of the window
        struct Half : IUIInputHandler {
            Half(CountingHandler& target, bool leftHalf) : _target(target), _leftHalf(leftHalf) {}
            bool ContainsPoint(float x, float) const override {
                return (x < kWindowWidth * 0.5f) == _leftHalf;
            }
            bool OnMouseButtonDown(const SDL_MouseButtonEvent& e) override {
                return _target.OnMouseButtonDown(e);
            }
            bool OnMouseButtonUp(const SDL_MouseButtonEvent& e) override {
                return _target.OnMouseButtonUp(e);
            }
            bool OnMouseMotion(const SDL_MouseMotionEvent& e) override {
                return _target.OnMouseMotion(e);
            }
            void OnPointerLeave(uint32_t which) override {
                _target.OnPointerLeave(which);
            }
            CountingHandler& _target;
            bool _leftHalf;
        } leftHalf(left, true), rightHalf(right, false);
        const ScopedUIInputHandler leftHalfRegistered(leftHalf);
        const ScopedUIInputHandler rightHalfRegistered(rightHalf);

        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 1, 0.25f, 0.5f));
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 2, 0.75f, 0.5f));
        // The first finger drags over to the other half: it keeps its press and hover, and the second finger's target is not disturbed
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_MOTION, 1, 0.9f, 0.5f, 0.65f, 0.0f));
        _provider.ProcessEvents();

        ASSERT_EQ(left.downs.size(), 1u);
        ASSERT_EQ(right.downs.size(), 1u);
        EXPECT_NE(left.downs[0], right.downs[0]);
        EXPECT_TRUE(CoreManager::GetUIInput().HoldsPress(leftHalf, left.downs[0]));
        EXPECT_TRUE(CoreManager::GetUIInput().HoldsPress(rightHalf, right.downs[0]));
        EXPECT_EQ(left.motions.size(), 2u) << "its arrival and its drag";
        EXPECT_EQ(right.motions.size(), 1u) << "only the arrival";

        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_UP, 2, 0.75f, 0.5f));
        _provider.ProcessEvents();

        EXPECT_TRUE(left.ups.empty());
        ASSERT_EQ(right.ups.size(), 1u);
        EXPECT_EQ(right.ups[0], right.downs[0]);
        EXPECT_TRUE(CoreManager::GetUIInput().HoldsPress(leftHalf, left.downs[0])) << "the other finger is still down";

        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_UP, 1, 0.9f, 0.5f));
        _provider.ProcessEvents();

        ASSERT_EQ(left.ups.size(), 1u);
        EXPECT_EQ(left.ups[0], left.downs[0]);
    }

    TEST_F(SDLEventsProviderTest, ALiftedFingerTellsItsHoverToLeave) {
        InstallWindow(1.0f);
        CountingHandler handler;
        const ScopedUIInputHandler handlerRegistered(handler);

        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 1, 0.5f, 0.5f));
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_UP, 1, 0.5f, 0.5f));
        _provider.ProcessEvents();

        ASSERT_EQ(handler.leaves.size(), 1u);
        EXPECT_EQ(handler.leaves[0], handler.downs[0]);
    }

    TEST_F(SDLEventsProviderTest, ACancelledFingerReleasesItsPress) {
        InstallWindow(1.0f);
        CountingHandler handler;
        const ScopedUIInputHandler handlerRegistered(handler);

        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 1, 0.5f, 0.5f));
        _provider.ProcessEvents();
        ASSERT_EQ(handler.downs.size(), 1u);
        EXPECT_TRUE(CoreManager::GetUIInput().HoldsPress(handler, handler.downs[0]));

        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_CANCELED, 1, 0.5f, 0.5f));
        _provider.ProcessEvents();
        const bool held = CoreManager::GetUIInput().HoldsPress(handler, handler.downs[0]);

        EXPECT_FALSE(held);
        ASSERT_EQ(handler.ups.size(), 1u);
        EXPECT_EQ(handler.ups[0], handler.downs[0]);
    }

    TEST_F(SDLEventsProviderTest, ANinthFingerGetsNoPointer) {
        InstallWindow(1.0f);
        CountingHandler handler;
        const ScopedUIInputHandler handlerRegistered(handler);

        for (SDL_FingerID finger = 1; finger <= kMaxFingerPointers + 1; ++finger) {
            ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, finger, 0.5f, 0.5f));
        }
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_MOTION, kMaxFingerPointers + 1, 0.6f, 0.5f, 0.1f, 0.0f));
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_UP, kMaxFingerPointers + 1, 0.6f, 0.5f));
        _provider.ProcessEvents();

        EXPECT_EQ(handler.downs.size(), kMaxFingerPointers);
        EXPECT_EQ(handler.motions.size(), kMaxFingerPointers) << "the arrivals only";
        EXPECT_TRUE(handler.ups.empty());
    }

    TEST_F(SDLEventsProviderTest, ATouchMouseEventIsTheSameTouchAgainWhileSDLSendsFingerEvents) {
        InstallWindow(1.0f);
        MouseRecorder downs;
        InputRecorder recorder;

        // The order SDL pushes them in: the synthesized mouse event first, then the finger's own
        ASSERT_NO_FATAL_FAILURE(PushTouchMouse(SDL_EVENT_MOUSE_MOTION, 400.0f, 300.0f));
        ASSERT_NO_FATAL_FAILURE(PushTouchMouse(SDL_EVENT_MOUSE_BUTTON_DOWN, 400.0f, 300.0f));
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 1, 0.5f, 0.5f));
        ASSERT_NO_FATAL_FAILURE(PushTouchMouse(SDL_EVENT_MOUSE_BUTTON_UP, 400.0f, 300.0f));
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_UP, 1, 0.5f, 0.5f));
        _provider.ProcessEvents();

        ASSERT_EQ(downs.clicks.size(), 1u) << "the first tap reaches the game once";
        EXPECT_TRUE(IsFingerPointer(downs.clicks[0].which));
        ASSERT_EQ(recorder.ups.size(), 1u);
        EXPECT_TRUE(IsFingerPointer(recorder.ups[0].which));
        for (const SDL_MouseMotionEvent& motion : recorder.motions) {
            EXPECT_TRUE(IsFingerPointer(motion.which));
        }
    }

    TEST_F(SDLEventsProviderTest, TheTouchMouseEventsAreTheOnlySourceWhenSDLSendsNoFingerEvents) {
        const ScopedFingerEvents noFingers(false);
        InstallWindow(1.0f);
        MouseRecorder downs;
        InputRecorder recorder;

        ASSERT_NO_FATAL_FAILURE(PushTouchMouse(SDL_EVENT_MOUSE_MOTION, 10.0f, 20.0f));
        ASSERT_NO_FATAL_FAILURE(PushTouchMouse(SDL_EVENT_MOUSE_BUTTON_DOWN, 10.0f, 20.0f));
        ASSERT_NO_FATAL_FAILURE(PushTouchMouse(SDL_EVENT_MOUSE_BUTTON_UP, 10.0f, 20.0f));
        _provider.ProcessEvents();

        ASSERT_EQ(downs.clicks.size(), 1u);
        EXPECT_EQ(downs.clicks[0].which, SDL_TOUCH_MOUSEID);
        EXPECT_EQ(recorder.motions.size(), 1u);
        EXPECT_EQ(recorder.ups.size(), 1u);
    }

    TEST_F(SDLEventsProviderTest, AMouseOfAnotherIdIsNotAffectedByTheTouchRule) {
        InstallWindow(1.0f);
        MouseRecorder downs;

        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, 1.0f, 2.0f));
        _provider.ProcessEvents();

        EXPECT_EQ(downs.clicks.size(), 1u);
    }

    TEST_F(SDLEventsProviderTest, ATrackpadAndAPenTouchAreNoFingersOnTheScene) {
        InstallWindow(1.0f);
        MouseRecorder downs;

        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 1, 0.5f, 0.5f, 0.0f, 0.0f, SDL_MOUSE_TOUCHID));
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 2, 0.5f, 0.5f, 0.0f, 0.0f, SDL_PEN_TOUCHID));
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 3, 0.5f, 0.5f, 0.0f, 0.0f, kScreenTouch, 0));
        _provider.ProcessEvents();

        EXPECT_TRUE(downs.clicks.empty());
    }

    TEST_F(SDLEventsProviderTest, AFingerWithoutAWindowToMeasureAgainstIsDropped) {
        MouseRecorder downs;

        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 1, 0.5f, 0.5f));
        _provider.ProcessEvents();

        EXPECT_TRUE(downs.clicks.empty());
    }

    TEST_F(SDLEventsProviderTest, APausedGameAndAnImGuiCaptureDropFingersLikeTheMouse) {
        InstallWindow(1.0f);
        MouseRecorder downs;
        InputRecorder recorder;

        CoreManager::GetPause().SetPaused(true);
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 1, 0.5f, 0.5f));
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_MOTION, 1, 0.6f, 0.5f, 0.1f, 0.0f));
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_UP, 1, 0.6f, 0.5f));
        _provider.ProcessEvents();
        CoreManager::GetPause().SetPaused(false);

        ImGui::CreateContext();
        ImGui::GetIO().WantCaptureMouse = true;
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 2, 0.5f, 0.5f));
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_UP, 2, 0.5f, 0.5f));
        _provider.ProcessEvents();

        EXPECT_TRUE(downs.clicks.empty());
        EXPECT_TRUE(recorder.motions.empty());
        EXPECT_TRUE(recorder.ups.empty());
    }

    TEST_F(SDLEventsProviderTest, AFingerLiftedInAPauseFreesItsPlace) {
        InstallWindow(1.0f);
        MouseRecorder downs;

        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, 1, 0.5f, 0.5f));
        _provider.ProcessEvents();
        CoreManager::GetPause().SetPaused(true);
        ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_UP, 1, 0.5f, 0.5f));
        _provider.ProcessEvents();
        CoreManager::GetPause().SetPaused(false);

        // Eight more fingers fit only when the first one gave its place back
        for (SDL_FingerID finger = 2; finger < 2 + kMaxFingerPointers; ++finger) {
            ASSERT_NO_FATAL_FAILURE(PushFinger(SDL_EVENT_FINGER_DOWN, finger, 0.5f, 0.5f));
        }
        _provider.ProcessEvents();

        EXPECT_EQ(downs.clicks.size(), 1u + kMaxFingerPointers);
    }

}  // namespace BECore::Tests
