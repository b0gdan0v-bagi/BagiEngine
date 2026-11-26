#include <BECore/Application/RunMode.h>
#include <BECore/Control/ControlService.h>
#include <BECore/PoolString/PoolBlock.h>
#include <BECore/PoolString/PoolStringMonitor.h>
#include <BECore/PoolString/PoolStringStorage.h>
#include <BECore/PoolString/PoolStringsControl.h>
#include <BECore/PoolStringTestSupport.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <Common/ControlTestSupport.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Common/Require.h>

namespace BECore::Tests {

    namespace {

        using Details::ConstructPoolEntry;
        using Details::PoolEntryBytes;

        constexpr StringView kCookedSource = "config/Control.bin";
        constexpr StringView kForcedSource = "config/Forced.bin";
        constexpr uint64_t kSharedHash = 42;
        constexpr ProfileSite kZone{"pool strings zone", "Load", "tests/PoolStringsControlTest.cpp", 7, 0};

        struct Hashed {
            StringView text;
            uint64_t hash;
        };

        Hashed Right(StringView text) {
            return {text, StringUtils::GetHash(text)};
        }

        /// Adopts entries with the given hashes, unverified: how a pool comes to hold what `Intern` never would
        void Adopt(Details::PoolStringStorage& storage, const Vector<Hashed>& entries, StringView source) {
            size_t bytes = 0;
            for (const Hashed& entry : entries) {
                bytes += PoolEntryBytes(entry.text.size());
            }
            PoolBlock block = storage.AllocateBlock(bytes);
            size_t offset = 0;
            for (const Hashed& entry : entries) {
                ConstructPoolEntry(block.Data() + offset, entry.hash, entry.text);
                offset += PoolEntryBytes(entry.text.size());
            }
            PoolAdoptOptions options;
            options.verifyHashes = false;
            options.sourceName = source;
            ASSERT_TRUE(storage.AdoptBlock(block, bytes, {}, options).has_value());
        }

        int64_t Number(JsonView object, StringView key) {
            return IntOf(object, key).value_or(-1);
        }

        Vector<JsonView> ItemsOf(JsonView object, StringView key) {
            Vector<JsonView> items;
            for (const JsonView item : MemberOf(object, key).Items()) {
                items.push_back(item);
            }
            return items;
        }

        /// The keys of an object as written, `a;b;`
        String KeysOf(JsonView object) {
            String keys;
            for (const auto& member : object.Members()) {
                keys += Format("{};", member.key.Text().value_or(StringView()));
            }
            return keys;
        }

        /// The `text` of every search row of the answer, `a;b;`
        String FoundTexts(const ControlReply& reply) {
            String texts;
            for (const JsonView row : ItemsOf(MemberOf(MemberOf(reply.View(), "result"), "search"), "rows")) {
                texts += Format("{};", StringOf(row, "text"));
            }
            return texts;
        }

    }  // namespace

    /// A pool with one static string, one runtime string, a cooked block with one more string and a duplicate, and a block
    /// of wrong hashes: `pool_collision_a` and `_b` share one, `pool_twice` is held under two
    class PoolStringsControlTest : public ::testing::Test {
    protected:
        void SetUp() override {
            const Details::PoolStringEntry* entry = ConstructPoolEntry(staticMemory, StringUtils::GetHash("pool_static"), "pool_static");
            PoolString registered;
            const PoolStaticRegistration registration{entry, &registered};
            pool.storage.RegisterStatic({&registration, 1});
            static_cast<void>(pool.storage.Intern("pool_runtime"));
            Adopt(pool.storage, {Right("pool_adopted"), Right("pool_runtime")}, kCookedSource);
            Adopt(pool.storage, {{"pool_collision_a", kSharedHash}, {"pool_collision_b", kSharedHash}, Right("pool_twice"), {"pool_twice", kSharedHash + 1}}, kForcedSource);
            monitor = MakeUnique<PoolStringMonitor>(pool.storage);
            service = MakeUnique<ControlService>();
            RegisterPoolStringsControl(*service, *monitor);
        }

        ControlReply Call(StringView paramsText = R"({})") {
            return CallControlText(*service, 1, "pool_strings", paramsText);
        }

