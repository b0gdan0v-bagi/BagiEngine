#include <BECore/PoolString/PoolStringMonitor.h>
#include <BECore/PoolStringTestSupport.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/Wait.h>

namespace BECore::Tests {

    namespace {

        using Details::PoolEntryBytes;

        constexpr ProfileSite kZone{"pool monitor zone", "Load", "tests/PoolStringMonitorTest.cpp", 1, 0};
        constexpr ProfileSite kOtherZone{"pool monitor other zone", "Update", "tests/PoolStringMonitorTest.cpp", 2, 0};

        /// Makes `zone` the thread's innermost zone, as a `ProfileZone` of a build that tracks zones does
        class CurrentZone {
        public:
            explicit CurrentZone(const ProfileSite* zone) : _previous(AllocTracker::ExchangeCurrentZone(zone)) {}
            ~CurrentZone() {
                AllocTracker::ExchangeCurrentZone(_previous);
            }
            CurrentZone(const CurrentZone&) = delete;
            CurrentZone& operator=(const CurrentZone&) = delete;

        private:
            const ProfileSite* _previous;
        };

        void Intern(IsolatedPool& pool, StringView text) {
            static_cast<void>(pool.storage.Intern(text));
        }

        Vector<PoolGrowthFrame> Frames(const PoolStringMonitor& monitor, size_t count = PoolStringMonitor::kFrameHistory) {
            Vector<PoolGrowthFrame> frames;
            monitor.ForEachRecent(count, [&frames](const PoolGrowthFrame& frame) { frames.push_back(frame); });
            return frames;
        }

        struct Grouped {
            Vector<PoolStringRecord> records;
            Vector<PoolZoneGroup> groups;

            explicit Grouped(const PoolStringMonitor& monitor) {
                monitor.GroupByZone(records, groups);
            }

            /// The group of `zone`; an empty one when the zone has no record
            PoolZoneGroup Of(const ProfileSite* zone) const {
                for (const PoolZoneGroup& group : groups) {
                    if (group.zone == zone) {
                        return group;
                    }
                }
                return {};
            }

            String Texts(const ProfileSite* zone) const {
                const PoolZoneGroup group = Of(zone);
                String texts;
                for (size_t i = group.first; i < group.first + group.count; ++i) {
                    texts += Format("{}@{};", records[i].text.ToStringView(), records[i].frame);
                }
                return texts;
            }
        };

    }  // namespace

    TEST(PoolStringMonitorTest, AFrameCountsWhatWasAddedSinceTheEndOfTheFrameBefore) {
        IsolatedPool pool;
        Intern(pool, "growth_baseline");
        PoolStringMonitor monitor(pool.storage);

        Intern(pool, "growth_before_the_first_frame");
        monitor.BeginFrame(7);
        Intern(pool, "growth_in_the_frame");
        Intern(pool, "growth_baseline");  // a hit
        PoolBlock block = pool.storage.AllocateBlock(PoolEntryBytes(1));
        Details::ConstructPoolEntry(block.Data(), StringUtils::GetHash("a"), "a");
        ASSERT_TRUE(pool.storage.AdoptBlock(block, PoolEntryBytes(1), {}, {}).has_value());
        monitor.EndFrame();
        monitor.BeginFrame(8);
        monitor.EndFrame();
        Intern(pool, "growth_between_frames");
        monitor.BeginFrame(9);
        monitor.EndFrame();

        const Vector<PoolGrowthFrame> frames = Frames(monitor);
        ASSERT_EQ(frames.size(), 3u);
        EXPECT_EQ(frames[0].frame, 7u);
        EXPECT_EQ(frames[0].runtimeStrings, 2u);
        EXPECT_EQ(frames[0].runtimeBytes, PoolEntryBytes(StringView("growth_before_the_first_frame").size()) + PoolEntryBytes(StringView("growth_in_the_frame").size()));
        EXPECT_EQ(frames[0].otherStrings, 1u);
        EXPECT_EQ(frames[1].runtimeStrings, 0u);
        EXPECT_EQ(frames[1].runtimeBytes, 0u);
        EXPECT_EQ(frames[1].otherStrings, 0u);
        EXPECT_EQ(frames[2].frame, 9u);
        EXPECT_EQ(frames[2].runtimeStrings, 1u);
        EXPECT_EQ(Back(Frames(monitor, 1)).frame, 9u);
        EXPECT_TRUE(Grouped(monitor).records.empty()) << "nothing is recorded unless asked";
    }

    TEST(PoolStringMonitorTest, OnlyTheLastFramesAreKept) {
        IsolatedPool pool;
        PoolStringMonitor monitor(pool.storage);
        constexpr uint64_t kExtra = 3;
        constexpr uint64_t kLast = PoolStringMonitor::kFrameHistory + kExtra;
        for (uint64_t frame = 1; frame <= kLast; ++frame) {
            monitor.BeginFrame(frame);
            monitor.EndFrame();
        }

        EXPECT_EQ(monitor.RecentCount(kLast), PoolStringMonitor::kFrameHistory);
        const Vector<PoolGrowthFrame> frames = Frames(monitor, kLast);
        ASSERT_EQ(frames.size(), PoolStringMonitor::kFrameHistory);
        for (size_t i = 0; i < frames.size(); ++i) {
            ASSERT_EQ(frames[i].frame, kExtra + 1 + i);
        }
    }

