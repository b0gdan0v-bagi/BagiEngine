#include <BECore/Animation/TweenManager.h>
#include <BECore/Profiler/ProfilerClock.h>
#include <Stress/EntityField.h>
#include <cstdio>
#include <gtest/gtest.h>
#include <tests/StressSceneFixture.h>

namespace BECore::Stress::Tests {

    namespace {

        /// A field that costs milliseconds a frame, well above the noise of the clock, and is still built twice within seconds
        constexpr uint32_t kAgingCount = 100000;
        constexpr size_t kYoungFrames = 30;
        constexpr size_t kOldFrames = 60;
        /// The game time before the old frames: every node has been through at least two turns of its orbit
        constexpr double kAgingSeconds = 2.0 * kOrbitMaxSeconds;
        constexpr double kNanosPerMilli = 1e6;

        TweenManager& Tweens() {
            return CoreManager::GetTweens();
        }

    }  // namespace

    class StressEntitiesAgingTest : public StressSceneFixture {
    protected:
        void TearDown() override {
            StressSceneFixture::TearDown();
            Tweens().CancelAll();  // a failed test must not leave tweens for the next one
        }

        /// What a frame of the scene costs the CPU: the update of the nodes and the tweens
        void Frame() {
            Update();
            Tweens().Advance(static_cast<float>(kFrame));
        }

        double MedianFrameMs(size_t frames) {
            Vector<uint64_t> ticks;
            ticks.reserve(frames);
            for (size_t i = 0; i < frames; ++i) {
                const uint64_t start = ProfilerClock::Now();
                Frame();
                ticks.push_back(ProfilerClock::Now() - start);
            }
            eastl::sort(ticks.begin(), ticks.end());
            return ProfilerClock::ToNanoseconds(ticks[ticks.size() / 2]) / kNanosPerMilli;
        }
    };

    /// Prints what an old field costs against a young one. The field built a second time is the control: it stays slow when the machine slowed down
    /// during the run, and also when what slowed the frame outlives a field (a pool's free list)
    TEST_F(StressEntitiesAgingTest, DISABLED_AnOldFieldAgainstAYoungOne) {
        ASSERT_NO_FATAL_FAILURE(StartGame({"--stress-scene=entities", Format("--stress-count={}", kAgingCount).c_str()}));
        Frame();  // builds the field: not timed
        ASSERT_EQ(Tweens().GetActiveCount(), size_t{kAgingCount});
        const double youngMs = MedianFrameMs(kYoungFrames);

        for (size_t i = 0; i < static_cast<size_t>(kAgingSeconds / kFrame); ++i) {
            Frame();
        }
        const double oldMs = MedianFrameMs(kOldFrames);

        SetCount(0);
        Frame();  // drops the nodes and cancels their tweens; the walk of the same frame compacts the list
        ASSERT_EQ(Tweens().GetActiveCount(), 0u);
        SetCount(kAgingCount);
        Frame();
        ASSERT_EQ(Tweens().GetActiveCount(), size_t{kAgingCount});
        const double youngAgainMs = MedianFrameMs(kYoungFrames);

        std::printf("EntitiesAging count=%u young_ms=%.3f old_ms=%.3f young2_ms=%.3f old_over_young=%.3f\n", kAgingCount, youngMs, oldMs, youngAgainMs, oldMs / youngMs);
    }

}  // namespace BECore::Stress::Tests
