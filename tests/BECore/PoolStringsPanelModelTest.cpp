#include <BECore/PoolString/PoolBlock.h>
#include <BECore/PoolString/PoolStringMonitor.h>
#include <BECore/PoolStringTestSupport.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <BECore/RefCounted/New.h>
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

    TEST(PoolStringsPanelTextTest, ALineEndsAtTheFirstBreakAndIsNeverCutInsideACharacter) {
        EXPECT_EQ(PoolStringLine("short", 80), "short");
        EXPECT_EQ(PoolStringLine("first\nsecond", 80), "first");
        EXPECT_EQ(PoolStringLine("first\r\nsecond", 80), "first");
        EXPECT_EQ(PoolStringLine("abcdef", 3), "abc");
        // Two bytes each: a limit inside the second character leaves the first
        EXPECT_EQ(PoolStringLine("\xC3\xA9\xC3\xA9", 3), "\xC3\xA9");
        EXPECT_EQ(PoolStringLine("\xC3\xA9\xC3\xA9", 4), "\xC3\xA9\xC3\xA9");
        EXPECT_EQ(PoolStringLine("\xC3\xA9", 1), "");
    }

}  // namespace BECore::Tests
