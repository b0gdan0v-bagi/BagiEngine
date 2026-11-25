#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Widgets/PanelAssets/PanelAssetHolder.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String PanelText(StringView title, StringView elementId = "text") {
            return Format(R"(<PanelAsset id="Held" title="{}"><elements><element type="PanelText" id="{}" text="hello"/></elements></PanelAsset>)", title, elementId);
        }

        PanelLoadResult Good(StringView title) {
            PanelLoadResult result = LoadPanelAsset(StringView(PanelText(title)));
            EXPECT_TRUE(result.issues.empty());
            return result;
        }

        PanelLoadResult WithIssues(StringView title) {
            PanelLoadResult result = LoadPanelAsset(StringView(PanelText(title, "bad id")));
            EXPECT_FALSE(result.issues.empty());
            EXPECT_NE(result.asset, nullptr) << "an asset with issues is still held by the result";
            return result;
        }

        StringView TitleOf(const PanelAssetHolder& holder) {
            return holder.Current() != nullptr ? StringView(holder.Current()->title) : StringView();
        }

    }  // namespace

    TEST(PanelAssetHolderTest, AGoodReadIsCurrentAndBumpsTheVersion) {
        PanelAssetHolder holder;
        EXPECT_EQ(holder.Current(), nullptr);
        const uint32_t before = holder.Version();

        holder.ApplyResult(Good("one"));

        EXPECT_EQ(TitleOf(holder), "one");
        EXPECT_TRUE(holder.Issues().empty());
        EXPECT_NE(holder.Version(), before);
    }

    TEST(PanelAssetHolderTest, ABadReadKeepsTheLastGoodAssetAndTheVersion) {
        PanelAssetHolder holder;
        holder.ApplyResult(Good("one"));
        const IntrusivePtr<PanelAsset> good = holder.Current();
        const uint32_t version = holder.Version();

        holder.ApplyResult(WithIssues("two"));

        EXPECT_EQ(holder.Current(), good);
        EXPECT_EQ(TitleOf(holder), "one");
        EXPECT_FALSE(holder.Issues().empty());
        EXPECT_EQ(holder.Version(), version);
    }

    TEST(PanelAssetHolderTest, AFirstBadReadLeavesNoAsset) {
        PanelAssetHolder holder;

        holder.ApplyResult(WithIssues("one"));

        EXPECT_EQ(holder.Current(), nullptr);
        EXPECT_FALSE(holder.Issues().empty());
    }

    TEST(PanelAssetHolderTest, ATextThatDoesNotParseIsAnIssueWithNoAsset) {
        PanelAssetHolder holder;
        holder.ApplyResult(Good("one"));

        holder.ApplyResult(LoadPanelAsset(StringView("<PanelAsset")));

        EXPECT_EQ(TitleOf(holder), "one");
        EXPECT_FALSE(holder.Issues().empty());
    }

    TEST(PanelAssetHolderTest, AGoodReadAfterABadOneClearsTheIssuesAndReplacesTheAsset) {
        PanelAssetHolder holder;
        holder.ApplyResult(Good("one"));
        holder.ApplyResult(WithIssues("two"));
        const uint32_t version = holder.Version();

        holder.ApplyResult(Good("three"));

        EXPECT_EQ(TitleOf(holder), "three");
        EXPECT_TRUE(holder.Issues().empty());
        EXPECT_NE(holder.Version(), version);
    }

    TEST(PanelAssetHolderTest, AFailedReloadIsKeptBesideTheAssetUntilTheNextRead) {
        PanelAssetHolder holder;
        holder.ApplyResult(Good("one"));
        const uint32_t version = holder.Version();

        holder.ApplyReloadFailure("the file does not parse");

        EXPECT_EQ(holder.Failure(), "the file does not parse");
        EXPECT_EQ(TitleOf(holder), "one");
        EXPECT_EQ(holder.Version(), version);

        holder.ApplyResult(Good("two"));
        EXPECT_TRUE(holder.Failure().empty());
    }

    /// A manager over a `config` folder of its own: the shipped configs are never read or written
    class PanelAssetHolderConfigTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("panel_holder");
            MakeEmptyDirectory(_dir);
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
        }

        void TearDown() override {
            RemoveDirectory(_dir);
        }

        /// Writes the config file and (re)loads the manager's documents; each test owns the name it writes
        void Write(StringView name, StringView text) {
            WriteFile(_dir / Format("{}.xml", name), text);
            _manager.Initialize();
        }

        Path FileOf(StringView name) const {
            return _dir / Format("{}.xml", name);
        }

        Path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        PanelAssetHolder _holder;
    };

    TEST_F(PanelAssetHolderConfigTest, ReadsTheNamedConfig) {
        Write("HolderGood", PanelText("from file"));

        _holder.Load(_manager, "HolderGood", {});

        EXPECT_TRUE(_holder.Issues().empty());
        EXPECT_EQ(TitleOf(_holder), "from file");
    }

    TEST_F(PanelAssetHolderConfigTest, AnUnknownConfigIsAnIssue) {
        Write("HolderKnown", PanelText("x"));
        _holder.Load(_manager, "HolderKnown", {});

        _holder.Load(_manager, "HolderNoSuchConfig", {});

        ASSERT_EQ(_holder.Issues().size(), 1u);
        EXPECT_NE(_holder.Issues()[0].message.find("config not found"), String::npos);
        EXPECT_EQ(TitleOf(_holder), "x") << "the asset of an earlier read stays";
    }

    TEST_F(PanelAssetHolderConfigTest, ABrokenFileThenARestoredOne) {
        Write("HolderEdit", PanelText("one"));
        _holder.Load(_manager, "HolderEdit", {});
        ASSERT_EQ(TitleOf(_holder), "one");

        WriteFile(FileOf("HolderEdit"), PanelText("two", "bad id"));
        ASSERT_TRUE(_manager.ReloadConfig("HolderEdit"_intern));
        _holder.Load(_manager, "HolderEdit", {});
        EXPECT_FALSE(_holder.Issues().empty());
        EXPECT_EQ(TitleOf(_holder), "one");

        WriteFile(FileOf("HolderEdit"), PanelText("three"));
        ASSERT_TRUE(_manager.ReloadConfig("HolderEdit"_intern));
        _holder.Load(_manager, "HolderEdit", {});
        EXPECT_TRUE(_holder.Issues().empty());
        EXPECT_EQ(TitleOf(_holder), "three");
    }

    TEST_F(PanelAssetHolderConfigTest, AFileThatDoesNotParseIsAFailureAndTheOldDocumentStaysReadable) {
        Write("HolderBroken", PanelText("one"));
        _holder.Load(_manager, "HolderBroken", {});

        WriteFile(FileOf("HolderBroken"), "<PanelAsset");
        EXPECT_FALSE(_manager.ReloadConfig("HolderBroken"_intern));
        _holder.ApplyReloadFailure("does not parse");
        EXPECT_EQ(TitleOf(_holder), "one");
        EXPECT_EQ(_holder.Failure(), "does not parse");

        _holder.Load(_manager, "HolderBroken", {});
        EXPECT_EQ(TitleOf(_holder), "one");
        EXPECT_TRUE(_holder.Failure().empty());
    }

    TEST_F(PanelAssetHolderConfigTest, AFileThatIsGoneKeepsTheAsset) {
        Write("HolderGone", PanelText("one"));
        _holder.Load(_manager, "HolderGone", {});

        ASSERT_TRUE(RemoveFile(FileOf("HolderGone")));
        EXPECT_FALSE(_manager.ReloadConfig("HolderGone"_intern));
        _holder.ApplyReloadFailure("the file is gone");

        EXPECT_EQ(TitleOf(_holder), "one");
        EXPECT_EQ(_holder.Failure(), "the file is gone");
    }

}  // namespace BECore::Tests