        static JsonView Section(const ControlReply& reply, StringView key) {
            return MemberOf(MemberOf(reply.View(), "result"), key);
        }

        alignas(Details::kPoolEntryAlignment) char staticMemory[64]{};  // outlives the pool that points at it
        IsolatedPool pool;
        UniquePtr<PoolStringMonitor> monitor;
        UniquePtr<ControlService> service;
    };

    TEST_F(PoolStringsControlTest, TheSummaryHasTheStatisticsOfThePool) {
        const ControlReply reply = Call();
        ASSERT_TRUE(reply.IsOk()) << String(reply.Text()).c_str();
        EXPECT_EQ(KeysOf(MemberOf(reply.View(), "result")), "checks;growth;recording;search;summary;");

        const JsonView summary = Section(reply, "summary");
        EXPECT_EQ(KeysOf(summary), "adopted;batchCalls;batchStrings;blockBytes;blockCount;blocks;deadBytes;deadStrings;heapFallbackBlocks;hits;internCalls;longestProbe;misses;runtime;"
                                   "staticDuplicates;staticEntries;stringBytes;stringCount;tableCapacity;tableLoad;tableUsed;writeLocks;");
        const PoolStringStats stats = pool.storage.GetStats();
        EXPECT_EQ(Number(summary, "stringCount"), 7);
        EXPECT_EQ(Number(summary, "tableUsed"), 7);
        EXPECT_EQ(Number(summary, "blockCount"), 3);
        EXPECT_EQ(Number(summary, "deadStrings"), 1);
        EXPECT_EQ(Number(summary, "deadBytes"), static_cast<int64_t>(PoolEntryBytes(StringView("pool_runtime").size())));
        EXPECT_EQ(Number(summary, "misses"), 1);
        EXPECT_EQ(Number(summary, "hits"), 0);
        EXPECT_EQ(Number(summary, "internCalls"), 1);
        EXPECT_EQ(Number(summary, "stringBytes"), static_cast<int64_t>(stats.stringBytes));
        EXPECT_EQ(Number(summary, "blockBytes"), static_cast<int64_t>(stats.blockBytes));
        EXPECT_EQ(Number(summary, "tableCapacity"), static_cast<int64_t>(stats.tableCapacity));
        EXPECT_EQ(Number(summary, "longestProbe"), static_cast<int64_t>(stats.longestProbe));
        EXPECT_EQ(Number(summary, "writeLocks"), static_cast<int64_t>(stats.writeLocks));
        EXPECT_DOUBLE_EQ(NumberOf(summary, "tableLoad"), 7.0 / static_cast<double>(stats.tableCapacity));

        const JsonView adopted = MemberOf(summary, "adopted");
        EXPECT_EQ(KeysOf(adopted), "blockBytes;blocks;bytes;strings;");
        EXPECT_EQ(Number(adopted, "strings"), 5);
        EXPECT_EQ(Number(adopted, "blocks"), 2);
        EXPECT_EQ(Number(adopted, "bytes"), static_cast<int64_t>(stats.adopted.bytes));
        EXPECT_EQ(Number(adopted, "blockBytes"), static_cast<int64_t>(stats.adopted.blockBytes));
        EXPECT_EQ(Number(MemberOf(summary, "runtime"), "strings"), 1);
        EXPECT_EQ(Number(MemberOf(summary, "runtime"), "blocks"), 1);
        EXPECT_EQ(Number(MemberOf(summary, "staticEntries"), "strings"), 1);
        EXPECT_EQ(Number(MemberOf(summary, "staticEntries"), "blocks"), 0);
    }

