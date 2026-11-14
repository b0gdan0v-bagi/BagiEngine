#include <BECore/Clipboard/FakeClipboard.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLEvents.h>
#include <CoreSDL/SDLEventsProvider.h>
#include <Events/ClipboardEvents.h>
#include <imgui.h>

namespace BECore::Tests {

    namespace {

        /// Collects every MouseButtonDownEvent that reaches game code while it is alive.
        class MouseRecorder : public SubscriptionHolder {
        public:
            MouseRecorder() {
                Subscribe<SDLEvents::MouseButtonDownEvent, &MouseRecorder::OnMouseDown>(this);
            }

            eastl::vector<SDL_MouseButtonEvent> clicks;

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

            eastl::vector<SDL_MouseButtonEvent> ups;
            eastl::vector<SDL_MouseMotionEvent> motions;
            eastl::vector<SDL_MouseWheelEvent> wheels;
            eastl::vector<SDLEvents::KeyDownEvent> keyDowns;
            eastl::vector<SDLEvents::KeyUpEvent> keyUps;
            eastl::vector<eastl::string> texts;
            eastl::vector<SDL_EventType> windowEvents;
            int safeAreaChanges = 0;
            eastl::vector<SDLEvents::DisplayOrientationChangedEvent> orientations;

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

            eastl::vector<uint32_t> types;

        private:
            void OnRaw(const SDLEvents::SDLEventWrapper& e) {
                types.push_back(e.event.type);
            }
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

        void PushLeftClick(float x, float y) {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down = true;
            event.button.x = x;
            event.button.y = y;
            ASSERT_TRUE(SDL_PushEvent(&event));
        }

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
        ASSERT_NO_FATAL_FAILURE(PushLeftClick(123.0f, 45.0f));
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
        ASSERT_NO_FATAL_FAILURE(PushLeftClick(10.0f, 10.0f));
        _provider.ProcessEvents();

        EXPECT_TRUE(recorder.clicks.empty());
    }

    TEST_F(SDLEventsProviderTest, EditorModeForwardsTheClickWhenImGuiIgnoresTheMouse) {
        ImGui::CreateContext();
        ImGui::GetIO().WantCaptureMouse = false;

        MouseRecorder recorder;
        ASSERT_NO_FATAL_FAILURE(PushLeftClick(10.0f, 20.0f));
        _provider.ProcessEvents();

        ASSERT_EQ(recorder.clicks.size(), 1u);
        EXPECT_FLOAT_EQ(recorder.clicks[0].x, 10.0f);
        EXPECT_FLOAT_EQ(recorder.clicks[0].y, 20.0f);
    }

    TEST_F(SDLEventsProviderTest, AUIHandlerConsumesTheClickBeforeGameCode) {
        ConsumingHandler ui;
        CoreManager::GetUIInput().Register(ui, 0);
        MouseRecorder recorder;

        const auto pushAndProcess = [this]() {
            ASSERT_NO_FATAL_FAILURE(PushLeftClick(30.0f, 40.0f));
            _provider.ProcessEvents();
        };
        pushAndProcess();
        CoreManager::GetUIInput().Unregister(ui);

        EXPECT_EQ(ui.downs, 1);
        EXPECT_TRUE(recorder.clicks.empty());

        // Once the UI is gone the same click reaches the game
        pushAndProcess();
        ASSERT_EQ(recorder.clicks.size(), 1u);
        EXPECT_FLOAT_EQ(recorder.clicks[0].x, 30.0f);
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
        CoreManager::GetUIInput().Register(ui, 0);
        InputRecorder recorder;

        ASSERT_NO_FATAL_FAILURE(PushMouseMotion(1.0f, 1.0f));
        _provider.ProcessEvents();
        CoreManager::GetUIInput().Unregister(ui);

        EXPECT_EQ(ui.motions, 1);
        EXPECT_TRUE(recorder.motions.empty());
    }

    TEST_F(SDLEventsProviderTest, OnAHighDensityWindowThePositionsAreOutputPixels) {
        InstallWindow(2.0f);
        MouseRecorder clicks;
        InputRecorder recorder;

        ASSERT_NO_FATAL_FAILURE(PushLeftClick(10.0f, 20.0f));
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
        CoreManager::GetUIInput().Register(ui, 0);

        ASSERT_NO_FATAL_FAILURE(PushLeftClick(10.0f, 20.0f));
        _provider.ProcessEvents();
        CoreManager::GetUIInput().Unregister(ui);

        EXPECT_EQ(ui.downs, 1);
        EXPECT_FLOAT_EQ(ui.x, 20.0f);
        EXPECT_FLOAT_EQ(ui.y, 40.0f);
    }

