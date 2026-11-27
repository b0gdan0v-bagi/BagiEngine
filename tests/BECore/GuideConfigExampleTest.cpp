#include <BECore/Config/ConfigManager.h>
#include <BECore/GuideConfigExample.h>
#include <Common/ConfigReloadSupport.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TestFiles.h>
#include <Events/ConfigEvents.h>
#include <Generated/GuideConfigExample.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using namespace GuideConfigExample;

        constexpr StringView kFileName = "GuideSpawnerConfig.xml";
        constexpr float kExampleInterval = 0.5f;
        constexpr int32_t kExampleCount = 8;
        constexpr int32_t kChangedCount = 12;

        /// The file the page shows, read from the source tree: a game's content is not mounted in the test executable
        Path ExampleFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "docs" / "guide" / "examples" / "config" / "config" / kFileName;
        }

    }  // namespace

    /// A manager of its own over a `config` folder of a temporary directory that holds the example file; the engine's own `config/` is never written
    class GuideConfigExampleTest : public ConfigReloadFixture {
    protected:
        const char* TempPrefix() const override {
            return "guide_cfg";
        }

        void WriteConfigFiles() override {
            WriteFile(ConfigPath(), ReadFile(ExampleFile()));
        }

        void PostConfigAndSettle() {
            ConfigReloadFixture::PostAndSettle(ConfigPath());
        }

        Path ConfigPath() const {
            return _dir / kFileName;
        }

        /// The example file with the first @p from replaced by @p to
        void WriteExampleWith(StringView from, StringView to) {
            String text = ReadFile(ExampleFile());
            const size_t at = text.find(from.data(), 0, from.size());
            ASSERT_NE(at, String::npos) << from.data();
            text.replace(at, from.size(), to.data(), to.size());
            WriteFile(ConfigPath(), text);
        }
    };

    TEST_F(GuideConfigExampleTest, TheFileIsReadByNameIntoTheFields) {
        const LogRecorder log;

        const auto read = ReadGuideSpawnerConfig(_manager);
        const GuideSpawnerConfig& config = Require(read);
        LogEvent::Flush();

        EXPECT_FLOAT_EQ(config._interval, kExampleInterval);
        EXPECT_EQ(config._count, kExampleCount);
        EXPECT_TRUE(config._enabled);
        EXPECT_EQ(config._shape, SpawnShape::Line);
        EXPECT_EQ(config._label, "first wave");
        EXPECT_EQ(log.Count(LogLevel::Warning, {"GuideSpawnerConfig"}), 0u) << "the example is a clean file";
    }

    TEST_F(GuideConfigExampleTest, ATypoAndAValueOutOfRangeAreWarningsWithTheConfigTheKeyAndTheMessage) {
        ASSERT_NO_FATAL_FAILURE(WriteExampleWith("interval=\"0.5\" count=\"8\"", "inteval=\"0.5\" count=\"800\""));
        ASSERT_TRUE(_manager.ReloadConfig(GuideSpawnerNames::config));
        const LogRecorder log;

        const auto read = ReadGuideSpawnerConfig(_manager);
        const GuideSpawnerConfig& config = Require(read);
        LogEvent::Flush();

        const auto typo = log.Messages(LogLevel::Warning, {"Config 'GuideSpawnerConfig': inteval"});
        ASSERT_EQ(typo.size(), 1u);
        EXPECT_EQ(typo[0], "Config 'GuideSpawnerConfig': inteval: Unknown attribute 'inteval', did you mean 'interval'?");
        const auto range = log.Messages(LogLevel::Warning, {"Config 'GuideSpawnerConfig': count"});
        ASSERT_EQ(range.size(), 1u);
        EXPECT_EQ(range[0], "Config 'GuideSpawnerConfig': count: Value 800 is outside the range [1, 100]");
        EXPECT_EQ(config._count, 800) << "a finding keeps what was read";
        EXPECT_FLOAT_EQ(config._interval, 1.0f) << "a misspelled key leaves the default";
    }

    TEST_F(GuideConfigExampleTest, AFileChangedOnDiskGivesTheEventAndTheClassHoldsTheNewValue) {
        StartOnFake();
        const GuideSpawnerSettings settings(_manager);
        ChangeRecorder changes;
        ASSERT_EQ(settings.Get()._count, kExampleCount);

        ASSERT_NO_FATAL_FAILURE(WriteExampleWith("count=\"8\"", "count=\"12\""));
        PostConfigAndSettle();

        ASSERT_EQ(changes.causes.size(), 1u);
        EXPECT_EQ(changes.causes[0], ConfigEvents::ConfigChangeCause::Reloaded);
        EXPECT_EQ(settings.Get()._count, kChangedCount);
        EXPECT_FLOAT_EQ(settings.Get()._interval, kExampleInterval);
    }

    TEST_F(GuideConfigExampleTest, AFileThatDoesNotParseKeepsTheOldValueAndGivesTheFailureNotTheChange) {
        StartOnFake();
        const GuideSpawnerSettings settings(_manager);
        ChangeRecorder changes;
        FailureRecorder failures;

        WriteFile(ConfigPath(), "<GuideSpawnerConfig count=\"12\"");
        PostConfigAndSettle();

        EXPECT_EQ(failures.names.size(), 1u);
        EXPECT_TRUE(changes.causes.empty());
        EXPECT_EQ(settings.Get()._count, kExampleCount);
    }

    TEST_F(GuideConfigExampleTest, TheManagersOwnSaveGivesOneEventAndItsEchoFromTheWatcherNone) {
        constexpr int32_t kSavedCount = 9;
        StartOnFake();
        const GuideSpawnerSettings settings(_manager);
        ChangeRecorder changes;
        pugi::xml_node root = _manager.GetConfig(GuideSpawnerNames::config).GetPugiNode();
        ASSERT_TRUE(root);

        root.attribute("count").set_value(kSavedCount);
        ASSERT_TRUE(_manager.SaveConfig(GuideSpawnerNames::config));
        EXPECT_EQ(changes.causes.size(), 1u) << "the save";
        EXPECT_EQ(settings.Get()._count, kSavedCount);

        PostConfigAndSettle();
        EXPECT_EQ(changes.causes.size(), 1u) << "the echo of the save";
    }

}  // namespace BECore::Tests