    TEST_F(PoolStringsControlTest, EveryBlockHasItsKindSourceAndFill) {
        const ControlReply reply = Call();
        const Vector<JsonView> blocks = ItemsOf(Section(reply, "summary"), "blocks");
        ASSERT_EQ(blocks.size(), 3u);

        EXPECT_EQ(KeysOf(blocks[0]), "capacity;deadBytes;deadStrings;kind;source;used;");
        EXPECT_EQ(StringOf(blocks[0], "kind"), "runtime");
        EXPECT_EQ(StringOf(blocks[0], "source"), "");
        EXPECT_EQ(Number(blocks[0], "capacity"), static_cast<int64_t>(PoolString::kBlockBytes));
        EXPECT_EQ(Number(blocks[0], "used"), static_cast<int64_t>(PoolEntryBytes(StringView("pool_runtime").size())));

        const auto cookedBytes = static_cast<int64_t>(PoolEntryBytes(StringView("pool_adopted").size()) + PoolEntryBytes(StringView("pool_runtime").size()));
        EXPECT_EQ(StringOf(blocks[1], "kind"), "adopted");
        EXPECT_EQ(StringOf(blocks[1], "source"), String(kCookedSource));
        EXPECT_EQ(Number(blocks[1], "capacity"), cookedBytes);
        EXPECT_EQ(Number(blocks[1], "used"), cookedBytes);
        EXPECT_EQ(Number(blocks[1], "deadStrings"), 1);
        EXPECT_EQ(Number(blocks[1], "deadBytes"), static_cast<int64_t>(PoolEntryBytes(StringView("pool_runtime").size())));
        EXPECT_EQ(StringOf(blocks[2], "source"), String(kForcedSource));
    }

    TEST_F(PoolStringsControlTest, ABlockGivenOutAndNotAdoptedIsAllocated) {
        const PoolBlock block = pool.storage.AllocateBlock(64);
        const ControlReply reply = Call();
        const Vector<JsonView> blocks = ItemsOf(Section(reply, "summary"), "blocks");
        ASSERT_EQ(blocks.size(), 4u);
        EXPECT_EQ(StringOf(blocks[3], "kind"), "allocated");
        EXPECT_EQ(Number(blocks[3], "used"), 0);
    }

    TEST_F(PoolStringsControlTest, TheChecksCountCollisionsAndTextsHeldTwice) {
        const ControlReply reply = Call();
        const JsonView checks = Section(reply, "checks");
        EXPECT_EQ(Number(checks, "hashCollisions"), 1);
        EXPECT_EQ(Number(checks, "duplicateTexts"), 1);
    }

    TEST_F(PoolStringsControlTest, ASearchRowSaysWhereItsStringCameFrom) {
        const ControlReply reply = Call(R"({"query":"ADOPT"})");
        const JsonView search = Section(reply, "search");
        EXPECT_EQ(Number(search, "matched"), 1);
        const Vector<JsonView> rows = ItemsOf(search, "rows");
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(KeysOf(rows[0]), "file;hash;length;source;text;");
        EXPECT_EQ(StringOf(rows[0], "text"), "pool_adopted");
        EXPECT_EQ(StringOf(rows[0], "source"), "adopted");
        EXPECT_EQ(StringOf(rows[0], "file"), String(kCookedSource));
        EXPECT_EQ(StringOf(rows[0], "hash"), Format("{:016x}", StringUtils::GetHash("pool_adopted")));
        EXPECT_EQ(Number(rows[0], "length"), static_cast<int64_t>(StringView("pool_adopted").size()));

        const ControlReply collision = Call(R"({"query":"collision_a"})");
        const Vector<JsonView> collided = ItemsOf(Section(collision, "search"), "rows");
        ASSERT_EQ(collided.size(), 1u);
        EXPECT_EQ(StringOf(collided[0], "hash"), "000000000000002a");
    }

    TEST_F(PoolStringsControlTest, TheSourceFilterAndTheLimitCutTheRowsNotTheMatchCount) {
        const ControlReply statics = Call(R"({"source":"static"})");
        EXPECT_EQ(FoundTexts(statics), "pool_static;");
        const Vector<JsonView> staticRows = ItemsOf(Section(statics, "search"), "rows");
        ASSERT_EQ(staticRows.size(), 1u);
        EXPECT_EQ(StringOf(staticRows[0], "file"), "");
        EXPECT_EQ(FoundTexts(Call(R"({"source":"runtime"})")), "pool_runtime;");
        EXPECT_EQ(FoundTexts(Call(R"({"source":"adopted","query":"twice"})")), "pool_twice;pool_twice;");

        const ControlReply cut = Call(R"({"limit":2})");
        EXPECT_EQ(FoundTexts(cut), "pool_adopted;pool_collision_a;");
        EXPECT_EQ(Number(Section(cut, "search"), "matched"), 7);

        const ControlReply none = Call(R"({"limit":0})");
        EXPECT_EQ(FoundTexts(none), "");
        EXPECT_EQ(Number(Section(none, "search"), "matched"), 7);
    }

