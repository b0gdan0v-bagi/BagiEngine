#include "ConfigManagerBinaryTest.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FakeFileWatchBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Reflection/SchemaHash.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/BinaryStreams.h>
#include <Common/ContentBinaryFiles.h>
#include <Common/EngineEnvironment.h>
#include <Common/FreshText.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/ConfigEvents.h>
#include <Events/SubscriptionHolder.h>
#include <Generated/ConfigManagerBinaryTest.gen.hpp>

namespace BECore::Tests {

    using namespace BinaryConfigTestData;

    namespace {

        constexpr StringView kRootName = "BinaryConfigRoot";
        const PoolString kConfig = "BinaryProbe"_intern;
        const PoolString kKind = "binary-kind"_intern;
        constexpr StringView kXmlText = "<BinaryProbe id=\"7\" title=\"seven\" kind=\"binary-kind\"/>\n";
        constexpr double kWatchTime = 1.0;

        BinaryConfigRoot MakeRoot(int32_t id = 7) {
            BinaryConfigRoot root;
            root.id = id;
            root.title = "seven";
            root.kind = kKind;
            root.items = {1, 2, 3};
            root.part.label = "part";
            root.part.weight = 1.5f;
            root.parts.resize(2);
            root.parts[0].label = "first";
            root.parts[1].label = "second";
            root.parts[1].weight = 0.25f;
            root.extra = 9;
            return root;
        }

        String ToXml(const BinaryConfigRoot& root) {
            XmlSerializer serializer;
            EXPECT_TRUE(serializer.BeginObject("BinaryConfigRoot"));
            root.Serialize(serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        }

        /// The text of a config file for @p root: its fields on the root element, as `MakeFromConfig` reads them
        String ToConfigXml(const BinaryConfigRoot& root) {
            XmlSerializer serializer;
            root.Serialize(serializer);
            return serializer.SaveToString();
        }

        class ChangeCounter : public SubscriptionHolder {
        public:
            ChangeCounter() {
                Subscribe<ConfigEvents::ConfigChangedEvent, &ChangeCounter::OnChanged>(this);
            }

            size_t count = 0;

        private:
            void OnChanged(const ConfigEvents::ConfigChangedEvent&) {
                ++count;
            }
        };

    }  // namespace

    /// Managers of their own over folders of a temporary directory: layer 0 is the lower one, layer 1 the upper
    class ConfigManagerBinaryTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _lower = TestTempDirectory("cfg_bin_lower");
            _upper = TestTempDirectory("cfg_bin_upper");
            MakeEmptyDirectory(_lower);
            MakeEmptyDirectory(_upper);
        }

        void TearDown() override {
            _manager.StopWatching();
            _watcher.Stop();
            RemoveDirectory(_lower);
            RemoveDirectory(_upper);
        }

