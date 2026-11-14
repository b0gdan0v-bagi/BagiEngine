#include <BECore/Control/Json.h>
#include <Stress/RampController.h>
#include <gtest/gtest.h>

/**
 * Coverage of the ramp's search with synthetic frame times as a function of the count: a linear cost, a cliff, a machine that is too fast and
 * one that is too slow, the p95 of a step (one outlier is forgiven, two are not), the settle frames, the final re-measure that steps down, the
 * abort guard, counts always in range, and the same answer for the same frames.
 */
namespace BECore::Stress::Tests {

    namespace {

        /// The measure of one frame shown at @p count; @p visit is how many times the controller has switched to this count so far (1 for the first)
        using CostOf = std::function<double(uint32_t count, uint32_t visit)>;

        struct RampRun {
            RampResult result;
            Vector<uint32_t> counts;  // the count shown for every frame fed, in order
        };

        /// Feeds the controller the cost of every frame it asks for until it is done (or @p frameLimit frames went)
        RampRun Drive(RampController& controller, const CostOf& cost, uint32_t frameLimit = 100000) {
            RampRun run;
            HashMap<uint32_t, uint32_t> visits;
            uint32_t shown = 0;
            for (uint32_t frame = 0; frame < frameLimit && !controller.IsDone(); ++frame) {
                const uint32_t count = controller.GetCount();
                run.counts.push_back(count);
                if (count != shown) {
                    shown = count;
                    ++visits[count];
                }
                controller.OnFrame(cost(count, visits[count]));
            }
            run.result = controller.GetResult();
            return run;
        }

        RampRun Drive(const CostOf& cost, uint32_t maxFrames = kMaxRampFrames) {
            RampController controller(StressMeasure::Work, maxFrames);
            return Drive(controller, cost);
        }

        CostOf Linear(double msPerObject) {
            return [msPerObject](uint32_t count, uint32_t) { return msPerObject * count; };
        }

        /// The largest count whose frame fits the budget in Linear(@p msPerObject)
        uint32_t LinearLimit(double msPerObject) {
            return static_cast<uint32_t>(kRampBudgetMs / msPerObject);
        }

        /// One step of a controller shown @p count: the settle frames at @p settleMs and the measured ones from @p window
        RampNext RunStep(RampController& controller, double settleMs, const Array<double, kMeasureFrames>& window) {
            RampNext next;
            for (uint32_t i = 0; i < kSettleFrames; ++i) {
                next = controller.OnFrame(settleMs);
            }
            for (const double ms : window) {
                next = controller.OnFrame(ms);
            }
            return next;
        }

        Array<double, kMeasureFrames> Window(double ms, uint32_t outliers) {
            Array<double, kMeasureFrames> window;
            window.fill(ms);
            for (uint32_t i = 0; i < outliers; ++i) {
                window[kMeasureFrames - 1 - i * 3] = 100.0;
            }
            return window;
        }

    }  // namespace

    TEST(StressRampTest, ALinearCostGivesTheLimitWithinThePrecision) {
        for (const double msPerObject : {0.001, 0.01, 0.1}) {
            const RampRun run = Drive(Linear(msPerObject));
            const uint32_t limit = LinearLimit(msPerObject);
            EXPECT_LE(run.result.objectsAt60fps, limit) << msPerObject << ": a count that does not fit was published";
            EXPECT_GE(run.result.objectsAt60fps, static_cast<uint32_t>(limit * (1.0 - 2 * kRampPrecision))) << msPerObject;
            EXPECT_TRUE(run.result.verified) << msPerObject;
            EXPECT_FALSE(run.result.capped);
            EXPECT_FALSE(run.result.aborted);
            EXPECT_GT(run.result.steps, 3u);
        }
    }

    TEST(StressRampTest, ACliffIsFoundJustBelowItsEdge) {
        constexpr uint32_t kEdge = 777;
        const RampRun run = Drive([](uint32_t count, uint32_t) { return count <= kEdge ? 1.0 : 50.0; });
        EXPECT_LE(run.result.objectsAt60fps, kEdge);
        EXPECT_GE(run.result.objectsAt60fps, static_cast<uint32_t>(kEdge * (1.0 - 2 * kRampPrecision)));
        EXPECT_TRUE(run.result.verified);
    }

    TEST(StressRampTest, AMachineThatIsFastEnoughIsCappedAtTheLargestCount) {
        const RampRun run = Drive([](uint32_t, uint32_t) { return 1.0; });
        EXPECT_EQ(run.result.objectsAt60fps, kMaxCount);
        EXPECT_TRUE(run.result.capped);
        EXPECT_FALSE(run.result.aborted);
        EXPECT_TRUE(run.result.verified);
    }

    TEST(StressRampTest, AFrameOverTheBudgetAtTheStartIsZero) {
        const RampRun run = Drive([](uint32_t, uint32_t) { return 20.0; });
        EXPECT_EQ(run.result.objectsAt60fps, 0u);
        EXPECT_FALSE(run.result.capped);
        EXPECT_EQ(run.result.steps, 1u);
        EXPECT_EQ(run.result.frames, kSettleFrames + kMeasureFrames);
    }