    TEST_F(PoolStringsControlTest, TheDefaultsAreTwentyRowsAndSixtyFrames) {
        for (size_t i = 0; i < 30; ++i) {
            static_cast<void>(pool.storage.Intern(CounterName("pool_many_", i)));
        }
        for (uint64_t frame = 1; frame <= 70; ++frame) {
            monitor->BeginFrame(frame);
            monitor->EndFrame();
        }
        const ControlReply reply = Call();
        EXPECT_EQ(ItemsOf(Section(reply, "search"), "rows").size(), 20u);
        EXPECT_EQ(Number(Section(reply, "search"), "matched"), 37);
        const Vector<JsonView> frames = ItemsOf(Section(reply, "growth"), "frames");
        ASSERT_EQ(frames.size(), 60u);
        EXPECT_EQ(Number(frames.front(), "frame"), 11);
        EXPECT_EQ(Number(frames.back(), "frame"), 70);
    }

    TEST_F(PoolStringsControlTest, GrowthListsTheLastFramesAndSumsTheirRuntimeAdditions) {
        monitor->BeginFrame(5);
        static_cast<void>(pool.storage.Intern("pool_grown"));
        monitor->EndFrame();
        monitor->BeginFrame(6);
        Adopt(pool.storage, {Right("pool_later")}, kCookedSource);
        monitor->EndFrame();

        const ControlReply both = Call();
        const JsonView growth = Section(both, "growth");
        const auto grownBytes = static_cast<int64_t>(PoolEntryBytes(StringView("pool_grown").size()));
        EXPECT_EQ(KeysOf(growth), "bytes;frames;strings;");
        EXPECT_EQ(Number(growth, "strings"), 1);
        EXPECT_EQ(Number(growth, "bytes"), grownBytes);
        const Vector<JsonView> frames = ItemsOf(growth, "frames");
        ASSERT_EQ(frames.size(), 2u);
        EXPECT_EQ(KeysOf(frames[0]), "frame;otherStrings;runtimeBytes;runtimeStrings;");
        EXPECT_EQ(Number(frames[0], "frame"), 5);
        EXPECT_EQ(Number(frames[0], "runtimeStrings"), 1);
        EXPECT_EQ(Number(frames[0], "runtimeBytes"), grownBytes);
        EXPECT_EQ(Number(frames[1], "otherStrings"), 1);
        EXPECT_EQ(Number(frames[1], "runtimeStrings"), 0);

        const ControlReply last = Call(R"({"frames":1})");
        EXPECT_EQ(ItemsOf(Section(last, "growth"), "frames").size(), 1u);
        EXPECT_EQ(Number(Section(last, "growth"), "strings"), 0);
    }