        void Start(bool withUpper = false) {
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _lower));
            if (withUpper) {
                ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _upper));
            }
            _manager.Initialize();
        }

        BinaryConfigRoot Read() const {
            BinaryConfigRoot root;
            const auto reader = _manager.CreateDeserializer(kConfig);
            EXPECT_NE(reader, nullptr);
            if (reader) {
                root.Deserialize(*reader);
                EXPECT_FALSE(reader->HasErrors());
            }
            return root;
        }

        std::filesystem::path _lower;
        std::filesystem::path _upper;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        FileWatcher _watcher{_fileSystem};
    };

    TEST_F(ConfigManagerBinaryTest, ABinFileLoadsAndTheDeserializerReadsTheObjectWritten) {
        WriteContentBin(_lower / "BinaryProbe.bin", kRootName, MakeRoot());
        Start();

        EXPECT_TRUE(_manager.HasConfig(kConfig));
        EXPECT_TRUE(_manager.IsBinaryConfig(kConfig));
        EXPECT_TRUE(_manager.IsBinaryConfig("BinaryProbe"));
        EXPECT_EQ(_manager.GetConfigCount(), 1u);
        EXPECT_EQ(_manager.GetConfigFilePath(kConfig), _lower / "BinaryProbe.bin");
        EXPECT_EQ(_manager.GetAllConfigNames().size(), 1u);
        EXPECT_TRUE(_manager.GetConfig(kConfig).IsEmpty()) << "GetConfig serves XML only";
        EXPECT_EQ(ToXml(Read()), ToXml(MakeRoot()));
    }

    TEST_F(ConfigManagerBinaryTest, TheObjectOfAnXmlConfigReadsEqualToThatOfItsBin) {
        WriteContentBin(_lower / "FromBin.bin", kRootName, MakeRoot());
        WriteFile(_lower / "FromXml.xml", ToConfigXml(MakeRoot()));
        Start();

        BinaryConfigRoot fromBin;
        BinaryConfigRoot fromXml;
        {
            const auto reader = _manager.CreateDeserializer("FromBin");
            ASSERT_NE(reader, nullptr);
            fromBin.Deserialize(*reader);
            EXPECT_FALSE(reader->HasErrors());
        }
        {
            const auto reader = _manager.CreateDeserializer("FromXml");
            ASSERT_NE(reader, nullptr);
            fromXml.Deserialize(*reader);
            EXPECT_FALSE(reader->HasErrors());
        }
        EXPECT_EQ(ToXml(fromBin), ToXml(fromXml));
        EXPECT_EQ(ToXml(fromBin), ToXml(MakeRoot()));
    }

    TEST_F(ConfigManagerBinaryTest, ABinOfTheUpperLayerBeatsAnXmlOfTheLowerOneAndTheReverse) {
        WriteFile(_lower / "BinaryProbe.xml", kXmlText);
        WriteContentBin(_upper / "BinaryProbe.bin", kRootName, MakeRoot(21));
        WriteContentBin(_lower / "Other.bin", kRootName, MakeRoot(1));
        WriteFile(_upper / "Other.xml", "<Other id=\"2\"/>\n");
        Start(true);

        EXPECT_TRUE(_manager.IsBinaryConfig(kConfig));
        EXPECT_EQ(Read().id, 21);
        EXPECT_FALSE(_manager.IsBinaryConfig("Other"));
        EXPECT_FALSE(_manager.GetConfig("Other").IsEmpty());
        EXPECT_EQ(_manager.GetConfigFilePath("Other"_intern), _upper / "Other.xml");
    }

    TEST_F(ConfigManagerBinaryTest, AnXmlAndABinOfOneStemInOneLayerLeaveTheConfigAbsent) {
        WriteContentBin(_upper / "BinaryProbe.bin", kRootName, MakeRoot());
        WriteFile(_upper / "BinaryProbe.xml", kXmlText);
        WriteFile(_lower / "BinaryProbe.xml", kXmlText);
        WriteFile(_lower / "Healthy.xml", "<Healthy/>\n");

        LogRecorder log;
        Start(true);
        LogEvent::Flush();

        EXPECT_FALSE(_manager.HasConfig(kConfig)) << "the lower layer's file is not a stand-in";
        EXPECT_TRUE(_manager.HasConfig("Healthy"));
        EXPECT_EQ(log.Count(LogLevel::Error, {"BinaryProbe", "BinaryProbe.bin", "BinaryProbe.xml"}), 1u);
    }

    TEST_F(ConfigManagerBinaryTest, AFileThatTheStampRefusesLeavesTheConfigAbsent) {
        const BinaryConfigRoot root = MakeRoot();
        const uint64_t layout = ComputeLayoutHash(kRootName).value_or(0);
        WriteContentBytes(_lower / "WrongHash.bin", StampedContentBytesWithLayout(kRootName, layout ^ 1, root));
        WriteContentBytes(_lower / "NoStamp.bin", UnstampedContentBytes(root));
        WriteContentBytes(_lower / "UnknownType.bin", StampedContentBytesWithLayout("NoSuchBinaryType", layout, root));
        Vector<uint8_t> truncated = StampedContentBytes(kRootName, root);
        truncated.resize(truncated.size() / 2);
        WriteContentBytes(_lower / "Truncated.bin", truncated);
        WriteContentBin(_lower / "Good.bin", kRootName, root);

        LogRecorder log;
        Start();
        LogEvent::Flush();

        EXPECT_TRUE(_manager.HasConfig("Good"));
        EXPECT_EQ(_manager.GetConfigCount(), 1u);
        for (const char* name : {"WrongHash", "NoStamp", "UnknownType", "Truncated"}) {
            EXPECT_FALSE(_manager.HasConfig(StringView(name))) << name;
            EXPECT_NE(log.Find(LogLevel::Error, {name, ".bin"}), nullptr) << name;
        }
    }

    TEST_F(ConfigManagerBinaryTest, ACookedConfigIsReadStraightIntoAPoolBlockAndItsPoolStringIsThePoolsOwn) {
        namespace Streams = BinaryStreams;
        const String fresh = FreshText("config_kind");
        const uint64_t layout = ComputeLayoutHash(kRootName).value_or(0);
        const Vector<uint8_t> stream = Streams::Build({Streams::Object(BinaryFormat::ContentStampName, {Streams::Text("root", kRootName), Streams::Number("layout", layout)}),
                                                       Streams::Number("id", int32_t{31}), Streams::Pooled("kind", fresh)});
        WriteContentBytes(_lower / "BinaryProbe.bin", stream);
        ASSERT_FALSE(PoolString::Find(StringView(fresh)).has_value());
        const size_t blocksBefore = PoolString::GetStats().adopted.blocks;
        Start();

        EXPECT_GT(PoolString::GetStats().adopted.blocks, blocksBefore);
        const Optional<PoolString> found = PoolString::Find(StringView(fresh));
        ASSERT_TRUE(found.has_value()) << "the text came into the pool with the file's block";
        const auto reader = _manager.CreateDeserializer(kConfig);
        ASSERT_NE(reader, nullptr);
        BinaryConfigRoot read;
        read.Deserialize(*reader);
        EXPECT_EQ(read.id, 31);
        EXPECT_EQ(read.kind, *found) << "a PoolString read from the content is the pool's entry of the text";
    }

    TEST_F(ConfigManagerBinaryTest, ACookedConfigWithADamagedPoolSectionLeavesTheConfigAbsentAndSaysToCookAgain) {
        BinaryConfigRoot root = MakeRoot();
        const String fresh = FreshText("config_damaged");
        root.kind = PoolString::Intern(StringView(fresh));
        Vector<uint8_t> bytes = StampedContentBytes(kRootName, root);
        const auto at = eastl::search(bytes.begin(), bytes.end(), fresh.begin(), fresh.end());
        ASSERT_NE(at, bytes.end());
        *at = static_cast<uint8_t>(*at ^ 1);  // the checksum of the section no longer matches
        WriteContentBytes(_lower / "Damaged.bin", bytes);
        WriteContentBin(_lower / "Good.bin", kRootName, root);

        LogRecorder log;
        Start();
        LogEvent::Flush();

        EXPECT_FALSE(_manager.HasConfig("Damaged"));
        EXPECT_TRUE(_manager.HasConfig("Good"));
        EXPECT_NE(log.Find(LogLevel::Error, {"Damaged", ".bin", "cook"}), nullptr);
    }

    TEST_F(ConfigManagerBinaryTest, ACookedConfigOfVersionTwoStillLoadsAndItsStringsAreHashedOnRead) {
        namespace Streams = BinaryStreams;
        const String fresh = FreshText("config_v2");
        const uint64_t layout = ComputeLayoutHash(kRootName).value_or(0);
        Streams::Options options;
        options.version = BinaryFormat::TabledVersion;
        const Vector<uint8_t> stream = Streams::Build({Streams::Object(BinaryFormat::ContentStampName, {Streams::Text("root", kRootName), Streams::Number("layout", layout)}),
                                                       Streams::Number("id", int32_t{31}), Streams::Pooled("kind", fresh)},
                                                      options);
        WriteContentBytes(_lower / "BinaryProbe.bin", stream);
        const size_t blocksBefore = PoolString::GetStats().adopted.blocks;
        Start();

        ASSERT_TRUE(_manager.IsBinaryConfig(kConfig));
        EXPECT_EQ(PoolString::GetStats().adopted.blocks, blocksBefore) << "a file without a pool section adopts nothing";
        const auto reader = _manager.CreateDeserializer(kConfig);
        ASSERT_NE(reader, nullptr);
        BinaryConfigRoot read;
        read.Deserialize(*reader);
        EXPECT_EQ(read.id, 31);
        EXPECT_EQ(read.kind.ToStringView(), StringView(fresh));
        EXPECT_EQ(PoolString::Find(StringView(fresh)), read.kind);
    }

    TEST_F(ConfigManagerBinaryTest, SaveAndReloadOfABinConfigFailAndChangeNothing) {
        const std::filesystem::path file = _lower / "BinaryProbe.bin";
        WriteContentBin(file, kRootName, MakeRoot());
        const Vector<uint8_t> before = ReadFileBytes(file);
        Start();

        LogRecorder log;
        ChangeCounter changes;
        EXPECT_FALSE(_manager.SaveConfig(kConfig));
        EXPECT_FALSE(_manager.ReloadConfig(kConfig));
        LogEvent::Flush();

        EXPECT_EQ(log.Count(LogLevel::Error, {"BinaryProbe", "binary"}), 2u);
        EXPECT_EQ(changes.count, 0u);
        EXPECT_EQ(ReadFileBytes(file), before);
        EXPECT_EQ(Read().id, 7);
    }

    TEST_F(ConfigManagerBinaryTest, AChangedBinUnderTheWatcherPostsNoEventAndNoFailure) {
        const std::filesystem::path file = _lower / "BinaryProbe.bin";
        WriteContentBin(file, kRootName, MakeRoot(7));
        WriteFile(_lower / "Plain.xml", "<Plain value=\"one\"/>\n");
        Start();
        auto fake = New<FakeFileWatchBackend>();
        _watcher.Start(IntrusivePtr<IFileWatchBackend>(fake));
        _manager.StartWatching(_watcher);

        ChangeCounter changes;
        WriteContentBin(file, kRootName, MakeRoot(8));
        fake->Emit(file);
        _watcher.Update(kWatchTime);
        _watcher.Update(kWatchTime + 2.0 * kFileWatchSettleSeconds);

        EXPECT_EQ(changes.count, 0u);
        EXPECT_EQ(Read().id, 7) << "the loaded bytes are the ones read at start";

        // An XML config beside it still reloads
        const std::filesystem::path plain = _lower / "Plain.xml";
        WriteFile(plain, "<Plain value=\"two\"/>\n");
        fake->Emit(plain);
        _watcher.Update(kWatchTime + 4.0 * kFileWatchSettleSeconds);
        _watcher.Update(kWatchTime + 6.0 * kFileWatchSettleSeconds);
        EXPECT_EQ(changes.count, 1u);
        EXPECT_EQ(_manager.GetConfig("Plain").ParseAttribute<StringView>("value").value_or(StringView()), "two");
    }

}  // namespace BECore::Tests