    TEST_F(SDLEventsProviderTest, TheDensityIsReadForEveryEvent) {
        InstallWindow(2.0f);
        MouseRecorder recorder;

        ASSERT_NO_FATAL_FAILURE(PushLeftClick(10.0f, 10.0f));
        _provider.ProcessEvents();
        _window->SetDensity(1.5f);  // the window moved to another display
        ASSERT_NO_FATAL_FAILURE(PushLeftClick(10.0f, 10.0f));
        _provider.ProcessEvents();

        ASSERT_EQ(recorder.clicks.size(), 2u);
        EXPECT_FLOAT_EQ(recorder.clicks[0].x, 20.0f);
        EXPECT_FLOAT_EQ(recorder.clicks[1].x, 15.0f);
    }

    TEST_F(SDLEventsProviderTest, ADensityOfOneLeavesThePositionsAsTheyAre) {
        InstallWindow(1.0f);
        MouseRecorder recorder;

        ASSERT_NO_FATAL_FAILURE(PushLeftClick(10.0f, 20.0f));
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
        EXPECT_EQ(recorder.texts[0], eastl::string("a\xD0\x96"));
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

        ASSERT_NO_FATAL_FAILURE(PushLeftClick(1.0f, 2.0f));
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

        ASSERT_NO_FATAL_FAILURE(PushLeftClick(1.0f, 2.0f));
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_DOWN, SDLK_A));
        SDL_Event resized{};
        resized.type = SDL_EVENT_WINDOW_RESIZED;
        ASSERT_NO_FATAL_FAILURE(PushEvent(resized));
        _provider.ProcessEvents();

        const eastl::vector<uint32_t> expected = {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_KEY_DOWN, SDL_EVENT_WINDOW_RESIZED};
        EXPECT_EQ(raw.types, expected);
        // A viewport resize re-lays the scene while paused
        ASSERT_EQ(recorder.windowEvents.size(), 1u);
    }

    TEST_F(SDLEventsProviderTest, InputPassesAgainAfterTheResumeAndNothingWasKept) {
        CoreManager::GetPause().SetPaused(true);
        InputRecorder recorder;
        MouseRecorder clicks;

        // A click made in the pause: the press is dropped, and the release after the resume is only a release, no click
        ASSERT_NO_FATAL_FAILURE(PushLeftClick(1.0f, 2.0f));
        ASSERT_NO_FATAL_FAILURE(PushKey(SDL_EVENT_KEY_DOWN, SDLK_A));
        _provider.ProcessEvents();
        CoreManager::GetPause().SetPaused(false);
        ASSERT_NO_FATAL_FAILURE(PushMouseButton(SDL_EVENT_MOUSE_BUTTON_UP, 1.0f, 2.0f));
        _provider.ProcessEvents();

        EXPECT_TRUE(clicks.clicks.empty());
        EXPECT_TRUE(recorder.keyDowns.empty());
        EXPECT_EQ(recorder.ups.size(), 1u);

        ASSERT_NO_FATAL_FAILURE(PushLeftClick(5.0f, 6.0f));
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
        CoreManager::GetUIInput().Register(ui, 0);

        ASSERT_NO_FATAL_FAILURE(PushLeftClick(30.0f, 40.0f));
        ASSERT_NO_FATAL_FAILURE(PushMouseMotion(1.0f, 1.0f));
        _provider.ProcessEvents();

        // The control API's injected input goes to UIInput directly and is not gated
        SDL_MouseButtonEvent injected{};
        injected.button = SDL_BUTTON_LEFT;
        injected.down = true;
        CoreManager::GetUIInput().DispatchMouseButtonDown(injected);
        CoreManager::GetUIInput().Unregister(ui);

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

        const eastl::vector<SDL_EventType> expected = {SDL_EVENT_WINDOW_RESIZED, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED, SDL_EVENT_WINDOW_FOCUS_LOST};
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

        EXPECT_EQ(pastes.pastes, (eastl::vector<eastl::string>{"ABC"}));
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

}  // namespace BECore::Tests
