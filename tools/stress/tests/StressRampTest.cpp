#include <BECore/Control/ControlParams.h>
#include <Common/JsonDoc.h>
#include <Stress/RampController.h>
#include <gtest/gtest.h>

/// The frame times are synthetic: a function of the count shown.
namespace BECore::Stress::Tests {

    namespace {

        /// The measure of one frame shown at @p count; @p visit is how many times the controller has switched to this count so far (1 for the first)
        using CostSignature = double(uint32_t count, uint32_t visit);
        using CostOf = MoveOnlyFunction<CostSignature>;
        using CostRef = FunctionRef<CostSignature>;

        struct RampRun {
            RampResult result;
            Vector<uint32_t> counts;  // the count shown for every frame fed, in order
        };

        /// A frame whose measured time is @p ms, whichever the measure
        StressSample FrameOf(double ms) {
            StressSample sample;
            sample.workMs = ms;
            sample.cpuMs = ms;
            return sample;
        }

        /// Feeds the controller the cost of every frame it asks for until it is done (or @p frameLimit frames went)
        RampRun Drive(RampController& controller, CostRef cost, uint32_t frameLimit = 100000) {
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
                controller.OnFrame(FrameOf(cost(count, visits[count])));
            }
            run.result = controller.GetResult();
            return run;
        }

        RampRun Drive(CostRef cost, uint32_t maxFrames = kMaxRampFrames) {
            RampController controller(StressMeasure::Work, kDefaultRampBudgetMs, maxFrames);
            return Drive(controller, cost);
        }

        CostOf Linear(double msPerObject) {
            return [msPerObject](uint32_t count, uint32_t) { return msPerObject * count; };
        }

        /// The largest count whose frame fits the budget in Linear(@p msPerObject)
        uint32_t LinearLimit(double msPerObject) {
            return static_cast<uint32_t>(kDefaultRampBudgetMs / msPerObject);
        }

        /// One step of a controller shown @p count: the settle frames at @p settleMs and the measured ones from @p window
        RampNext RunStep(RampController& controller, double settleMs, const Array<double, kMeasureFrames>& window) {
            RampNext next;
            for (uint32_t i = 0; i < kSettleFrames; ++i) {
                next = controller.OnFrame(FrameOf(settleMs));
            }
            for (const double ms : window) {
                next = controller.OnFrame(FrameOf(ms));
            }
            return next;
        }

        /// A mark of one step, so a published mean says which step it is of
        double StepMark(uint32_t count, uint32_t visit) {
            return count * 100.0 + visit;
        }

        /// Like Drive, but every frame also carries the mark of its step as its update time (the settle frames one that no mean may hold)
        RampRun DriveMarked(CostRef cost, uint32_t maxFrames = kMaxRampFrames) {
            RampController controller(StressMeasure::Work, kDefaultRampBudgetMs, maxFrames);
            RampRun run;
            HashMap<uint32_t, uint32_t> visits;
            uint32_t shown = 0;
            uint32_t framesShown = 0;
            while (!controller.IsDone()) {
                const uint32_t count = controller.GetCount();
                run.counts.push_back(count);
                if (count != shown) {
                    shown = count;
                    framesShown = 0;
                    ++visits[count];
                } else if (framesShown == kSettleFrames + kMeasureFrames) {
                    framesShown = 0;  // the same count again: the re-measure of the result
                    ++visits[count];
                }
                StressSample sample = FrameOf(cost(count, visits[count]));
                sample.updateMs = framesShown++ < kSettleFrames ? -1.0e9 : StepMark(count, visits[count]);
                controller.OnFrame(sample);
            }
            run.result = controller.GetResult();
            return run;
        }

