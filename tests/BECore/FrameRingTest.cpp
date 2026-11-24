#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/Ring/FrameRingBackend.h>
#include <BECore/Profiler/Ring/RingPause.h>
#include <BECore/Profiler/ScopedTrace.h>
#include <Common/ProfilerTestSupport.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        constexpr ProfileSite kOuterSite{"Outer", "Test", "FrameRingTest.cpp", 1, 0};
        constexpr ProfileSite kInnerSite{"Inner", "Test", "FrameRingTest.cpp", 2, 0};
        constexpr ProfileSite kLeafSite{"Leaf", "Test", "FrameRingTest.cpp", 3, 0};

        constexpr double kNanosecondsPerTestTick = 1.0e6;  // a test tick is one millisecond

        /// The clock of a ring under test: the test sets the tick before each call
        std::atomic<uint64_t> g_manualTick{0};
        uint64_t ManualNow() {
            return g_manualTick.load();
        }
        /// A clock that counts, so every zone event of the writing thread gets its own tick
        std::atomic<uint64_t> g_countingTick{1};
        uint64_t CountingNow() {
            return g_countingTick.fetch_add(1);
        }

        /// A clock that, when armed, takes a snapshot of g_ringToSnapshot from inside the call: EndZone reads it right after claiming a slot
        FrameRingBackend* g_ringToSnapshot = nullptr;
        RingSnapshot g_snapshotInsideEnd;
        uint64_t SnapshottingNow() {
            if (g_ringToSnapshot != nullptr) {
                g_snapshotInsideEnd = g_ringToSnapshot->Snapshot();
                g_ringToSnapshot = nullptr;
            }
            return g_manualTick.load();
        }

        FrameRingBackend::Options ManualOptions() {
            g_manualTick = 0;
            return {.now = &ManualNow, .nanosecondsPerTick = kNanosecondsPerTestTick};
        }

        /// A whole zone of @p site from tick @p begin to tick @p end
        void RecordZone(FrameRingBackend& ring, const ProfileSite& site, uint64_t begin, uint64_t end, ZoneAllocs allocs = {}) {
            g_manualTick = begin;
            const uint64_t token = ring.BeginZone(site);
            g_manualTick = end;
            ring.EndZone(token, site, allocs);
        }

        const RingThread* FindThread(const RingSnapshot& snapshot, uint32_t threadId) {
            for (const RingThread& thread : snapshot.threads) {
                if (thread.threadId == threadId) {
                    return &thread;
                }
            }
            return nullptr;
        }

    }  // namespace

    TEST(FrameRingTest, NestedZonesComeBackWithTheirTimesAndDepth) {
        FrameRingBackend ring(ManualOptions());
        g_manualTick = 10;
        const uint64_t outer = ring.BeginZone(kOuterSite);
        g_manualTick = 12;
        const uint64_t inner = ring.BeginZone(kInnerSite);
        g_manualTick = 15;
        ring.EndZone(inner, kInnerSite, {.count = 3, .bytes = 96});
        g_manualTick = 20;
        ring.EndZone(outer, kOuterSite, {.count = 5, .bytes = 160});

        const RingSnapshot snapshot = ring.Snapshot();
        ASSERT_EQ(snapshot.threads.size(), 1u);
        EXPECT_EQ(snapshot.threads[0].threadId, Profiler::CurrentThreadId());
        const auto& zones = snapshot.threads[0].zones;
        ASSERT_EQ(zones.size(), 2u);

        // Sorted by begin tick: the parent first
        EXPECT_EQ(zones[0].site, &kOuterSite);
        EXPECT_EQ(zones[0].beginTicks, 10u);
        EXPECT_EQ(zones[0].endTicks, 20u);
        EXPECT_EQ(zones[0].depth, 0u);
        EXPECT_EQ(zones[0].allocCount, 5u);
        EXPECT_EQ(zones[0].allocBytes, 160u);
        EXPECT_EQ(zones[1].site, &kInnerSite);
        EXPECT_EQ(zones[1].beginTicks, 12u);
        EXPECT_EQ(zones[1].endTicks, 15u);
        EXPECT_EQ(zones[1].depth, 1u);
        EXPECT_EQ(zones[1].allocCount, 3u);
        EXPECT_EQ(zones[1].allocBytes, 96u);

        EXPECT_DOUBLE_EQ(snapshot.DurationMs(zones[0]), 10.0);
        EXPECT_DOUBLE_EQ(snapshot.TicksToMs(3), 3.0);
        EXPECT_EQ(snapshot.dropped, 0u);
    }

    TEST(FrameRingTest, AParentComesBeforeItsChildOnTheSameBeginTick) {
        FrameRingBackend ring(ManualOptions());
        g_manualTick = 7;
        const uint64_t outer = ring.BeginZone(kOuterSite);
        const uint64_t inner = ring.BeginZone(kInnerSite);
        const uint64_t leaf = ring.BeginZone(kLeafSite);
        ring.EndZone(leaf, kLeafSite, {});
        ring.EndZone(inner, kInnerSite, {});
        ring.EndZone(outer, kOuterSite, {});

        const RingSnapshot snapshot = ring.Snapshot();
        ASSERT_EQ(snapshot.threads.size(), 1u);
        const auto& zones = snapshot.threads[0].zones;
        ASSERT_EQ(zones.size(), 3u);
        EXPECT_EQ(zones[0].site, &kOuterSite);
        EXPECT_EQ(zones[1].site, &kInnerSite);
        EXPECT_EQ(zones[2].site, &kLeafSite);
    }

    TEST(FrameRingTest, TheRingKeepsTheLastZonesOfAThread) {
        constexpr uint32_t kExtra = 100;
        FrameRingBackend ring(ManualOptions());
        for (uint32_t i = 0; i < FrameRingBackend::kEventsPerThread + kExtra; ++i) {
            RecordZone(ring, kOuterSite, uint64_t{i} * 10, uint64_t{i} * 10 + 5, {.count = i, .bytes = i * 2});
        }
        const RingSnapshot snapshot = ring.Snapshot();
        ASSERT_EQ(snapshot.threads.size(), 1u);
        const auto& zones = snapshot.threads[0].zones;
        ASSERT_EQ(zones.size(), FrameRingBackend::kEventsPerThread);
        for (size_t k = 0; k < zones.size(); ++k) {
            const uint64_t i = kExtra + k;  // the first kExtra zones fell out
            ASSERT_EQ(zones[k].beginTicks, i * 10) << k;
            ASSERT_EQ(zones[k].endTicks, i * 10 + 5) << k;
            ASSERT_EQ(zones[k].allocCount, i) << k;
            ASSERT_EQ(zones[k].allocBytes, i * 2) << k;
        }
    }

    TEST(FrameRingTest, MarksMakeFramesWithConsecutiveIndexes) {
        FrameRingBackend ring(ManualOptions());  // made at tick 0
        for (uint64_t i = 0; i < 3; ++i) {
            g_manualTick = (i + 1) * 16;
            ring.FrameMark();
        }
        const RingSnapshot snapshot = ring.Snapshot();
        ASSERT_EQ(snapshot.frames.size(), 3u);
        for (uint64_t i = 0; i < 3; ++i) {
            EXPECT_EQ(snapshot.frames[i].index, i);
            EXPECT_EQ(snapshot.frames[i].beginTicks, i * 16);
            EXPECT_EQ(snapshot.frames[i].endTicks, (i + 1) * 16);
        }
    }

    TEST(FrameRingTest, AReopenedRingShowsNoFrameOrZoneFromBeforeTheGap) {
        FrameRingBackend ring(ManualOptions());
        ring.Start();
        RecordZone(ring, kOuterSite, 1, 3);
        g_manualTick = 10;
        ring.FrameMark();
        g_manualTick = 5000;  // the ring was off for a long time
        ring.Start();
        RecordZone(ring, kInnerSite, 5001, 5004);
        g_manualTick = 5016;
        ring.FrameMark();
        g_manualTick = 5032;
        ring.FrameMark();

        const RingSnapshot snapshot = ring.Snapshot();
        ASSERT_EQ(snapshot.frames.size(), 2u);
        EXPECT_EQ(snapshot.frames[0].index, 1u);
        EXPECT_EQ(snapshot.frames[0].beginTicks, 5000u);  // the epoch's tick, not the mark before the gap
        EXPECT_EQ(snapshot.frames[0].endTicks, 5016u);
        EXPECT_EQ(snapshot.frames[1].index, 2u);
        EXPECT_EQ(snapshot.frames[1].beginTicks, 5016u);
        ASSERT_EQ(snapshot.threads.size(), 1u);
        ASSERT_EQ(snapshot.threads[0].zones.size(), 1u);
        EXPECT_EQ(snapshot.threads[0].zones[0].site, &kInnerSite);
    }

    TEST(FrameRingTest, ARingReopenedWithoutAMarkShowsNoFrame) {
        FrameRingBackend ring(ManualOptions());
        g_manualTick = 10;
        ring.FrameMark();
        g_manualTick = 99;
        ring.Start();
        EXPECT_TRUE(ring.Snapshot().frames.empty());
    }

    TEST(FrameRingTest, TheLastFramesKeepTheirNumbersAsOlderOnesFallOut) {
        constexpr uint32_t kExtra = 10;
        FrameRingBackend ring(ManualOptions());
        constexpr uint32_t kMarks = FrameRingBackend::kMaxFrames + kExtra;
        for (uint64_t i = 0; i < kMarks; ++i) {
            g_manualTick = (i + 1) * 10;
            ring.FrameMark();
        }
        const RingSnapshot snapshot = ring.Snapshot();
        // The oldest mark still held has lost its predecessor, so the frame it ends is not a whole one and is left out
        ASSERT_EQ(snapshot.frames.size(), FrameRingBackend::kMaxFrames - 1);
        EXPECT_EQ(snapshot.frames.front().index, uint64_t{kExtra} + 1);
        EXPECT_EQ(snapshot.frames.back().index, uint64_t{kMarks} - 1);
        for (size_t k = 1; k < snapshot.frames.size(); ++k) {
            EXPECT_EQ(snapshot.frames[k].index, snapshot.frames[k - 1].index + 1);
            EXPECT_EQ(snapshot.frames[k].beginTicks, snapshot.frames[k - 1].endTicks);
        }
        EXPECT_EQ(snapshot.frames.back().endTicks, uint64_t{kMarks} * 10);
    }

    TEST(FrameRingTest, AZoneThatCrossesAMarkBelongsToTheFrameItBeganIn) {
        FrameRingBackend ring(ManualOptions());
        g_manualTick = 5;
        const uint64_t token = ring.BeginZone(kOuterSite);
        g_manualTick = 10;
        ring.FrameMark();  // ends frame 0
        g_manualTick = 14;
        ring.EndZone(token, kOuterSite, {});
        g_manualTick = 20;
        ring.FrameMark();  // ends frame 1

        const RingSnapshot snapshot = ring.Snapshot();
        ASSERT_EQ(snapshot.frames.size(), 2u);
        ASSERT_EQ(snapshot.threads.size(), 1u);
        ASSERT_FALSE(snapshot.threads[0].zones.empty());
        const ZoneRecord& zone = snapshot.threads[0].zones[0];
        const RingFrame& first = snapshot.frames[0];
        const RingFrame& second = snapshot.frames[1];
        EXPECT_TRUE(zone.beginTicks >= first.beginTicks && zone.beginTicks < first.endTicks);
        EXPECT_GE(zone.endTicks, second.beginTicks);  // it ends in the next frame and stays with the first
    }

    TEST(FrameRingTest, AnEndWithoutABeginRecordsNothing) {
        FrameRingBackend ring(ManualOptions());
        ring.EndZone(0, kOuterSite, {});
        EXPECT_TRUE(ring.Snapshot().threads.empty() || ring.Snapshot().threads[0].zones.empty());

        // A ring started in the middle of a zone: the zone's end is forgotten, the next whole zone is recorded
        ring.BeginZone(kOuterSite);
        EXPECT_TRUE(ring.Start());
        ring.EndZone(0, kOuterSite, {});
        const RingSnapshot started = ring.Snapshot();
        ASSERT_EQ(started.threads.size(), 1u);
        EXPECT_TRUE(started.threads[0].zones.empty());
        RecordZone(ring, kInnerSite, 3, 4);
        const RingSnapshot recorded = ring.Snapshot();
        ASSERT_EQ(recorded.threads.size(), 1u);
        ASSERT_EQ(recorded.threads[0].zones.size(), 1u);
        EXPECT_EQ(recorded.threads[0].zones[0].site, &kInnerSite);
    }

    TEST(FrameRingTest, ADeeperZoneThanTheStackIsCountedAndNotRecorded) {
        FrameRingBackend ring(ManualOptions());
        constexpr uint32_t kDepth = FrameRingBackend::kMaxZoneDepth + 3;
        for (uint32_t i = 0; i < kDepth; ++i) {
            g_manualTick = i;
            ring.BeginZone(kOuterSite);
        }
        for (uint32_t i = 0; i < kDepth; ++i) {
            g_manualTick = 1000 + i;
            ring.EndZone(0, kOuterSite, {});
        }
        const RingSnapshot snapshot = ring.Snapshot();
        ASSERT_EQ(snapshot.threads.size(), 1u);
        ASSERT_EQ(snapshot.threads[0].zones.size(), size_t{FrameRingBackend::kMaxZoneDepth});
        EXPECT_EQ(snapshot.dropped, 3u);
        EXPECT_EQ(ring.GetDroppedCount(), 3u);
        // The recorded ones are the outer ones, with their own begin and end
        const auto& zones = snapshot.threads[0].zones;
        for (uint32_t i = 0; i < FrameRingBackend::kMaxZoneDepth; ++i) {
            EXPECT_EQ(zones[i].depth, i);
            EXPECT_EQ(zones[i].beginTicks, uint64_t{i});
            EXPECT_EQ(zones[i].endTicks, uint64_t{1000} + (kDepth - 1 - i));
        }
    }

    TEST(FrameRingTest, ZonesUnderAPauseLeaveNoZoneAndNoOrphan) {
        FrameRingBackend ring(ManualOptions());
        {
            const ScopedRingPause pause;
            RecordZone(ring, kOuterSite, 1, 2);
            ring.RecordZone(kLeafSite, 3, 4);
        }
        EXPECT_TRUE(ring.Snapshot().threads.empty() || ring.Snapshot().threads[0].zones.empty());

        // The pause pushed nothing, so a zone of the next scope sits at depth 0 and an end without a begin still records nothing
        RecordZone(ring, kInnerSite, 5, 6);
        ring.EndZone(0, kOuterSite, {});
        const RingSnapshot snapshot = ring.Snapshot();
        ASSERT_EQ(snapshot.threads.size(), 1u);
        const auto& zones = snapshot.threads[0].zones;
        ASSERT_EQ(zones.size(), 1u);
        EXPECT_EQ(zones[0].site, &kInnerSite);
        EXPECT_EQ(zones[0].depth, 0u);
        EXPECT_EQ(snapshot.dropped, 0u);
    }

    TEST(FrameRingTest, AnOuterZoneBegunBeforeAPauseIsRecordedWholeWithoutItsInnerZones) {
        FrameRingBackend ring(ManualOptions());
        g_manualTick = 10;
        const uint64_t outer = ring.BeginZone(kOuterSite);
        {
            const ScopedRingPause pause;
            g_manualTick = 12;
            const uint64_t inner = ring.BeginZone(kInnerSite);
            g_manualTick = 14;
            ring.EndZone(inner, kInnerSite, {});
        }
        g_manualTick = 15;
        const uint64_t after = ring.BeginZone(kLeafSite);
        g_manualTick = 17;
        ring.EndZone(after, kLeafSite, {});
        g_manualTick = 20;
        ring.EndZone(outer, kOuterSite, {});

        const RingSnapshot snapshot = ring.Snapshot();
        ASSERT_EQ(snapshot.threads.size(), 1u);
        const auto& zones = snapshot.threads[0].zones;
        ASSERT_EQ(zones.size(), 2u);
        EXPECT_EQ(zones[0].site, &kOuterSite);
        EXPECT_EQ(zones[0].beginTicks, 10u);
        EXPECT_EQ(zones[0].endTicks, 20u);
        EXPECT_EQ(zones[0].depth, 0u);
        EXPECT_EQ(zones[1].site, &kLeafSite);
        EXPECT_EQ(zones[1].depth, 1u);
    }

    TEST(FrameRingTest, NestedPausesCountAndFrameMarksAreNotPaused) {
        FrameRingBackend ring(ManualOptions());
        {
            const ScopedRingPause outer;
            {
                const ScopedRingPause inner;
            }
            EXPECT_TRUE(ScopedRingPause::IsActive());
            RecordZone(ring, kOuterSite, 1, 2);
            g_manualTick = 16;
            ring.FrameMark();
        }
        EXPECT_FALSE(ScopedRingPause::IsActive());
        const RingSnapshot snapshot = ring.Snapshot();
        EXPECT_EQ(snapshot.frames.size(), 1u);
        EXPECT_TRUE(snapshot.threads.empty() || snapshot.threads[0].zones.empty());
    }

    TEST(FrameRingTest, APauseOnOneThreadDoesNotStopAnother) {
        FrameRingBackend ring;
        const ScopedRingPause pause;
        std::thread other([&ring] {
            const uint64_t token = ring.BeginZone(kInnerSite);
            ring.EndZone(token, kInnerSite, {});
        });
        other.join();
        const uint64_t token = ring.BeginZone(kOuterSite);
        ring.EndZone(token, kOuterSite, {});

        const RingSnapshot snapshot = ring.Snapshot();
        size_t zones = 0;
        for (const RingThread& thread : snapshot.threads) {
            for (const ZoneRecord& zone : thread.zones) {
                EXPECT_EQ(zone.site, &kInnerSite);
                ++zones;
            }
        }
        EXPECT_EQ(zones, 1u);
    }

    TEST(FrameRingTest, EveryThreadGetsALaneWithItsProfilerName) {
        FrameRingBackend ring;
        uint32_t idA = 0;
        uint32_t idB = 0;
        const auto work = [&ring](const char* name, uint32_t sortIndex, uint32_t& id, const ProfileSite& site) {
            Profiler::SetThreadName(name, sortIndex);
            id = Profiler::CurrentThreadId();
            const uint64_t token = ring.BeginZone(site);
            ring.EndZone(token, site, {});
        };
        std::thread a(work, "Ring lane A", 7u, std::ref(idA), std::cref(kOuterSite));
        std::thread b(work, "Ring lane B", 9u, std::ref(idB), std::cref(kInnerSite));
        a.join();
        b.join();
        if (idA >= Profiler::kMaxThreads || idB >= Profiler::kMaxThreads) {
            GTEST_SKIP() << "the profiler names the first kMaxThreads threads of a process only, and this one has used them up";
        }

        const RingSnapshot snapshot = ring.Snapshot();
        ASSERT_EQ(snapshot.threads.size(), 2u);
        const RingThread* laneA = FindThread(snapshot, idA);
        const RingThread* laneB = FindThread(snapshot, idB);
        ASSERT_NE(laneA, nullptr);
        ASSERT_NE(laneB, nullptr);
        EXPECT_STREQ(laneA->name.c_str(), "Ring lane A");
        EXPECT_EQ(laneA->sortIndex, 7u);
        EXPECT_STREQ(laneB->name.c_str(), "Ring lane B");
        EXPECT_EQ(laneB->sortIndex, 9u);
        ASSERT_EQ(laneA->zones.size(), 1u);
        EXPECT_EQ(laneA->zones[0].site, &kOuterSite);
        ASSERT_EQ(laneB->zones.size(), 1u);
        EXPECT_EQ(laneB->zones[0].site, &kInnerSite);
    }

    TEST(FrameRingTest, AThreadWithoutAPlaceHasNoSortIndex) {
        FrameRingBackend ring(ManualOptions());
        RecordZone(ring, kOuterSite, 1, 2);
        const RingSnapshot snapshot = ring.Snapshot();
        ASSERT_EQ(snapshot.threads.size(), 1u);
        EXPECT_EQ(snapshot.threads[0].sortIndex, Profiler::GetThreadSortIndex(Profiler::CurrentThreadId()));
    }

    TEST(FrameRingTest, ASlotTheWriterHasClaimedIsLeftOutOfASnapshot) {
        constexpr uint32_t kExtra = 5;
        g_manualTick = 0;
        FrameRingBackend ring({.now = &SnapshottingNow, .nanosecondsPerTick = kNanosecondsPerTestTick});
        for (uint32_t i = 0; i < FrameRingBackend::kEventsPerThread + kExtra; ++i) {
            RecordZone(ring, kOuterSite, uint64_t{i} * 10, uint64_t{i} * 10 + 5);
        }
        const RingSnapshot full = ring.Snapshot();
        ASSERT_EQ(full.threads.size(), 1u);
        EXPECT_EQ(full.threads[0].zones.size(), FrameRingBackend::kEventsPerThread);

        // The next zone claims the slot of the oldest one; a snapshot taken at that moment must not trust it
        g_manualTick = 1;
        const uint64_t token = ring.BeginZone(kInnerSite);
        g_ringToSnapshot = &ring;
        ring.EndZone(token, kInnerSite, {});
        ASSERT_EQ(g_snapshotInsideEnd.threads.size(), 1u);
        const auto& inside = g_snapshotInsideEnd.threads[0].zones;
        ASSERT_EQ(inside.size(), size_t{FrameRingBackend::kEventsPerThread - 1});
        EXPECT_EQ(inside.front().beginTicks, uint64_t{kExtra + 1} * 10);

        // Once the zone is written the window is whole again, with the new zone in it
        const RingSnapshot after = ring.Snapshot();
        ASSERT_EQ(after.threads.size(), 1u);
        EXPECT_EQ(after.threads[0].zones.size(), size_t{FrameRingBackend::kEventsPerThread});
    }

    TEST(FrameRingTest, ASnapshotWhileAThreadRecordsNeverHoldsAZoneTheWriterDidNotWrite) {
        // The writer's zones are Outer [t, t + 3] around Inner [t + 1, t + 2] on a clock that counts, and it writes far more of them than the
        // ring holds, so the oldest slots are overwritten all the time the reader copies. A torn slot would show as a pair that does not fit
        // The writer goes on until the reader took enough snapshots (an optimized build writes the minimum before the reader gets its first one)
        constexpr uint32_t kPairs = FrameRingBackend::kEventsPerThread * 6;
        constexpr size_t kMinSnapshots = 50;
        constexpr std::chrono::seconds kTimeLimit{20};
        FrameRingBackend ring({.now = &CountingNow, .nanosecondsPerTick = kNanosecondsPerTestTick});
        std::atomic<bool> started{false};
        std::atomic<bool> done{false};
        std::atomic<bool> stop{false};
        std::atomic<size_t> snapshots{0};
        std::thread writer([&] {
            started = true;
            const auto deadline = std::chrono::steady_clock::now() + kTimeLimit;
            for (uint32_t i = 0; !stop && (i < kPairs || (snapshots.load() < kMinSnapshots && std::chrono::steady_clock::now() < deadline)); ++i) {
                const uint64_t outer = ring.BeginZone(kOuterSite);
                const uint64_t inner = ring.BeginZone(kInnerSite);
                ring.EndZone(inner, kInnerSite, {.count = 1, .bytes = 8});
                ring.EndZone(outer, kOuterSite, {.count = 3, .bytes = 40});
                if (i % 1000 == 0) {
                    ring.FrameMark();
                }
            }
            done = true;
        });
        while (!started) {
            std::this_thread::yield();
        }

        size_t zonesChecked = 0;
        const auto check = [&](const RingSnapshot& snapshot) {
            ASSERT_LE(snapshot.threads.size(), 1u);
            for (const RingThread& thread : snapshot.threads) {
                uint64_t lastBegin = 0;
                for (const ZoneRecord& zone : thread.zones) {
                    ASSERT_TRUE(zone.site == &kOuterSite || zone.site == &kInnerSite);
                    ASSERT_LE(zone.beginTicks, zone.endTicks);
                    ASSERT_GE(zone.beginTicks, lastBegin);
                    lastBegin = zone.beginTicks;
                    if (zone.site == &kOuterSite) {
                        ASSERT_EQ(zone.depth, 0u);
                        ASSERT_EQ(zone.endTicks, zone.beginTicks + 3);
                        ASSERT_EQ(zone.allocCount, 3u);
                        ASSERT_EQ(zone.allocBytes, 40u);
                    } else {
                        ASSERT_EQ(zone.depth, 1u);
                        ASSERT_EQ(zone.endTicks, zone.beginTicks + 1);
                        ASSERT_EQ(zone.allocCount, 1u);
                        ASSERT_EQ(zone.allocBytes, 8u);
                    }
                    ++zonesChecked;
                }
            }
            for (size_t k = 1; k < snapshot.frames.size(); ++k) {
                ASSERT_EQ(snapshot.frames[k].index, snapshot.frames[k - 1].index + 1);
                ASSERT_EQ(snapshot.frames[k].beginTicks, snapshot.frames[k - 1].endTicks);
            }
        };
        // No assertion returns before the join: the writer uses the ring on this stack, and a joinable thread that goes out of scope ends the process
        RingSnapshot reused;  // the refill form under a writer, as the Profiler panel takes it
        while (!done && !HasFatalFailure()) {
            ring.Snapshot(reused);
            check(reused);
            ++snapshots;
        }
        stop = true;
        writer.join();
        ASSERT_FALSE(HasFatalFailure()) << "a snapshot taken while the writer ran held a zone it did not write";
        ASSERT_NO_FATAL_FAILURE(check(ring.Snapshot()));
        EXPECT_GE(snapshots.load(), kMinSnapshots);
        EXPECT_GT(zonesChecked, 0u);
        const RingSnapshot last = ring.Snapshot();
        ASSERT_EQ(last.threads.size(), 1u);
        EXPECT_EQ(last.threads[0].zones.size(), FrameRingBackend::kEventsPerThread);
    }

    TEST(FrameRingTest, RefillingASnapshotGivesTheContentOfTheByValueForm) {
        FrameRingBackend ring(ManualOptions());
        RecordZone(ring, kOuterSite, 1, 9, {.count = 2, .bytes = 64});
        RecordZone(ring, kInnerSite, 2, 4);
        g_manualTick = 10;
        ring.FrameMark();

        RingSnapshot into;
        into.dropped = 77;  // stale content that the refill must replace
        ring.Snapshot(into);
        const RingSnapshot value = ring.Snapshot();
        EXPECT_EQ(into.dropped, value.dropped);
        EXPECT_EQ(into.nanosecondsPerTick, value.nanosecondsPerTick);
        ASSERT_EQ(into.frames.size(), value.frames.size());
        ASSERT_EQ(into.threads.size(), value.threads.size());
        ASSERT_EQ(into.threads[0].zones.size(), value.threads[0].zones.size());
        for (size_t i = 0; i < value.threads[0].zones.size(); ++i) {
            EXPECT_EQ(into.threads[0].zones[i].site, value.threads[0].zones[i].site);
            EXPECT_EQ(into.threads[0].zones[i].beginTicks, value.threads[0].zones[i].beginTicks);
            EXPECT_EQ(into.threads[0].zones[i].endTicks, value.threads[0].zones[i].endTicks);
        }
        ring.Snapshot(into);  // a second refill adds nothing to the first
        EXPECT_EQ(into.frames.size(), value.frames.size());
        EXPECT_EQ(into.threads.size(), value.threads.size());
        EXPECT_EQ(into.threads[0].zones.size(), value.threads[0].zones.size());
    }

    TEST(FrameRingTest, ARefillLeavesNoThreadOrZoneOfTheContentBefore) {
        FrameRingBackend busy(ManualOptions());
        RecordZone(busy, kOuterSite, 1, 2);
        std::thread other([&] { RecordZone(busy, kInnerSite, 3, 4); });
        other.join();
        RingSnapshot into;
        busy.Snapshot(into);
        ASSERT_EQ(into.threads.size(), 2u);

        FrameRingBackend quiet(ManualOptions());
        RecordZone(quiet, kLeafSite, 5, 6);
        quiet.Snapshot(into);
        ASSERT_EQ(into.threads.size(), 1u);
        ASSERT_EQ(into.threads[0].zones.size(), 1u);
        EXPECT_EQ(into.threads[0].zones[0].site, &kLeafSite);

        FrameRingBackend empty(ManualOptions());
        empty.Snapshot(into);
        EXPECT_TRUE(into.threads.empty());
        EXPECT_TRUE(into.frames.empty());
    }

    TEST(FrameRingTest, ARefillKeepsTheCapacityOfTheZoneLists) {
        constexpr uint32_t kZones = 40;
        FrameRingBackend ring(ManualOptions());
        for (uint32_t i = 0; i < kZones; ++i) {
            RecordZone(ring, kLeafSite, 10 + 2 * i, 11 + 2 * i);
        }
        RingSnapshot into;
        ring.Snapshot(into);
        ASSERT_EQ(into.threads.size(), 1u);
        ASSERT_EQ(into.threads[0].zones.size(), size_t{kZones});
        const ZoneRecord* storage = into.threads[0].zones.data();
        const size_t capacity = into.threads[0].zones.capacity();

        ring.Snapshot(into);  // the same window
        EXPECT_EQ(into.threads[0].zones.data(), storage);

        g_manualTick = 1000;
        EXPECT_TRUE(ring.Start());  // the whole window is copied and then cut to the epoch: nothing is left, the storage stays
        ring.Snapshot(into);
        ASSERT_EQ(into.threads.size(), 1u);
        EXPECT_TRUE(into.threads[0].zones.empty());
        EXPECT_EQ(into.threads[0].zones.data(), storage);
        EXPECT_EQ(into.threads[0].zones.capacity(), capacity);
    }

    TEST(FrameRingTest, AReusedSnapshotHoldsNoZoneOfAnEarlierEpoch) {
        FrameRingBackend ring(ManualOptions());
        RecordZone(ring, kOuterSite, 1, 2);
        RecordZone(ring, kOuterSite, 3, 4);
        RingSnapshot into;
        ring.Snapshot(into);
        ASSERT_EQ(into.threads[0].zones.size(), 2u);

        g_manualTick = 100;
        EXPECT_TRUE(ring.Start());
        RecordZone(ring, kInnerSite, 101, 102);
        ring.Snapshot(into);
        ASSERT_EQ(into.threads.size(), 1u);
        ASSERT_EQ(into.threads[0].zones.size(), 1u);
        EXPECT_EQ(into.threads[0].zones[0].site, &kInnerSite);
    }

    // ---- The ring beside the primary backend (Profiler::SetRingEnabled) ----

    namespace {

        constexpr ProfileSite kRingOnlySite{"Ring only", "Test", "FrameRingTest.cpp", 10, 0};
        constexpr ProfileSite kBothSite{"In both", "Test", "FrameRingTest.cpp", 11, 0};
        constexpr ProfileSite kSwitchedSite{"Spans the switch", "Test", "FrameRingTest.cpp", 12, 0};
        constexpr ProfileSite kPrimaryOnlySite{"Primary only", "Test", "FrameRingTest.cpp", 13, 0};

        /// The zones of @p site in the process's ring (all threads)
        size_t CountInRing(const ProfileSite& site) {
            return CountRingZones(Profiler::GetFrameRing(), [&site](const RingThread&, const ZoneRecord& zone) { return zone.site == &site; });
        }

        size_t CountText(const String& text, const char* needle) {
            size_t count = 0;
            for (size_t at = text.find(needle); at != String::npos; at = text.find(needle, at + 1)) {
                ++count;
            }
            return count;
        }

        size_t CountBegins(const ChromeTraceBackend& recorder) {
            return CountText(recorder.WriteJson(), "\"ph\":\"B\"");
        }

        size_t CountEnds(const ChromeTraceBackend& recorder) {
            return CountText(recorder.WriteJson(), "\"ph\":\"E\"");
        }

    }  // namespace

    TEST(ProfilerRingTest, WithTheRingOffTheBackendIsExactlyThePrimary) {
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        ASSERT_TRUE(Profiler::SetBackend(&recorder));
        EXPECT_EQ(Profiler::GetBackend(), &recorder);
        EXPECT_EQ(Profiler::GetPrimaryBackend(), &recorder);
        EXPECT_FALSE(Profiler::IsRingEnabled());

        Profiler::SetBackend(nullptr);
        EXPECT_EQ(Profiler::GetBackend(), nullptr);
        EXPECT_EQ(Profiler::GetPrimaryBackend(), nullptr);
    }

    TEST(ProfilerRingTest, TheRingAloneIsTheBackendWhenThereIsNoPrimary) {
        const ProfilerStateGuard guard;
        const size_t before = CountInRing(kRingOnlySite);
        Profiler::SetRingEnabled(true);
        EXPECT_TRUE(Profiler::IsRingEnabled());
        EXPECT_EQ(Profiler::GetBackend(), &Profiler::GetFrameRing());
        EXPECT_EQ(Profiler::GetPrimaryBackend(), nullptr);
        {
            const ProfileZone zone(kRingOnlySite);
        }
        EXPECT_EQ(CountInRing(kRingOnlySite), before + 1);

        Profiler::SetRingEnabled(false);
        EXPECT_EQ(Profiler::GetBackend(), nullptr);
        {
            const ProfileZone zone(kRingOnlySite);
        }
        EXPECT_EQ(CountInRing(kRingOnlySite), before + 1);  // off: nothing more is recorded
    }

    TEST(ProfilerRingTest, AZoneLandsInThePrimaryAndInTheRing) {
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        Profiler::SetBackend(&recorder);
        Profiler::SetRingEnabled(true);
        EXPECT_NE(Profiler::GetBackend(), &recorder);
        EXPECT_NE(Profiler::GetBackend(), &Profiler::GetFrameRing());
        EXPECT_EQ(Profiler::GetPrimaryBackend(), &recorder);

        const size_t before = CountInRing(kBothSite);
        {
            const ProfileZone zone(kBothSite);
        }
        EXPECT_EQ(CountInRing(kBothSite), before + 1);
        EXPECT_EQ(CountBegins(recorder), 1u);
        EXPECT_EQ(CountEnds(recorder), 1u);

        Profiler::SetRingEnabled(false);
        EXPECT_EQ(Profiler::GetBackend(), &recorder);
    }

    TEST(ProfilerRingTest, APausedRingStillLetsThePrimaryHaveEveryZone) {
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        Profiler::SetBackend(&recorder);
        Profiler::SetRingEnabled(true);
        const size_t before = CountInRing(kBothSite);
        {
            const ScopedRingPause pause;
            const ProfileZone zone(kBothSite);
        }
        EXPECT_EQ(CountInRing(kBothSite), before);
        EXPECT_EQ(CountBegins(recorder), 1u);
        EXPECT_EQ(CountEnds(recorder), 1u);
    }

    TEST(ProfilerRingTest, TheSameFanOutServesAPrimaryEveryTimeTheRingComesBack) {
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        Profiler::SetBackend(&recorder);
        Profiler::SetRingEnabled(true);
        IProfilerBackend* const first = Profiler::GetBackend();
        Profiler::SetRingEnabled(false);
        Profiler::SetRingEnabled(true);
        EXPECT_EQ(Profiler::GetBackend(), first);
    }

    TEST(ProfilerRingTest, ChangingThePrimaryKeepsTheRing) {
        ChromeTraceBackend first;
        ChromeTraceBackend second;
        const ProfilerStateGuard guard;
        Profiler::SetBackend(&first);
        Profiler::SetRingEnabled(true);
        ASSERT_TRUE(Profiler::SetBackend(&second));
        EXPECT_TRUE(Profiler::IsRingEnabled());
        EXPECT_EQ(Profiler::GetPrimaryBackend(), &second);

        const size_t before = CountInRing(kBothSite);
        {
            const ProfileZone zone(kBothSite);
        }
        EXPECT_EQ(CountInRing(kBothSite), before + 1);
        EXPECT_EQ(CountBegins(second), 1u);
        EXPECT_EQ(CountBegins(first), 0u);

        // No primary at all: the ring goes on alone
        Profiler::SetBackend(nullptr);
        EXPECT_EQ(Profiler::GetBackend(), &Profiler::GetFrameRing());
    }

    TEST(ProfilerRingTest, AZoneEndsOnTheBackendItBeganOnWhenTheRingGoesOff) {
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        Profiler::SetBackend(&recorder);
        Profiler::SetRingEnabled(true);
        const size_t before = CountInRing(kSwitchedSite);
        {
            Optional<ProfileZone> zone;
            zone.emplace(kSwitchedSite);
            Profiler::SetRingEnabled(false);
            zone.reset();  // ends on the fan-out it began on, not on the bare recorder
        }
        EXPECT_EQ(CountBegins(recorder), 1u);
        EXPECT_EQ(CountEnds(recorder), 1u);
        EXPECT_EQ(CountInRing(kSwitchedSite), before + 1);
    }

    TEST(ProfilerRingTest, AZoneBegunBeforeTheRingWasOnEndsOnThePrimaryOnly) {
        ChromeTraceBackend recorder;
        const ProfilerStateGuard guard;
        Profiler::SetBackend(&recorder);
        const size_t before = CountInRing(kPrimaryOnlySite);
        {
            Optional<ProfileZone> zone;
            zone.emplace(kPrimaryOnlySite);
            Profiler::SetRingEnabled(true);
            zone.reset();
        }
        EXPECT_EQ(CountBegins(recorder), 1u);
        EXPECT_EQ(CountEnds(recorder), 1u);
        EXPECT_EQ(CountInRing(kPrimaryOnlySite), before);
    }

    TEST(ProfilerRingTest, ScopedTraceWithTheRingOnRestoresThePrimary) {
        ChromeTraceBackend original;
        const ProfilerStateGuard guard;
        Profiler::SetBackend(&original);
        Profiler::SetRingEnabled(true);

        const Path folder(TestTempDirectory("profiler_ring"));
        MakeDirectory(folder);
        {
            const ScopedTrace trace((folder / "trace.json").View());
            EXPECT_EQ(Profiler::GetPrimaryBackend(), &Profiler::GetChromeTrace());
            EXPECT_TRUE(Profiler::IsRingEnabled());
        }
        EXPECT_EQ(Profiler::GetPrimaryBackend(), &original);
        EXPECT_TRUE(Profiler::IsRingEnabled());
        RemoveDirectory(folder);
    }

#if BE_PROFILER

    TEST(ProfilerRingTest, TheMacrosReachTheRing) {
        const ProfilerStateGuard guard;
        // The macro makes a site of its own, so the zone is found by name
        const auto countByName = [](const char* name) {
            size_t count = 0;
            for (const RingThread& thread : Profiler::GetFrameRing().Snapshot().threads) {
                for (const ZoneRecord& zone : thread.zones) {
                    count += StringView(zone.site->name) == name ? 1 : 0;
                }
            }
            return count;
        };
        Profiler::SetRingEnabled(true);
        const size_t zonesBefore = countByName("Ring macro zone");
        const size_t framesBefore = Profiler::GetFrameRing().Snapshot().frames.size();
        {
            BE_PROFILE_SCOPE("Ring macro zone");
        }
        BE_PROFILE_FRAME();
        EXPECT_EQ(countByName("Ring macro zone"), zonesBefore + 1);
        EXPECT_GT(Profiler::GetFrameRing().Snapshot().frames.size(), framesBefore);
    }

#endif

}  // namespace BECore::Tests
