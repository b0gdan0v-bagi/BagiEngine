#include <Common/Printers.h>
#include <CoreSDL/SDLEvents.h>
#include <CoreSDL/SDLEventsProvider.h>
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
            if (ImGui::GetCurrentContext() != nullptr) {
                ImGui::DestroyContext();
            }
            SDL_Quit();
        }

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

}  // namespace BECore::Tests