    TEST(StressRampTest, TheCountGrowsByAHalfAndAtLeastOne) {
        RampController controller(StressMeasure::Work);
        Vector<uint32_t> shown;
        shown.push_back(controller.GetCount());
        for (int step = 0; step < 4; ++step) {
            const RampNext next = RunStep(controller, 1.0, Window(1.0, 0));
            shown.push_back(next.count);
        }
        const Vector<uint32_t> expected = {100, 150, 225, 337, 505};
        EXPECT_EQ(shown, expected);
    }

    TEST(StressRampTest, OneSlowFrameOfAStepIsForgivenAndTwoAreNot) {
        RampController forgiving(StressMeasure::Work);
        const RampNext grown = RunStep(forgiving, 1.0, Window(1.0, 1));
        EXPECT_EQ(grown.count, 150u) << "p95 of 30 frames is the 29th: one outlier is above it";
        EXPECT_FALSE(grown.done);

        RampController strict(StressMeasure::Work);
        const RampNext failed = RunStep(strict, 1.0, Window(1.0, 2));
        EXPECT_TRUE(failed.done) << "two outliers are the 29th and the 30th: the step fails, and it was the start";
        EXPECT_EQ(strict.GetResult().objectsAt60fps, 0u);
    }

    TEST(StressRampTest, TheSettleFramesAreThrownAway) {
        RampController controller(StressMeasure::Work);
        const RampNext next = RunStep(controller, 1000.0, Window(1.0, 0));
        EXPECT_EQ(next.count, 150u) << "ten frames of 1000 ms before the window did not fail the step";
    }

    TEST(StressRampTest, TheStepsAndFramesAreCounted) {
        RampController controller(StressMeasure::Work);
        RunStep(controller, 1.0, Window(1.0, 0));
        RunStep(controller, 1.0, Window(1.0, 0));
        EXPECT_EQ(controller.GetResult().steps, 2u);
        EXPECT_EQ(controller.GetResult().frames, 2 * (kSettleFrames + kMeasureFrames));
    }

    TEST(StressRampTest, TheFinalRemeasureStepsDownWhenItFails) {
        constexpr double kMsPerObject = 0.05;
        const RampRun plain = Drive(Linear(kMsPerObject));
        ASSERT_TRUE(plain.result.verified);
        const uint32_t found = plain.result.objectsAt60fps;

        // The same machine, but the count the search found fails the second time it is shown (noise)
        const RampRun flaky = Drive([&](uint32_t count, uint32_t visit) { return (count == found && visit >= 2) ? 50.0 : kMsPerObject * count; });

        EXPECT_LT(flaky.result.objectsAt60fps, found);
        EXPECT_GE(flaky.result.objectsAt60fps, static_cast<uint32_t>(found * (1.0 - 2 * kRampPrecision)));
        EXPECT_TRUE(flaky.result.verified);
        EXPECT_EQ(flaky.result.steps, plain.result.steps + 1) << "one more step: the lower count's own re-measure";
    }

    TEST(StressRampTest, TheStepDownsAreLimitedAndAnUnverifiedResultSaysSo) {
        constexpr double kMsPerObject = 0.05;
        const RampRun plain = Drive(Linear(kMsPerObject));
        ASSERT_TRUE(plain.result.verified);

        // From the first count that is shown a second time (the verification of the result) on, every frame is over the budget
        bool verifying = false;
        const RampRun noisy = Drive([&](uint32_t count, uint32_t visit) {
            verifying = verifying || visit >= 2;
            return verifying ? 50.0 : kMsPerObject * count;
        });

        EXPECT_FALSE(noisy.result.verified);
        EXPECT_LT(noisy.result.objectsAt60fps, plain.result.objectsAt60fps);
        EXPECT_EQ(noisy.result.steps, plain.result.steps + kMaxStepDowns) << "the first re-measure was in the plain run; three step-downs more";
        EXPECT_GE(noisy.result.objectsAt60fps, plain.result.objectsAt60fps / 2) << "stepped down by the resolution of the search, not to nothing";
    }

    TEST(StressRampTest, ARunPastTheFrameLimitIsAbortedWithTheBestCountSoFar) {
        const RampRun run = Drive(Linear(0.01), 400);

        EXPECT_TRUE(run.result.aborted);
        EXPECT_FALSE(run.result.verified);
        EXPECT_EQ(run.result.frames, 401u) << "the frame that went over the limit ends it";
        EXPECT_GE(run.result.objectsAt60fps, kRampStart) << "a count that passed before the stop";
        EXPECT_LE(run.result.objectsAt60fps, LinearLimit(0.01));
    }

    TEST(StressRampTest, TheRealLimitIsHighEnoughForTheLongestSearch) {
        // The slowest way to end: every count passes up to the cap, which takes the most steps of any ramp
        const RampRun capped = Drive([](uint32_t, uint32_t) { return 1.0; });
        EXPECT_FALSE(capped.result.aborted);
        EXPECT_LT(capped.result.frames, kMaxRampFrames / 4);
    }