        /// How many times @p run showed @p count for a whole step
        uint32_t StepsAt(const RampRun& run, uint32_t count) {
            return static_cast<uint32_t>(eastl::count(run.counts.begin(), run.counts.end(), count)) / (kSettleFrames + kMeasureFrames);
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

    TEST(StressRampTest, HalfTheBudgetFindsAboutHalfTheCount) {
        RampController whole(StressMeasure::Work, 16.6);
        RampController half(StressMeasure::Work, 8.3);
        const uint32_t atWhole = Drive(whole, Linear(0.01)).result.objectsAt60fps;
        const uint32_t atHalf = Drive(half, Linear(0.01)).result.objectsAt60fps;

        EXPECT_LE(atHalf, 830u) << "8.3 ms at 0.01 ms an object";
        EXPECT_NEAR(static_cast<double>(atHalf), atWhole / 2.0, atWhole / 2.0 * 2 * kRampPrecision);
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
        RampController controller(StressMeasure::Work, kDefaultRampBudgetMs);
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
        RampController forgiving(StressMeasure::Work, kDefaultRampBudgetMs);
        const RampNext grown = RunStep(forgiving, 1.0, Window(1.0, 1));
        EXPECT_EQ(grown.count, 150u) << "p95 of 30 frames is the 29th: one outlier is above it";
        EXPECT_FALSE(grown.done);

        RampController strict(StressMeasure::Work, kDefaultRampBudgetMs);
        const RampNext failed = RunStep(strict, 1.0, Window(1.0, 2));
        EXPECT_TRUE(failed.done) << "two outliers are the 29th and the 30th: the step fails, and it was the start";
        EXPECT_EQ(strict.GetResult().objectsAt60fps, 0u);
    }

    TEST(StressRampTest, TheSettleFramesAreThrownAway) {
        RampController controller(StressMeasure::Work, kDefaultRampBudgetMs);
        const RampNext next = RunStep(controller, 1000.0, Window(1.0, 0));
        EXPECT_EQ(next.count, 150u) << "ten frames of 1000 ms before the window did not fail the step";
    }

    TEST(StressRampTest, TheStepsAndFramesAreCounted) {
        RampController controller(StressMeasure::Work, kDefaultRampBudgetMs);
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
        // A count above kMaxCount / kRampGrowth grows past kMaxCount: the step is clamped to it, not wrapped
        RampController controller(StressMeasure::Work, kDefaultRampBudgetMs);
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
        RampController controller(StressMeasure::Work, kDefaultRampBudgetMs);
        Drive(controller, [](uint32_t, uint32_t) { return 20.0; });
        ASSERT_TRUE(controller.IsDone());
        const RampResult before = controller.GetResult();

        const RampNext after = controller.OnFrame(FrameOf(1.0));

        EXPECT_TRUE(after.done);
        EXPECT_EQ(controller.GetResult().objectsAt60fps, before.objectsAt60fps);
        EXPECT_EQ(controller.GetResult().frames, before.frames);
    }

    TEST(StressRampTest, TheStagesAreTheMeansOfTheVerifyingStep) {
        const RampRun run = DriveMarked(Linear(0.05));

        ASSERT_TRUE(run.result.verified);
        const uint32_t found = run.result.objectsAt60fps;
        ASSERT_EQ(StepsAt(run, found), 2u) << "the search passed it and the re-measure ran it again";
        EXPECT_DOUBLE_EQ(run.result.stages.updateMs, StepMark(found, 2));
    }

    TEST(StressRampTest, TheStagesOfASteppedDownResultAreOfItsOwnStep) {
        constexpr double kMsPerObject = 0.05;
        const uint32_t found = Drive(Linear(kMsPerObject)).result.objectsAt60fps;

        const RampRun run = DriveMarked([&](uint32_t count, uint32_t visit) { return (count == found && visit >= 2) ? 50.0 : kMsPerObject * count; });

        ASSERT_LT(run.result.objectsAt60fps, found);
        EXPECT_DOUBLE_EQ(run.result.stages.updateMs, StepMark(run.result.objectsAt60fps, 1));
    }

    TEST(StressRampTest, TheStagesOfAnUnverifiedResultAreOfTheStepThatFailedIt) {
        constexpr double kMsPerObject = 0.05;
        bool verifying = false;
        const RampRun run = DriveMarked([&](uint32_t count, uint32_t visit) {
            verifying = verifying || visit >= 2;
            return verifying ? 50.0 : kMsPerObject * count;
        });

        ASSERT_FALSE(run.result.verified);
        ASSERT_GT(run.result.objectsAt60fps, 0u);
        EXPECT_DOUBLE_EQ(run.result.stages.updateMs, StepMark(run.result.objectsAt60fps, 1));
    }

    TEST(StressRampTest, TheStagesOfAnAbortedRampAreOfTheStepThatPassedItsCount) {
        // 100, 150, 225 and 337 pass, 505 fails the budget of 16.6 ms, and the limit falls in the step after it
        constexpr uint32_t kStep = kSettleFrames + kMeasureFrames;
        const RampRun run = DriveMarked(Linear(0.04), 5 * kStep + kStep / 2);

        ASSERT_TRUE(run.result.aborted);
        ASSERT_EQ(run.result.objectsAt60fps, 337u);
        EXPECT_DOUBLE_EQ(run.result.stages.updateMs, StepMark(337, 1)) << "neither the failed step after it nor the half step of the abort";
    }

    TEST(StressRampTest, ACountOfZeroHasNoStages) {
        const RampRun failed = DriveMarked([](uint32_t, uint32_t) { return 20.0; });
        ASSERT_EQ(failed.result.objectsAt60fps, 0u);
        EXPECT_EQ(failed.result.stages.updateMs, 0.0);

        const RampRun aborted = DriveMarked(Linear(0.001), kSettleFrames);
        ASSERT_TRUE(aborted.result.aborted);
        ASSERT_EQ(aborted.result.objectsAt60fps, 0u);
        EXPECT_EQ(aborted.result.stages.updateMs, 0.0);
    }

    TEST(StressRampTest, EveryStageIsAMeanAndTheGpuTimeIsOfTheFramesThatHaveOne) {
        RampController controller(StressMeasure::Work, kDefaultRampBudgetMs, kSettleFrames + kMeasureFrames);
        for (uint32_t i = 0; i < kSettleFrames + kMeasureFrames; ++i) {
            const bool odd = i % 2 == 1;
            StressSample sample = FrameOf(1.0);
            sample.updateMs = odd ? 3.0 : 1.0;
            sample.drawMs = odd ? 5.0 : 3.0;
            sample.submitMs = odd ? 7.0 : 5.0;
            sample.presentMs = odd ? 9.0 : 7.0;
            sample.waitMs = odd ? 11.0 : 9.0;
            sample.gpuMs = odd ? 6.0 : 0.0;
            controller.OnFrame(sample);
        }
        controller.OnFrame(FrameOf(1.0));  // past the limit: the count that passed is published

        const RampResult& result = controller.GetResult();
        ASSERT_EQ(result.objectsAt60fps, kRampStart);
        EXPECT_DOUBLE_EQ(result.stages.updateMs, 2.0);
        EXPECT_DOUBLE_EQ(result.stages.drawMs, 4.0);
        EXPECT_DOUBLE_EQ(result.stages.submitMs, 6.0);
        EXPECT_DOUBLE_EQ(result.stages.presentMs, 8.0);
        EXPECT_DOUBLE_EQ(result.stages.waitMs, 10.0);
        EXPECT_DOUBLE_EQ(result.stages.gpuMs, 6.0) << "half the frames had no GPU time";
    }

    TEST(StressRampTest, TheLineHasEveryKeyInSortedOrder) {
        RampResult result;
        result.objectsAt60fps = 4321;
        result.steps = 17;
        result.frames = 680;
        result.verified = true;
        result.stages = {.updateMs = 1.0, .drawMs = 2.0, .submitMs = 3.0, .presentMs = 4.0, .waitMs = 5.0, .gpuMs = 6.0};
        RampRunContext context;
        context.budgetMs = 8.5;
        context.scene = StressScene::Entities;
        context.atlases = 4;
        context.manualClock = true;

        const String line = FormatRampLine(result, StressMeasure::Cpu, context);

        const BECore::Tests::JsonDoc parsed(line);
        ASSERT_TRUE(parsed.IsParsed()) << line.c_str();
        EXPECT_EQ(GetParamString(parsed.Root(), "mode").value_or(""), "ramp");
        EXPECT_EQ(GetParamString(parsed.Root(), "scene").value_or(""), "entities");
        EXPECT_EQ(GetParamString(parsed.Root(), "measure").value_or(""), "cpu");
        EXPECT_EQ(GetParamInt(parsed.Root(), "objectsAt60fps").value_or(0), 4321);
        EXPECT_DOUBLE_EQ(GetParamNumber(parsed.Root(), "budgetMs").value_or(0.0), 8.5);
        EXPECT_EQ(GetParamBool(parsed.Root(), "capped"), Optional(false));
        EXPECT_EQ(GetParamBool(parsed.Root(), "aborted"), Optional(false));
        EXPECT_EQ(GetParamBool(parsed.Root(), "verified"), Optional(true));
        EXPECT_EQ(GetParamInt(parsed.Root(), "steps").value_or(0), 17);
        EXPECT_EQ(GetParamInt(parsed.Root(), "settleFrames").value_or(0), kSettleFrames);
        EXPECT_EQ(GetParamInt(parsed.Root(), "measureFrames").value_or(0), kMeasureFrames);
        EXPECT_EQ(GetParamBool(parsed.Root(), "manualClock"), Optional(true));
        constexpr const char* kStageKeys[] = {"updateMs", "drawMs", "submitMs", "presentMs", "waitMs", "gpuMs"};
        for (size_t i = 0; i < std::size(kStageKeys); ++i) {
            EXPECT_DOUBLE_EQ(GetParamNumber(parsed.Root(), kStageKeys[i]).value_or(0.0), static_cast<double>(i + 1)) << kStageKeys[i];
        }

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
        EXPECT_EQ(keys.size(), parsed.Root().Size());
        for (size_t i = 1; i < keys.size(); ++i) {
            EXPECT_LT(keys[i - 1], keys[i]);
        }
    }

}  // namespace BECore::Stress::Tests
