#include <Common/Printers.h>
#include <CoreSDL/SDLClipboard.h>
#include <SDL3/SDL.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

#if defined(__APPLE__)
        constexpr SDL_Keymod kPrimary = SDL_KMOD_LGUI;
        constexpr SDL_Keymod kPrimaryRight = SDL_KMOD_RGUI;
        constexpr SDL_Keymod kOtherPrimary = SDL_KMOD_LCTRL;
#else
        constexpr SDL_Keymod kPrimary = SDL_KMOD_LCTRL;
        constexpr SDL_Keymod kPrimaryRight = SDL_KMOD_RCTRL;
        constexpr SDL_Keymod kOtherPrimary = SDL_KMOD_LGUI;
#endif

    }  // namespace

    TEST(SDLClipboardTest, ThePasteShortcutIsVWithThePrimaryModifier) {
        EXPECT_TRUE(IsPasteShortcut(SDLK_V, kPrimary, false));
        EXPECT_TRUE(IsPasteShortcut(SDLK_V, kPrimaryRight, false));
    }

    TEST(SDLClipboardTest, ShiftIsAllowed) {
        EXPECT_TRUE(IsPasteShortcut(SDLK_V, kPrimary | SDL_KMOD_LSHIFT, false));
        EXPECT_TRUE(IsPasteShortcut(SDLK_V, kPrimary | SDL_KMOD_RSHIFT, false));
    }

    TEST(SDLClipboardTest, AltAndTheOtherPrimaryModifierAreNot) {
        EXPECT_FALSE(IsPasteShortcut(SDLK_V, kPrimary | SDL_KMOD_LALT, false));
        EXPECT_FALSE(IsPasteShortcut(SDLK_V, kPrimary | SDL_KMOD_RALT, false));
        EXPECT_FALSE(IsPasteShortcut(SDLK_V, kOtherPrimary, false));
        EXPECT_FALSE(IsPasteShortcut(SDLK_V, kPrimary | kOtherPrimary, false));
    }

    TEST(SDLClipboardTest, ARepeatNoModifierOrAnotherKeyIsNot) {
        EXPECT_FALSE(IsPasteShortcut(SDLK_V, kPrimary, true));
        EXPECT_FALSE(IsPasteShortcut(SDLK_V, SDL_KMOD_NONE, false));
        EXPECT_FALSE(IsPasteShortcut(SDLK_V, SDL_KMOD_LSHIFT, false));
        EXPECT_FALSE(IsPasteShortcut(SDLK_C, kPrimary, false));
        EXPECT_FALSE(IsPasteShortcut(SDLK_X, kPrimary, false));
    }

    /// The real SDL backend, only on the dummy video driver: SDL then keeps the clipboard in its own memory, so ctest never touches the system one
    class SDLClipboardBackendTest : public ::testing::Test {
    protected:
        void SetUp() override {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
            if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
                SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
                GTEST_SKIP() << "no dummy video driver: " << SDL_GetError();
            }
            _initialised = true;
            const char* driver = SDL_GetCurrentVideoDriver();
            if (driver == nullptr || StringView(driver) != "dummy") {
                GTEST_SKIP() << "the video driver is not dummy, the system clipboard would be touched";
            }
        }

        void TearDown() override {
            if (_initialised) {
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
            }
            SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
        }

        SDLClipboardBackend backend;

    private:
        bool _initialised = false;
    };

    TEST_F(SDLClipboardBackendTest, ATextWrittenIsReadBack) {
        EXPECT_TRUE(backend.IsAvailable());
        ASSERT_TRUE(backend.WriteText("room 4711 мир"));

        ClipboardText read;
        backend.BeginRead();
        ASSERT_TRUE(backend.PollRead(read));
        EXPECT_EQ(read.status, ClipboardStatus::Ok);
        EXPECT_EQ(read.text, "room 4711 мир");
    }

    TEST_F(SDLClipboardBackendTest, AnEmptyClipboardIsEmpty) {
        ASSERT_TRUE(backend.WriteText(""));

        ClipboardText read;
        read.text = "stale";
        backend.BeginRead();
        ASSERT_TRUE(backend.PollRead(read));
        EXPECT_EQ(read.status, ClipboardStatus::Empty);
        EXPECT_TRUE(read.text.empty());
    }

    TEST(SDLClipboardTest, WithoutSDLVideoTheBackendIsNotAvailable) {
        if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
            GTEST_SKIP() << "SDL video is up in this process";
        }
        const SDLClipboardBackend backend;
        EXPECT_FALSE(backend.IsAvailable());
    }

    TEST(SDLClipboardTest, TheWebCodesMapToTheirStatuses) {
        EXPECT_EQ(StatusFromWebCode(kWebCodeOk), ClipboardStatus::Ok);
        EXPECT_EQ(StatusFromWebCode(kWebCodeEmpty), ClipboardStatus::Empty);
        EXPECT_EQ(StatusFromWebCode(kWebCodeRefused), ClipboardStatus::Refused);
        EXPECT_EQ(StatusFromWebCode(kWebCodeUnavailable), ClipboardStatus::Unavailable);
    }

    TEST(SDLClipboardTest, AnUnknownWebCodeIsRefused) {
        EXPECT_EQ(StatusFromWebCode(-1), ClipboardStatus::Refused);
        EXPECT_EQ(StatusFromWebCode(4), ClipboardStatus::Refused);
        EXPECT_EQ(StatusFromWebCode(1000000), ClipboardStatus::Refused);
    }

    TEST(SDLClipboardTest, AGestureReadIsFreshInsideTheWindow) {
        EXPECT_TRUE(IsGestureReadFresh(5000.0, 5000.0, kGestureReadWindowMs));
        EXPECT_TRUE(IsGestureReadFresh(5500.0, 5000.0, kGestureReadWindowMs));
        EXPECT_TRUE(IsGestureReadFresh(5000.0 + kGestureReadWindowMs, 5000.0, kGestureReadWindowMs));  // exactly at the window
    }

    TEST(SDLClipboardTest, AGestureReadIsNotFreshWhenOldFutureOrNotANumber) {
        EXPECT_FALSE(IsGestureReadFresh(5000.0 + kGestureReadWindowMs + 0.5, 5000.0, kGestureReadWindowMs));
        EXPECT_FALSE(IsGestureReadFresh(5000.0, 5000.5, kGestureReadWindowMs));  // started in the future
        EXPECT_FALSE(IsGestureReadFresh(5000.0, std::numeric_limits<double>::quiet_NaN(), kGestureReadWindowMs));
        EXPECT_FALSE(IsGestureReadFresh(std::numeric_limits<double>::quiet_NaN(), 5000.0, kGestureReadWindowMs));
        EXPECT_FALSE(IsGestureReadFresh(5000.0, 5000.0, std::numeric_limits<double>::quiet_NaN()));
        EXPECT_FALSE(IsGestureReadFresh(std::numeric_limits<double>::infinity(), 5000.0, kGestureReadWindowMs));
        EXPECT_FALSE(IsGestureReadFresh(5000.0, 5000.0, -1.0));
    }

    TEST(SDLClipboardTest, AWebTextIsKeptOnlyForOk) {
        const ClipboardText ok = MakeWebClipboardText(kWebCodeOk, "room 4711");
        EXPECT_EQ(ok.status, ClipboardStatus::Ok);
        EXPECT_EQ(ok.text, "room 4711");

        for (const int code : {kWebCodeEmpty, kWebCodeRefused, kWebCodeUnavailable, 42}) {
            const ClipboardText other = MakeWebClipboardText(code, "leaked");
            EXPECT_NE(other.status, ClipboardStatus::Ok);
            EXPECT_TRUE(other.text.empty()) << code;
        }
    }

    TEST(SDLClipboardTest, ThePlatformBackendExists) {
        EXPECT_NE(MakePlatformClipboardBackend().Get(), nullptr);
    }

}  // namespace BECore::Tests