    TEST(StressRampTest, EveryCountItShowsIsInRange) {
        const CostOf costs[] = {Linear(0.0001), Linear(0.05), Linear(10.0), [](uint32_t, uint32_t) { return 1.0; }, [](uint32_t count, uint32_t) { return count < 5000 ? 1.0 : 99.0; }};
        for (const CostOf& cost : costs) {
            const RampRun run = Drive(cost);
            for (const uint32_t count : run.counts) {
                EXPECT_GE(count, 1u);
                EXPECT_LE(count, kMaxCount);
            }
            EXPECT_LE(run.result.objectsAt60fps, kMaxCount);
        }
    }

    TEST(StressRampTest, TheCountNearTheLimitDoesNotOverflow) {
        // 666'667 x 1.5 is above kMaxCount: the step is clamped to it, not wrapped
        RampController controller(StressMeasure::Work);
        const RampRun run = Drive(controller, [](uint32_t, uint32_t) { return 1.0; });
        uint32_t previous = 0;
        for (const uint32_t count : run.counts) {
            EXPECT_GE(count, previous) << "a machine that never fails only ever goes up";
            previous = count;
        }
        EXPECT_EQ(previous, kMaxCount);
    }

    TEST(StressRampTest, TheSameFramesGiveTheSameAnswer) {
        const CostOf cost = [](uint32_t count, uint32_t) { return 0.013 * count + ((count % 7) == 0 ? 0.4 : 0.0); };
        const RampRun first = Drive(cost);
        const RampRun second = Drive(cost);

        EXPECT_EQ(first.result.objectsAt60fps, second.result.objectsAt60fps);
        EXPECT_EQ(first.result.steps, second.result.steps);
        EXPECT_EQ(first.result.frames, second.result.frames);
        EXPECT_EQ(first.counts, second.counts);
    }

    TEST(StressRampTest, ADoneControllerStaysDone) {
        RampController controller(StressMeasure::Work);
        Drive(controller, [](uint32_t, uint32_t) { return 20.0; });
        ASSERT_TRUE(controller.IsDone());
        const RampResult before = controller.GetResult();

        const RampNext after = controller.OnFrame(1.0);

        EXPECT_TRUE(after.done);
        EXPECT_EQ(controller.GetResult().objectsAt60fps, before.objectsAt60fps);
        EXPECT_EQ(controller.GetResult().frames, before.frames);
    }

    TEST(StressRampTest, TheLineHasEveryKeyInSortedOrder) {
        RampResult result;
        result.objectsAt60fps = 4321;
        result.steps = 17;
        result.frames = 680;
        result.verified = true;
        RampRunContext context;
        context.scene = StressScene::Entities;
        context.atlases = 4;
        context.manualClock = true;

        const String line = FormatRampLine(result, StressMeasure::Cpu, context);

        const auto parsed = ParseJson(line);
        ASSERT_TRUE(parsed.has_value()) << line.c_str();
        EXPECT_EQ(GetJsonString(*parsed, "mode").value_or(""), "ramp");
        EXPECT_EQ(GetJsonString(*parsed, "scene").value_or(""), "entities");
        EXPECT_EQ(GetJsonString(*parsed, "measure").value_or(""), "cpu");
        EXPECT_EQ(GetJsonInt(*parsed, "objectsAt60fps").value_or(0), 4321);
        EXPECT_DOUBLE_EQ(GetJsonNumber(*parsed, "budgetMs").value_or(0.0), 16.6);
        EXPECT_EQ(GetJsonBool(*parsed, "capped"), Optional(false));
        EXPECT_EQ(GetJsonBool(*parsed, "aborted"), Optional(false));
        EXPECT_EQ(GetJsonBool(*parsed, "verified"), Optional(true));
        EXPECT_EQ(GetJsonInt(*parsed, "steps").value_or(0), 17);
        EXPECT_EQ(GetJsonInt(*parsed, "settleFrames").value_or(0), kSettleFrames);
        EXPECT_EQ(GetJsonInt(*parsed, "measureFrames").value_or(0), kMeasureFrames);
        EXPECT_EQ(GetJsonBool(*parsed, "manualClock"), Optional(true));

        // The keys are in byte order, one object on one line starting with a brace
        EXPECT_EQ(line.front(), '{');
        EXPECT_EQ(line.find('\n'), String::npos);
        Vector<String> keys;
        for (size_t at = line.find('"'); at != String::npos;) {
            const size_t end = line.find('"', at + 1);
            if (end != String::npos && end + 1 < line.size() && line[end + 1] == ':') {
                keys.push_back(line.substr(at + 1, end - at - 1));
            }
            at = line.find('"', end + 1);
        }
        EXPECT_EQ(keys.size(), parsed->size());
        for (size_t i = 1; i < keys.size(); ++i) {
            EXPECT_LT(keys[i - 1], keys[i]);
        }
    }

}  // namespace BECore::Stress::Tests
