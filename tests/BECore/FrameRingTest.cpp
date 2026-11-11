#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/Ring/FrameRingBackend.h>
#include <atomic>
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
        const ZoneRecord& zone = snapshot.threads[0].zones.at(0);
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
        EXPECT_TRUE(ring.Snapshot().threads[0].zones.empty());
        RecordZone(ring, kInnerSite, 3, 4);
        ASSERT_EQ(ring.Snapshot().threads[0].zones.size(), 1u);
        EXPECT_EQ(ring.Snapshot().threads[0].zones[0].site, &kInnerSite);
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
        EXPECT_EQ(snapshot.threads[0].zones.size(), size_t{FrameRingBackend::kMaxZoneDepth});
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
        EXPECT_EQ(ring.Snapshot().threads[0].zones.size(), FrameRingBackend::kEventsPerThread);

        // The next zone claims the slot of the oldest one; a snapshot taken at that moment must not trust it
        g_manualTick = 1;
        const uint64_t token = ring.BeginZone(kInnerSite);
        g_ringToSnapshot = &ring;
        ring.EndZone(token, kInnerSite, {});
        const auto& inside = g_snapshotInsideEnd.threads[0].zones;
        ASSERT_EQ(inside.size(), size_t{FrameRingBackend::kEventsPerThread - 1});
        EXPECT_EQ(inside.front().beginTicks, uint64_t{kExtra + 1} * 10);

        // Once the zone is written the window is whole again, with the new zone in it
        const RingSnapshot after = ring.Snapshot();
        EXPECT_EQ(after.threads[0].zones.size(), size_t{FrameRingBackend::kEventsPerThread});
    }

    TEST(FrameRingTest, ASnapshotWhileAThreadRecordsNeverHoldsAZoneTheWriterDidNotWrite) {
        // The writer's zones are Outer [t, t + 3] around Inner [t + 1, t + 2] on a clock that counts, and it writes far more of them than the
        // ring holds, so the oldest slots are overwritten all the time the reader copies. A torn slot would show as a pair that does not fit
        constexpr uint32_t kPairs = FrameRingBackend::kEventsPerThread * 6;
        FrameRingBackend ring({.now = &CountingNow, .nanosecondsPerTick = kNanosecondsPerTestTick});
        std::atomic<bool> started{false};
        std::atomic<bool> done{false};
        std::thread writer([&] {
            started = true;
            for (uint32_t i = 0; i < kPairs; ++i) {
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

        size_t snapshots = 0;
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
        while (!done) {
            ASSERT_NO_FATAL_FAILURE(check(ring.Snapshot()));
            ++snapshots;
        }
        writer.join();
        ASSERT_NO_FATAL_FAILURE(check(ring.Snapshot()));
        EXPECT_GT(snapshots, 0u);
        EXPECT_GT(zonesChecked, 0u);
        const RingSnapshot last = ring.Snapshot();
        EXPECT_EQ(last.threads[0].zones.size(), FrameRingBackend::kEventsPerThread);
    }

}  // namespace BECore::Tests
