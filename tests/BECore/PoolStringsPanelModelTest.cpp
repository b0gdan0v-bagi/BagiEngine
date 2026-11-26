#include <BECore/PoolString/PoolBlock.h>
#include <BECore/PoolString/PoolStringMonitor.h>
#include <BECore/PoolStringTestSupport.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/PoolStrings/PoolStringsPanelModel.h>
#include <Widgets/PoolStrings/PoolStringsText.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kContentSource = "config/Content.bin";
        constexpr StringView kBrokenSource = "checks/Broken.bin";
        constexpr uint64_t kSharedHash = 42;
        constexpr std::chrono::seconds kThreadLimit{5};
        constexpr ProfileSite kZone{"pool panel zone", "Load", "tests/PoolStringsPanelModelTest.cpp", 7, 0};

        String ZoneLabel(StringView name, std::initializer_list<StringView> texts, uint64_t lastFrame) {
            size_t bytes = 0;
            for (const StringView text : texts) {
                bytes += Details::PoolEntryBytes(text.size());
            }
            return Format("{} ({} strings, {}, last frame {})", name, texts.size(), FormatBytes(bytes), lastFrame);
        }

        /// The strings of a listed zone with their frames, `text@frame;` each
        String ZoneTexts(const PoolStringsPanelModel& model, const PoolPanelZone& zone) {
            String texts;
            for (size_t i = zone.group.first; i < zone.group.first + zone.group.count; ++i) {
                const PoolStringRecord& record = model.GetRecords()[i];
                texts += Format("{}@{};", record.text.ToStringView(), record.frame);
            }
            return texts;
        }

        struct Hashed {
            StringView text;
            uint64_t hash;
        };

        Hashed Right(StringView text) {
            return {text, StringUtils::GetHash(text)};
        }

        /// An adopted block of the entries; with `verifyHashes` off it holds what `Intern` never would
        void Adopt(Details::PoolStringStorage& storage, const Vector<Hashed>& entries, StringView source, bool verifyHashes) {
            size_t bytes = 0;
            for (const Hashed& entry : entries) {
                bytes += Details::PoolEntryBytes(entry.text.size());
            }
            PoolBlock block = storage.AllocateBlock(bytes);
            size_t offset = 0;
            for (const Hashed& entry : entries) {
                Details::ConstructPoolEntry(block.Data() + offset, entry.hash, entry.text);
                offset += Details::PoolEntryBytes(entry.text.size());
            }
            PoolAdoptOptions options;
            options.verifyHashes = verifyHashes;
            options.sourceName = source;
            ASSERT_TRUE(storage.AdoptBlock(block, bytes, {}, options).has_value());
        }

        const PoolPanelSourceRow* RowOf(const PoolStringsPanelView& view, StringView source) {
            for (const PoolPanelSourceRow& row : view.sources) {
                if (row.source == source) {
                    return &row;
                }
            }
            return nullptr;
        }

        /// The listed strings of the contents, `text;` each
        String Listed(const PoolStringsPanelModel& model) {
            String texts;
            for (const uint32_t row : model.GetMatches()) {
                texts += Format("{};", model.GetSnapshot().strings[row].text.ToStringView());
            }
            return texts;
        }

        /// `kind|text|first source|second source;` of every listed pair
        String Pairs(const PoolStringsPanelView& view) {
            String pairs;
            for (const PoolPanelCheckRow& row : view.checks) {
                pairs += Format("{}|{}|{}|{};", row.kind, row.text, row.firstSource, row.secondSource);
            }
            return pairs;
        }

    }  // namespace

    /// The Pool Strings model over a pool of its own and no ImGui
    class PoolStringsPanelModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _model = New<PoolStringsPanelModel>();
            _model->SetMonitor(&_monitor);
        }

        void Open() {
            _model->OnOpenChanged(true);
            _model->Update();
        }

        void Refresh() {
            ASSERT_TRUE(_model->RunCommand("poolStrings.refresh", {}));
        }

        const PoolStringsPanelView& View() const {
            return _model->GetView();
        }

        void Intern(StringView text) {
            static_cast<void>(_pool.storage.Intern(text));
        }

        void Updates(uint32_t count) {
            for (uint32_t i = 0; i < count; ++i) {
                _model->Update();
            }
        }

        /// What a click on the switch writes, read by the next frame of the model
        void SwitchRecording(bool recording) {
            const_cast<PoolStringsPanelView&>(_model->GetView()).recording = recording;
            _model->Update();
        }

        /// What a control writes, read by the next frame of the model
        void Filter(StringView search, int32_t sourceIndex) {
            PoolStringsPanelView& view = const_cast<PoolStringsPanelView&>(_model->GetView());
            view.search = String(search);
            view.sourceIndex = sourceIndex;
            _model->Update();
        }

        /// One runtime string, one static one and two adopted files
        void FillContents() {
            PoolString constant;
            const PoolStaticRegistration registration{Details::ConstructPoolEntry(_readyEntry, StringUtils::GetHash("Door"), "Door"), &constant};
            _pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&registration, 1));
            static_cast<void>(_pool.storage.Intern("Player_Health"));
            Adopt(_pool.storage, {Right("player_name_long_one")}, "config/B.bin", true);
            Adopt(_pool.storage, {Right("speed")}, "config/C.bin", true);
        }

        /// One static string, two runtime ones and an adopted block with one new string and one dead entry
        void FillAllSources() {
            PoolString constant;
            const PoolStaticRegistration registration{Details::ConstructPoolEntry(_readyEntry, StringUtils::GetHash("panel_static"), "panel_static"), &constant};
            _pool.storage.RegisterStatic(Span<const PoolStaticRegistration>(&registration, 1));
            static_cast<void>(_pool.storage.Intern("panel_runtime"));
            static_cast<void>(_pool.storage.Intern("panel_other"));
            Adopt(_pool.storage, {Right("panel_adopted"), Right("panel_runtime")}, kContentSource, true);
        }

        // The ready entry and the pool outlive the monitor, the monitor outlives the model
        alignas(Details::kPoolEntryAlignment) char _readyEntry[64]{};
        IsolatedPool _pool;
        PoolStringMonitor _monitor{_pool.storage};
        IntrusivePtr<PoolStringsPanelModel> _model;
    };

    TEST_F(PoolStringsPanelModelTest, TheSnapshotIsTakenByTheFirstUpdateAfterThePanelOpens) {
        _model->Update();
        EXPECT_TRUE(View().sources.empty()) << "a closed panel takes none";

        _model->OnOpenChanged(true);
        EXPECT_TRUE(View().sources.empty());
        _model->Update();
        EXPECT_EQ(View().sources.size(), 4u);
    }

    TEST_F(PoolStringsPanelModelTest, TheSummaryShowsEachSourceTheTotalAndTheCounters) {
        ASSERT_NO_FATAL_FAILURE(FillAllSources());
        Open();

        const PoolStringStats stats = _pool.storage.GetStats();
        const PoolPanelSourceRow* constant = RowOf(View(), "static");
        const PoolPanelSourceRow* runtime = RowOf(View(), "runtime");
        const PoolPanelSourceRow* adopted = RowOf(View(), "adopted");
        const PoolPanelSourceRow* total = RowOf(View(), "total");
        ASSERT_TRUE(constant && runtime && adopted && total);
        EXPECT_EQ(constant->strings, "1");
        EXPECT_EQ(constant->blocks, "0");
        EXPECT_EQ(runtime->strings, "2");
        EXPECT_EQ(runtime->bytes, FormatBytes(stats.runtime.bytes));
        EXPECT_EQ(runtime->blockBytes, FormatBytes(stats.runtime.blockBytes));
        EXPECT_EQ(adopted->strings, "1");
        EXPECT_EQ(adopted->blocks, "1");
        EXPECT_EQ(total->strings, "4");
        EXPECT_EQ(total->bytes, FormatBytes(stats.stringBytes));
        EXPECT_EQ(total->blocks, Format("{}", stats.blockCount));

        EXPECT_EQ(View().deadText, Format("Dead entries of adopted blocks: 1 ({})", FormatBytes(stats.deadBytes)));
        EXPECT_TRUE(StringView(View().tableText).starts_with(Format("Table: 4 of {} slots used", stats.tableCapacity))) << View().tableText.c_str();
        EXPECT_EQ(View().internText, "Intern calls: 2 (0 hits, 2 misses)");
        EXPECT_EQ(View().batchText, "InternBatch calls: 0, strings given: 0");
        EXPECT_EQ(View().countersText, Format("Static duplicates: 0, write locks: {}, heap fallback blocks: 0", stats.writeLocks));
    }

    TEST_F(PoolStringsPanelModelTest, EveryBlockIsListedWithItsFileFillAndDeadEntries) {
        ASSERT_NO_FATAL_FAILURE(FillAllSources());
        Open();

        PoolStringSnapshot snapshot;
        _pool.storage.TakeSnapshot(snapshot);
        const Vector<PoolPanelBlock>& blocks = _model->GetBlocks();
        ASSERT_EQ(blocks.size(), snapshot.blocks.size());
        ASSERT_EQ(blocks.size(), 2u);

        EXPECT_EQ(blocks[0].kind, "runtime");
        EXPECT_TRUE(blocks[0].file.empty());
        EXPECT_FLOAT_EQ(blocks[0].fill, static_cast<float>(snapshot.blocks[0].used) / static_cast<float>(snapshot.blocks[0].capacity));
        EXPECT_EQ(blocks[0].usage, Format("{} / {}", FormatBytes(snapshot.blocks[0].used), FormatBytes(snapshot.blocks[0].capacity)));
        EXPECT_EQ(blocks[0].deadStrings, "0");

        EXPECT_EQ(blocks[1].kind, "adopted");
        EXPECT_EQ(blocks[1].file, "Content.bin");
        EXPECT_EQ(blocks[1].source, kContentSource);
        EXPECT_FLOAT_EQ(blocks[1].fill, 1.0f);
        EXPECT_EQ(blocks[1].deadStrings, "1");
        EXPECT_EQ(blocks[1].deadBytes, FormatBytes(snapshot.blocks[1].deadBytes));
    }

    TEST_F(PoolStringsPanelModelTest, TheChecksListEachPairWithBothSources) {
        static_cast<void>(_pool.storage.Intern("panel_runtime"));
        ASSERT_NO_FATAL_FAILURE(Adopt(_pool.storage, {{"impostor", StringUtils::GetHash("panel_runtime")}}, kBrokenSource, false));
        ASSERT_NO_FATAL_FAILURE(Adopt(_pool.storage, {{"twice", 1}, {"twice", 2}}, {}, false));
        Open();

        EXPECT_TRUE(View().hasFindings);
        EXPECT_EQ(View().checksText, "Hash collisions: 1, texts held twice: 1");
        EXPECT_EQ(Pairs(View()), "collision|impostor / panel_runtime|Broken.bin|runtime;duplicate|twice|adopted|adopted;");
        EXPECT_FALSE(View().checksCut);
    }

    TEST_F(PoolStringsPanelModelTest, APoolWithNothingWrongSaysNone) {
        ASSERT_NO_FATAL_FAILURE(FillAllSources());
        Open();

        EXPECT_FALSE(View().hasFindings);
        EXPECT_TRUE(StringView(View().checksText).ends_with("none")) << View().checksText.c_str();
        EXPECT_TRUE(View().checks.empty());
        EXPECT_FALSE(View().checksCut);
    }

    TEST_F(PoolStringsPanelModelTest, PairsPastTheCapAreCountedAndNotListed) {
        constexpr size_t kExtra = 3;
        Vector<String> texts;
        for (size_t i = 0; i <= PoolStringsPanelLimits::kMaxCheckRows + kExtra; ++i) {
            texts.push_back(CounterName("collide", i));
        }
        Vector<Hashed> entries;
        for (const String& text : texts) {
            entries.push_back({text, kSharedHash});
        }
        ASSERT_NO_FATAL_FAILURE(Adopt(_pool.storage, entries, kBrokenSource, false));
        Open();

        EXPECT_EQ(View().checks.size(), PoolStringsPanelLimits::kMaxCheckRows);
        EXPECT_TRUE(View().checksCut);
        EXPECT_EQ(View().checksCutText, Format("{} more pairs are not listed", kExtra));
    }

    TEST_F(PoolStringsPanelModelTest, ANewStringShowsOnlyAfterRefreshOrAReopen) {
        static_cast<void>(_pool.storage.Intern("first"));
        Open();
        ASSERT_NE(RowOf(View(), "runtime"), nullptr);

        static_cast<void>(_pool.storage.Intern("second"));
        _model->Update();
        _model->Update();
        EXPECT_EQ(RowOf(View(), "runtime")->strings, "1") << "no frame takes a snapshot by itself";

        ASSERT_NO_FATAL_FAILURE(Refresh());
        EXPECT_EQ(RowOf(View(), "runtime")->strings, "2");

        static_cast<void>(_pool.storage.Intern("third"));
        _model->OnOpenChanged(false);
        _model->OnOpenChanged(true);
        _model->Update();
        EXPECT_EQ(RowOf(View(), "runtime")->strings, "3");
    }

    TEST_F(PoolStringsPanelModelTest, AHeldSnapshotLeavesThePoolFreeToOtherThreadsAndShowingItAddsNothing) {
        ASSERT_NO_FATAL_FAILURE(FillAllSources());
        const PoolStringStats before = _pool.storage.GetStats();
        Open();
        ASSERT_NO_FATAL_FAILURE(Refresh());
        EXPECT_EQ(_pool.storage.GetStats().internCalls, before.internCalls);
        EXPECT_EQ(_pool.storage.GetStats().stringCount, before.stringCount);

        EXPECT_TRUE(RunThreadsTogether(1, kThreadLimit, [this](int) { static_cast<void>(_pool.storage.Intern("from_another_thread")); }));
        EXPECT_EQ(_pool.storage.GetStats().stringCount, before.stringCount + 1);
    }

    TEST_F(PoolStringsPanelModelTest, ACommandOfAnotherPanelIsNotTaken) {
        EXPECT_FALSE(_model->RunCommand("view.poolStrings", {}));
        EXPECT_TRUE(View().sources.empty());
    }

    TEST_F(PoolStringsPanelModelTest, TheContentsListEveryStringAndTheSearchIgnoresTheAsciiCase) {
        ASSERT_NO_FATAL_FAILURE(FillContents());
        Open();
        EXPECT_EQ(Listed(*_model), "Door;Player_Health;player_name_long_one;speed;");
        EXPECT_EQ(View().matchText, "4 of 4 strings");

        Filter("pLaYeR", 0);
        EXPECT_EQ(Listed(*_model), "Player_Health;player_name_long_one;");
        EXPECT_EQ(View().matchText, "2 of 4 strings");

        Filter("nothing like this", 0);
        EXPECT_TRUE(_model->GetMatches().empty());
        EXPECT_EQ(View().matchText, "0 of 4 strings");
    }

    TEST_F(PoolStringsPanelModelTest, EachSourceChoiceListsItsStringsAndAnIndexOutsideTheListIsAll) {
        ASSERT_NO_FATAL_FAILURE(FillContents());
        Open();
        EXPECT_EQ(View().sourceItems, (Vector<String>{"All", "Static", "Runtime", "Adopted", "config/B.bin", "config/C.bin"}));

        Filter("", 1);
        EXPECT_EQ(Listed(*_model), "Door;");
        Filter("", 2);
        EXPECT_EQ(Listed(*_model), "Player_Health;");
        Filter("", 3);
        EXPECT_EQ(Listed(*_model), "player_name_long_one;speed;");
        Filter("", 5);
        EXPECT_EQ(Listed(*_model), "speed;");
        Filter("player", 4);
        EXPECT_EQ(Listed(*_model), "player_name_long_one;");

        for (const int32_t outside : {6, -1}) {
            Filter("", outside);
            EXPECT_EQ(View().sourceIndex, 0) << outside;
            EXPECT_EQ(_model->GetMatches().size(), 4u) << outside;
        }
    }

    TEST_F(PoolStringsPanelModelTest, TheSourceOfAStringIsItsOriginOrTheFileOfItsBlock) {
        ASSERT_NO_FATAL_FAILURE(FillContents());
        ASSERT_NO_FATAL_FAILURE(Adopt(_pool.storage, {Right("unnamed")}, {}, true));
        Open();

        String sources;
        for (const uint32_t row : _model->GetMatches()) {
            sources += Format("{};", _model->SourceOf(_model->GetSnapshot().strings[row]));
        }
        EXPECT_EQ(sources, "static;runtime;B.bin;C.bin;adopted;");
    }

    TEST_F(PoolStringsPanelModelTest, EachSortKeyOrdersTheListInBothDirections) {
        ASSERT_NO_FATAL_FAILURE(FillContents());
        Open();

        _model->SetSort(PoolStringSortKey::Text, true);
        EXPECT_EQ(Listed(*_model), "speed;player_name_long_one;Player_Health;Door;");
        _model->SetSort(PoolStringSortKey::Length, false);
        EXPECT_EQ(Listed(*_model), "Door;speed;Player_Health;player_name_long_one;");
        _model->SetSort(PoolStringSortKey::Length, true);
        EXPECT_EQ(Listed(*_model), "player_name_long_one;Player_Health;speed;Door;");
        _model->SetSort(PoolStringSortKey::Source, false);
        EXPECT_EQ(Listed(*_model), "Door;Player_Health;player_name_long_one;speed;");
        _model->SetSort(PoolStringSortKey::Source, true);
        EXPECT_EQ(Listed(*_model), "speed;player_name_long_one;Player_Health;Door;");

        for (const bool descending : {false, true}) {
            _model->SetSort(PoolStringSortKey::Hash, descending);
            const Vector<uint32_t>& matches = _model->GetMatches();
            ASSERT_EQ(matches.size(), 4u);
            for (size_t i = 1; i < matches.size(); ++i) {
                const uint64_t before = _model->GetSnapshot().strings[matches[i - 1]].text.HashValue();
                const uint64_t after = _model->GetSnapshot().strings[matches[i]].text.HashValue();
                EXPECT_EQ(before > after, descending) << i;
            }
        }
    }

    TEST_F(PoolStringsPanelModelTest, AChosenFileStaysChosenByNameOverARefresh) {
        ASSERT_NO_FATAL_FAILURE(FillContents());
        Open();
        Filter("", 4);

        // A file that sorts before the chosen one moves its index
        ASSERT_NO_FATAL_FAILURE(Adopt(_pool.storage, {Right("first_file")}, "config/A.bin", true));
        ASSERT_NO_FATAL_FAILURE(Refresh());
        EXPECT_EQ(View().sourceIndex, 5);
        EXPECT_EQ(Listed(*_model), "player_name_long_one;");

        IsolatedPool other;
        PoolStringMonitor monitor(other.storage);
        static_cast<void>(other.storage.Intern("elsewhere"));
        _model->SetMonitor(&monitor);
        ASSERT_NO_FATAL_FAILURE(Refresh());
        EXPECT_EQ(View().sourceIndex, 0) << "no such file here";
        EXPECT_EQ(Listed(*_model), "elsewhere;");
        _model = {};  // before the monitor it watches
    }

    TEST_F(PoolStringsPanelModelTest, TheListIsSearchedAgainOnlyWhenAChoiceTheSortOrTheSnapshotChanged) {
        ASSERT_NO_FATAL_FAILURE(FillContents());
        Open();
        const uint64_t opened = _model->GetSearchCount();

        _model->Update();
        _model->Update();
        _model->SetSort(PoolStringSortKey::Text, false);
        EXPECT_EQ(_model->GetSearchCount(), opened);

        Filter("a", 0);
        _model->Update();
        EXPECT_EQ(_model->GetSearchCount(), opened + 1);
        Filter("a", 2);
        EXPECT_EQ(_model->GetSearchCount(), opened + 2);
        _model->SetSort(PoolStringSortKey::Text, true);
        EXPECT_EQ(_model->GetSearchCount(), opened + 3);
        ASSERT_NO_FATAL_FAILURE(Refresh());
        EXPECT_EQ(_model->GetSearchCount(), opened + 4);
        _model->Update();
        EXPECT_EQ(_model->GetSearchCount(), opened + 4);
    }

    TEST_F(PoolStringsPanelModelTest, TheGrowthShowsTheFramesThatAddedRuntimeStringsAndTheCleanOnesAfterThem) {
        _monitor.BeginFrame(1);
        _monitor.EndFrame();
        Open();
        EXPECT_FALSE(View().hasGrowth);
        EXPECT_EQ(View().growthText, "No runtime string was added in the frames kept");

        _monitor.BeginFrame(2);
        Intern("grown_one");
        Intern("grown_two");
        _monitor.EndFrame();
        _model->Update();
        const uint64_t bytes = Details::PoolEntryBytes(StringView("grown_one").size()) + Details::PoolEntryBytes(StringView("grown_two").size());
        const String grew = Format("Frames with new runtime strings: 1 (2 strings, {})", FormatBytes(bytes));
        EXPECT_TRUE(View().hasGrowth);
        EXPECT_EQ(View().growthText, grew);
        ASSERT_EQ(_model->GetGrowth().size(), 2u);
        EXPECT_EQ(_model->GetGrowth()[1].runtimeStrings, 2u);

        _monitor.BeginFrame(3);
        _monitor.EndFrame();
        _model->Update();
        ASSERT_EQ(_model->GetGrowth().size(), 3u);
        EXPECT_EQ(_model->GetGrowth()[2].runtimeStrings, 0u);
        EXPECT_EQ(View().growthText, grew) << "the frame is still kept";

        for (uint64_t frame = 4; frame < 4 + PoolStringMonitor::kFrameHistory; ++frame) {
            _monitor.BeginFrame(frame);
            _monitor.EndFrame();
        }
        _model->Update();
        EXPECT_FALSE(View().hasGrowth) << "the frame with the strings is no longer kept";
    }

    TEST_F(PoolStringsPanelModelTest, TheSwitchStartsAndStopsTheRecordingAndFollowsASwitchMadeElsewhere) {
        Open();
        SwitchRecording(true);
        EXPECT_TRUE(_monitor.IsRecording());
        SwitchRecording(false);
        EXPECT_FALSE(_monitor.IsRecording());

        _monitor.SetRecording(true);
        _model->Update();
        EXPECT_TRUE(View().recording);
        EXPECT_TRUE(_monitor.IsRecording());
        _monitor.SetRecording(false);
        _model->Update();
        EXPECT_FALSE(View().recording);
        EXPECT_FALSE(_monitor.IsRecording());
    }

    TEST_F(PoolStringsPanelModelTest, ClosingThePanelAndDestroyingTheModelLeaveTheRecordingOn) {
        Open();
        SwitchRecording(true);
        _model->OnOpenChanged(false);
        EXPECT_TRUE(_monitor.IsRecording());
        _model.Reset();
        EXPECT_TRUE(_monitor.IsRecording());
    }

    TEST_F(PoolStringsPanelModelTest, TheRecordsAreListedByZoneWithTheirFramesAndTheCounts) {
        _monitor.SetRecording(true);
        _monitor.BeginFrame(5);
        {
            const CurrentZone zone(&kZone);
            Intern("zoned_first_of_the_longer_strings");
            Intern("zoned_second_of_the_longer_strings");
        }
        {
            const CurrentZone none(nullptr);
            Intern("loose");
        }
        _monitor.EndFrame();
        Open();

        EXPECT_EQ(View().recordsText, "Recorded: 3, dropped past the limit: 0");
        ASSERT_EQ(_model->GetZones().size(), 2u);
        const PoolPanelZone& zoned = _model->GetZones()[0];
        EXPECT_EQ(zoned.label, ZoneLabel("pool panel zone", {"zoned_first_of_the_longer_strings", "zoned_second_of_the_longer_strings"}, 5));
        EXPECT_EQ(zoned.location, "tests/PoolStringsPanelModelTest.cpp:7");
        EXPECT_EQ(ZoneTexts(*_model, zoned), "zoned_first_of_the_longer_strings@5;zoned_second_of_the_longer_strings@5;");
        const PoolPanelZone& loose = _model->GetZones()[1];
        EXPECT_EQ(loose.label, ZoneLabel("(no zone)", {"loose"}, 5));
        EXPECT_TRUE(loose.location.empty());
        EXPECT_EQ(ZoneTexts(*_model, loose), "loose@5;");
    }

    TEST_F(PoolStringsPanelModelTest, StringsThatKeepComingAreGroupedAgainOnlyAfterSomeFramesAndByARefreshAtOnce) {
        _monitor.SetRecording(true);
        Open();
        Intern("regroup_first");
        _model->Update();
        EXPECT_TRUE(_model->GetRecords().empty()) << "not in the frame after a grouping";
        EXPECT_EQ(View().recordsText, "Recorded: 1, dropped past the limit: 0") << "the count does not wait";
        Updates(PoolStringsPanelLimits::kRegroupFrames);
        EXPECT_EQ(_model->GetRecords().size(), 1u);

        Refresh();
        Intern("regroup_second");
        _model->Update();
        EXPECT_EQ(_model->GetRecords().size(), 1u);
        Refresh();
        EXPECT_EQ(_model->GetRecords().size(), 2u);
    }

    TEST_F(PoolStringsPanelModelTest, AClearEmptiesTheListAtOnceWhoeverMadeIt) {
        _monitor.SetRecording(true);
        Intern("cleared_by_the_command");
        Open();
        ASSERT_EQ(_model->GetZones().size(), 1u);

        ASSERT_TRUE(_model->RunCommand("poolStrings.clearRecords", {}));
        EXPECT_TRUE(_model->GetZones().empty());
        EXPECT_TRUE(_monitor.IsRecording());

        Intern("cleared_by_the_monitor");
        Refresh();
        ASSERT_EQ(_model->GetZones().size(), 1u);
        _monitor.ClearRecords();
        Intern("came_after_the_clear");  // as many records as before
        _model->Update();
        ASSERT_EQ(_model->GetRecords().size(), 1u);
        EXPECT_EQ(_model->GetRecords()[0].text.ToStringView(), "came_after_the_clear");
    }

    TEST(PoolStringsPanelTextTest, ALineEndsAtTheFirstBreakAndIsNeverCutInsideACharacter) {
        EXPECT_EQ(PoolStringLine("short", 80), "short");
        EXPECT_EQ(PoolStringLine("first\nsecond", 80), "first");
        EXPECT_EQ(PoolStringLine("first\r\nsecond", 80), "first");
        EXPECT_EQ(PoolStringLine("abcdef", 3), "abc");
        // Two bytes each: a limit inside the second character leaves the first
        EXPECT_EQ(PoolStringLine("\xC3\xA9\xC3\xA9", 3), "\xC3\xA9");
        EXPECT_EQ(PoolStringLine("\xC3\xA9\xC3\xA9", 4), "\xC3\xA9\xC3\xA9");
        EXPECT_EQ(PoolStringLine("\xC3\xA9", 1), "");
        EXPECT_EQ(PoolStringPrefix("first\nsecond", 8), "first\nse");
    }

}  // namespace BECore::Tests