    TEST_F(PoolStringsControlTest, RecordedStringsComeByZoneAndResetClearsThemAfterTheAnswer) {
        monitor->SetRecording(true);
        monitor->BeginFrame(9);
        const ProfileSite* previous = AllocTracker::ExchangeCurrentZone(&kZone);
        constexpr size_t kInZone = 10;
        for (size_t i = 0; i < kInZone; ++i) {
            static_cast<void>(pool.storage.Intern(CounterName("pool_zoned_", i)));
        }
        AllocTracker::ExchangeCurrentZone(nullptr);
        static_cast<void>(pool.storage.Intern("pool_loose"));
        AllocTracker::ExchangeCurrentZone(previous);
        monitor->EndFrame();

        const ControlReply reply = Call(R"({"reset":true})");
        const JsonView recording = Section(reply, "recording");
        EXPECT_EQ(KeysOf(recording), "dropped;enabled;zones;");
        EXPECT_TRUE(BoolOf(recording, "enabled").value_or(false));
        EXPECT_EQ(Number(recording, "dropped"), 0);
        const Vector<JsonView> zones = ItemsOf(recording, "zones");
        ASSERT_EQ(zones.size(), 2u);
        EXPECT_EQ(KeysOf(zones[0]), "bytes;file;lastFrame;line;strings;texts;zone;");
        EXPECT_EQ(StringOf(zones[0], "zone"), "pool strings zone");
        EXPECT_EQ(StringOf(zones[0], "file"), "tests/PoolStringsControlTest.cpp");
        EXPECT_EQ(Number(zones[0], "line"), 7);
        EXPECT_EQ(Number(zones[0], "lastFrame"), 9);
        EXPECT_EQ(Number(zones[0], "strings"), static_cast<int64_t>(kInZone));
        EXPECT_EQ(Number(zones[0], "bytes"), static_cast<int64_t>(kInZone * PoolEntryBytes(StringView("pool_zoned_0").size())));
        EXPECT_EQ(RawOf(zones[0], "texts"), R"(["pool_zoned_0","pool_zoned_1","pool_zoned_2","pool_zoned_3","pool_zoned_4","pool_zoned_5","pool_zoned_6","pool_zoned_7"])");
        EXPECT_EQ(StringOf(zones[1], "zone"), "(no zone)");
        EXPECT_EQ(StringOf(zones[1], "file"), "");
        EXPECT_EQ(Number(zones[1], "line"), 0);
        EXPECT_EQ(RawOf(zones[1], "texts"), R"(["pool_loose"])");

        const ControlReply after = Call();
        EXPECT_EQ(ItemsOf(Section(after, "recording"), "zones").size(), 0u);
        EXPECT_TRUE(BoolOf(Section(after, "recording"), "enabled").value_or(false)) << "reset drops the records, not the recording";
    }

    TEST_F(PoolStringsControlTest, RecordSwitchesTheRecordingBeforeTheAnswerWhereThereIsAnEditor) {
        const ControlReply on = Call(R"({"record":true})");
        if constexpr (!kEditorBuilt) {
            EXPECT_EQ(on.ErrorCode(), "not_available");
            EXPECT_EQ(Call(R"({"record":false})").ErrorCode(), "not_available");
            EXPECT_FALSE(monitor->IsRecording());
            return;
        }
        EXPECT_TRUE(BoolOf(Section(on, "recording"), "enabled").value_or(false));
        EXPECT_TRUE(monitor->IsRecording());
        static_cast<void>(pool.storage.Intern("pool_switched_on"));

        const ControlReply off = Call(R"({"record":false})");
        EXPECT_FALSE(BoolOf(Section(off, "recording"), "enabled").value_or(true));
        EXPECT_EQ(ItemsOf(Section(off, "recording"), "zones").size(), 1u);
        EXPECT_FALSE(monitor->IsRecording());
    }

    TEST_F(PoolStringsControlTest, AWrongParameterIsRefusedAndSwitchesNothing) {
        for (const StringView params : {R"({"frames":0})", R"({"frames":241})", R"({"frames":1.5})", R"({"frames":"1"})", R"({"limit":-1})", R"({"limit":1001})", R"({"limit":null})", R"({"query":3})",
                                        R"({"source":"Static"})", R"({"source":""})", R"({"source":1})", R"({"record":1})", R"({"reset":"yes"})", R"({"record":true,"reset":0})"}) {
            EXPECT_EQ(Call(params).ErrorCode(), "invalid_params") << String(params).c_str();
        }
        EXPECT_FALSE(monitor->IsRecording());
        EXPECT_TRUE(Call(R"({"frames":240,"limit":1000,"query":""})").IsOk());
    }

    TEST_F(PoolStringsControlTest, ACallInternsNothingOfWhatTheClientSends) {
        const String query = FreshText("pool_client_text");
        const ControlReply reply = Call(Format(R"({{"query":"{}"}})", query));
        ASSERT_TRUE(reply.IsOk());
        EXPECT_FALSE(PoolString::Find(StringView{query}).has_value());
        EXPECT_EQ(pool.storage.GetStats().stringCount, 7u);
    }

}  // namespace BECore::Tests