    TEST(PoolStringMonitorTest, ARecordHasTheZoneAndTheFrameAndGroupsAreByZone) {
        IsolatedPool pool;
        PoolStringMonitor monitor(pool.storage);
        monitor.SetRecording(true);
        monitor.BeginFrame(3);
        {
            const CurrentZone zone(&kZone);
            Intern(pool, "record_first");
            {
                const CurrentZone inner(&kOtherZone);
                Intern(pool, "record_inner");
            }
            Intern(pool, "record_second");
            Intern(pool, "record_first");  // a hit
        }
        monitor.EndFrame();
        {
            const CurrentZone none(nullptr);
            Intern(pool, "record_between");
        }

        const Grouped grouped(monitor);
        ASSERT_EQ(grouped.groups.size(), 3u);
        EXPECT_EQ(grouped.groups[0].zone, &kZone) << "the most bytes first";
        EXPECT_EQ(grouped.Texts(&kZone), "record_first@3;record_second@3;");
        EXPECT_EQ(grouped.Texts(&kOtherZone), "record_inner@3;");
        EXPECT_EQ(grouped.Texts(nullptr), "record_between@4;");
        EXPECT_EQ(grouped.Of(&kZone).bytes, PoolEntryBytes(StringView("record_first").size()) + PoolEntryBytes(StringView("record_second").size()));
        EXPECT_EQ(grouped.Of(nullptr).lastFrame, 4u);
    }

    TEST(PoolStringMonitorTest, RecordingOffKeepsTheRecordsAndTakesNoMore) {
        IsolatedPool pool;
        PoolStringMonitor monitor(pool.storage);
        monitor.SetRecording(true);
        Intern(pool, "switch_on");
        monitor.SetRecording(false);
        EXPECT_FALSE(monitor.IsRecording());
        Intern(pool, "switch_off");
        EXPECT_EQ(Grouped(monitor).records.size(), 1u);

        monitor.SetRecording(true);
        Intern(pool, "switch_on_again");
        EXPECT_EQ(Grouped(monitor).records.size(), 2u);

        monitor.ClearRecords();
        EXPECT_TRUE(Grouped(monitor).records.empty());
        EXPECT_TRUE(Grouped(monitor).groups.empty());
    }

    TEST(PoolStringMonitorTest, StringsPastTheCapAreCountedAsDropped) {
        IsolatedPool pool;
        PoolStringMonitor monitor(pool.storage);
        monitor.SetRecording(true);
        constexpr size_t kOver = 5;
        for (size_t i = 0; i < PoolStringMonitor::kMaxRecords + kOver; ++i) {
            Intern(pool, CounterName("cap_", i));
        }

        EXPECT_EQ(Grouped(monitor).records.size(), PoolStringMonitor::kMaxRecords);
        EXPECT_EQ(monitor.GetDroppedCount(), kOver);
        monitor.ClearRecords();
        EXPECT_EQ(monitor.GetDroppedCount(), 0u);
    }

    TEST(PoolStringMonitorTest, StringsOfSeveralThreadsAreAllRecorded) {
        IsolatedPool pool;
        PoolStringMonitor monitor(pool.storage);
        monitor.SetRecording(true);
        constexpr size_t kPerThread = 500;
        const Array<String, 2> prefixes = {"thread_a_", "thread_b_"};

        EXPECT_TRUE(RunThreadsTogether(2, kWaitLimit, [&](int thread) {
            const CurrentZone zone(thread == 0 ? &kZone : &kOtherZone);
            for (size_t i = 0; i < kPerThread; ++i) {
                Intern(pool, CounterName(prefixes[thread], i));
            }
        }));

        const Grouped grouped(monitor);
        EXPECT_EQ(grouped.records.size(), 2 * kPerThread);
        EXPECT_EQ(grouped.Of(&kZone).count, kPerThread);
        EXPECT_EQ(grouped.Of(&kOtherZone).count, kPerThread);
    }

    TEST(PoolStringMonitorTest, AMonitorThatDiesWhileRecordingStopsListening) {
        IsolatedPool pool;
        {
            PoolStringMonitor monitor(pool.storage);
            monitor.SetRecording(true);
        }
        Intern(pool, "after_the_monitor");  // a call into the dead monitor is what a sanitizer would report
        EXPECT_EQ(pool.storage.GetStats().stringCount, 1u);
    }

    TEST(PoolStringMonitorTest, AProfileZoneIsTheZoneOfARecord) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << "profiler zones are tracked with ENGINE_PROFILER_ALLOC only";
        }
        IsolatedPool pool;
        PoolStringMonitor monitor(pool.storage);
        monitor.SetRecording(true);
        {
            BE_PROFILE_SCOPE("pool monitor real zone");
            Intern(pool, "real_zone");
        }

        const Grouped grouped(monitor);
        const PoolZoneGroup group = At(grouped.groups, 0);
        ASSERT_NE(group.zone, nullptr);
        EXPECT_EQ(StringView(group.zone->name), "pool monitor real zone");
    }

    TEST(PoolStringMonitorTest, ASteadyFrameAllocatesNothing) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << "allocations are counted with ENGINE_PROFILER_ALLOC only";
        }
        IsolatedPool pool;
        PoolStringMonitor monitor(pool.storage);
        monitor.SetRecording(true);
        Intern(pool, "steady");  // the miss takes the first block
        Intern(pool, "steady");  // the first hit of the thread makes its counter

        const uint64_t before = AllocTracker::GetThreadCounters().count;
        for (uint64_t frame = 1; frame <= PoolStringMonitor::kFrameHistory + 1; ++frame) {
            monitor.BeginFrame(frame);
            Intern(pool, "steady");
            monitor.EndFrame();
        }
        EXPECT_EQ(AllocTracker::GetThreadCounters().count, before);
    }

}  // namespace BECore::Tests
