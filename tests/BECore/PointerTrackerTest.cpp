#include <BECore/Input/HitClip.h>
#include <BECore/Input/PointerTracker.h>
#include <BECore/Input/UIInput.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kMouse = 1;
        constexpr uint32_t kFinger = 2;
        constexpr uint8_t kLeft = 1;
        constexpr uint8_t kRight = 3;

        class NullHandler : public IUIInputHandler {};

    }  // namespace

    TEST(PointerTrackerTest, CaptureIsKeptPerPointerAndButton) {
        PointerTracker tracker;
        NullHandler a;
        NullHandler b;
        NullHandler c;

        EXPECT_TRUE(tracker.BeginCapture(kMouse, kLeft, a));
        EXPECT_TRUE(tracker.BeginCapture(kMouse, kRight, b));
        EXPECT_TRUE(tracker.BeginCapture(kFinger, kLeft, c));

        EXPECT_EQ(tracker.FindCapture(kMouse, kLeft), &a);
        EXPECT_EQ(tracker.FindCapture(kMouse, kRight), &b);
        EXPECT_EQ(tracker.FindCapture(kFinger, kLeft), &c);
        EXPECT_EQ(tracker.FindCapture(kFinger, kRight), nullptr);
        EXPECT_EQ(tracker.CaptureCount(), 3u);
    }

    TEST(PointerTrackerTest, SecondPressOfTheSameKeyReplacesTheHolder) {
        PointerTracker tracker;
        NullHandler a;
        NullHandler b;

        tracker.BeginCapture(kMouse, kLeft, a);
        EXPECT_TRUE(tracker.BeginCapture(kMouse, kLeft, b));

        EXPECT_EQ(tracker.FindCapture(kMouse, kLeft), &b);
        EXPECT_EQ(tracker.CaptureCount(), 1u);
    }

    TEST(PointerTrackerTest, MotionHolderIsTheOldestCaptureOfThePointer) {
        PointerTracker tracker;
        NullHandler a;
        NullHandler b;
        NullHandler other;

        tracker.BeginCapture(kFinger, kLeft, other);
        tracker.BeginCapture(kMouse, kRight, a);
        tracker.BeginCapture(kMouse, kLeft, b);

        EXPECT_EQ(tracker.FindCaptureOfPointer(kMouse), &a);
        EXPECT_EQ(tracker.FindCaptureOfPointer(kFinger), &other);

        tracker.EndCapture(kMouse, kRight);
        EXPECT_EQ(tracker.FindCaptureOfPointer(kMouse), &b);

        tracker.EndCapture(kMouse, kLeft);
        EXPECT_EQ(tracker.FindCaptureOfPointer(kMouse), nullptr);
        EXPECT_EQ(tracker.FindCaptureOfPointer(kFinger), &other);
    }

    TEST(PointerTrackerTest, ReplacingKeepsTheAgeOfTheCapture) {
        PointerTracker tracker;
        NullHandler a;
        NullHandler b;
        NullHandler c;

        tracker.BeginCapture(kMouse, kLeft, a);
        tracker.BeginCapture(kMouse, kRight, b);
        tracker.BeginCapture(kMouse, kLeft, c);

        EXPECT_EQ(tracker.FindCaptureOfPointer(kMouse), &c);
    }

    TEST(PointerTrackerTest, EndCaptureOfAnUnknownKeyIsIgnored) {
        PointerTracker tracker;
        NullHandler a;

        tracker.BeginCapture(kMouse, kLeft, a);
        tracker.EndCapture(kMouse, kRight);
        tracker.EndCapture(kFinger, kLeft);

        EXPECT_EQ(tracker.FindCapture(kMouse, kLeft), &a);
        tracker.EndCapture(kMouse, kLeft);
        EXPECT_EQ(tracker.FindCapture(kMouse, kLeft), nullptr);
        EXPECT_EQ(tracker.CaptureCount(), 0u);
    }

    TEST(PointerTrackerTest, ACaptureBeyondTheCapacityIsRefusedButAKnownKeyStillReplaces) {
        PointerTracker tracker;
        NullHandler handlers[PointerTracker::kMaxEntries];
        NullHandler extra;

        for (size_t i = 0; i < PointerTracker::kMaxEntries; ++i) {
            ASSERT_TRUE(tracker.BeginCapture(kMouse, static_cast<uint8_t>(i), handlers[i]));
        }

        EXPECT_FALSE(tracker.BeginCapture(kFinger, kLeft, extra));
        EXPECT_EQ(tracker.FindCapture(kFinger, kLeft), nullptr);
        EXPECT_EQ(tracker.CaptureCount(), PointerTracker::kMaxEntries);

        EXPECT_TRUE(tracker.BeginCapture(kMouse, 0, extra));
        EXPECT_EQ(tracker.FindCapture(kMouse, 0), &extra);
    }

    TEST(PointerTrackerTest, HoverChangeReturnsThePrevious) {
        PointerTracker tracker;
        NullHandler a;
        NullHandler b;

        EXPECT_EQ(tracker.GetHover(kMouse), nullptr);
        EXPECT_EQ(tracker.SetHover(kMouse, &a), nullptr);
        EXPECT_EQ(tracker.GetHover(kMouse), &a);

        EXPECT_EQ(tracker.SetHover(kMouse, &b), &a);
        EXPECT_EQ(tracker.GetHover(kMouse), &b);

        EXPECT_EQ(tracker.SetHover(kMouse, nullptr), &b);
        EXPECT_EQ(tracker.GetHover(kMouse), nullptr);
        EXPECT_EQ(tracker.SetHover(kMouse, nullptr), nullptr);
    }

    TEST(PointerTrackerTest, HoversOfTwoPointersAreApart) {
        PointerTracker tracker;
        NullHandler a;
        NullHandler b;

        tracker.SetHover(kMouse, &a);
        tracker.SetHover(kFinger, &b);

        EXPECT_EQ(tracker.GetHover(kMouse), &a);
        EXPECT_EQ(tracker.GetHover(kFinger), &b);
        EXPECT_EQ(tracker.SetHover(kMouse, nullptr), &a);
        EXPECT_EQ(tracker.GetHover(kFinger), &b);
    }

    TEST(PointerTrackerTest, HoverOfANewPointerBeyondTheCapacityIsNotKept) {
        PointerTracker tracker;
        NullHandler handler;

        for (uint32_t which = 0; which < PointerTracker::kMaxEntries; ++which) {
            tracker.SetHover(which, &handler);
        }

        EXPECT_EQ(tracker.SetHover(100, &handler), nullptr);
        EXPECT_EQ(tracker.GetHover(100), nullptr);
        EXPECT_EQ(tracker.GetHover(0), &handler);
    }

    TEST(PointerTrackerTest, ForgetDropsEveryCaptureAndHoverOfTheHandlerOnly) {
        PointerTracker tracker;
        NullHandler gone;
        NullHandler kept;

        tracker.BeginCapture(kMouse, kLeft, gone);
        tracker.BeginCapture(kMouse, kRight, kept);
        tracker.BeginCapture(kFinger, kLeft, gone);
        tracker.SetHover(kMouse, &gone);
        tracker.SetHover(kFinger, &kept);

        tracker.Forget(gone);

        EXPECT_EQ(tracker.FindCapture(kMouse, kLeft), nullptr);
        EXPECT_EQ(tracker.FindCapture(kFinger, kLeft), nullptr);
        EXPECT_EQ(tracker.FindCapture(kMouse, kRight), &kept);
        EXPECT_EQ(tracker.GetHover(kMouse), nullptr);
        EXPECT_EQ(tracker.GetHover(kFinger), &kept);
        EXPECT_EQ(tracker.CaptureCount(), 1u);
    }

    TEST(PointerTrackerTest, ClearDropsEverything) {
        PointerTracker tracker;
        NullHandler a;

        tracker.BeginCapture(kMouse, kLeft, a);
        tracker.SetHover(kMouse, &a);
        tracker.Clear();

        EXPECT_EQ(tracker.FindCapture(kMouse, kLeft), nullptr);
        EXPECT_EQ(tracker.GetHover(kMouse), nullptr);
        EXPECT_EQ(tracker.CaptureCount(), 0u);
    }

    TEST(HitClipTest, NoClipContainsEverything) {
        const HitClip clip;

        EXPECT_TRUE(clip.Contains(-1000.0f, 1000.0f));
    }

    TEST(HitClipTest, RectContainsItsInsideAndEdges) {
        HitClip clip;
        clip.rect = Rect{10.0f, 20.0f, 100.0f, 50.0f};

        EXPECT_TRUE(clip.Contains(50.0f, 40.0f));
        EXPECT_TRUE(clip.Contains(10.0f, 20.0f));
        EXPECT_TRUE(clip.Contains(110.0f, 70.0f));
        EXPECT_FALSE(clip.Contains(9.9f, 40.0f));
        EXPECT_FALSE(clip.Contains(110.1f, 40.0f));
        EXPECT_FALSE(clip.Contains(50.0f, 19.9f));
        EXPECT_FALSE(clip.Contains(50.0f, 70.1f));
    }

    TEST(HitClipTest, EmptyClipContainsNothing) {
        HitClip clip;
        clip.rect = Rect{10.0f, 20.0f, 0.0f, 0.0f};
        clip.empty = true;

        EXPECT_FALSE(clip.Contains(10.0f, 20.0f));
    }

    TEST(HitClipTest, NullRendererMeansNoClip) {
        const HitClip clip = HitClip::FromRenderer(nullptr);

        EXPECT_FALSE(clip.rect.has_value());
        EXPECT_FALSE(clip.empty);
        EXPECT_TRUE(clip.Contains(5.0f, 5.0f));
    }

    TEST(HitClipTest, TakesThePushedClipOfTheRenderer) {
        RecordingRenderer renderer;

        EXPECT_TRUE(HitClip::FromRenderer(&renderer).Contains(500.0f, 500.0f));

        renderer.PushClipRect({10.0f, 10.0f, 40.0f, 40.0f});
        const HitClip clip = HitClip::FromRenderer(&renderer);
        renderer.PopClipRect();

        ASSERT_TRUE(clip.rect.has_value());
        EXPECT_FLOAT_EQ(clip.rect->w, 40.0f);
        EXPECT_TRUE(clip.Contains(30.0f, 30.0f));
        EXPECT_FALSE(clip.Contains(60.0f, 30.0f));
        EXPECT_TRUE(HitClip::FromRenderer(&renderer).Contains(60.0f, 30.0f));
    }

    TEST(HitClipTest, ClipsThatDoNotOverlapHideEverything) {
        RecordingRenderer renderer;

        renderer.PushClipRect({0.0f, 0.0f, 10.0f, 10.0f});
        renderer.PushClipRect({100.0f, 100.0f, 10.0f, 10.0f});
        const HitClip clip = HitClip::FromRenderer(&renderer);

        EXPECT_TRUE(clip.empty);
        EXPECT_FALSE(clip.Contains(5.0f, 5.0f));
        EXPECT_FALSE(clip.Contains(105.0f, 105.0f));
    }

}  // namespace BECore::Tests
