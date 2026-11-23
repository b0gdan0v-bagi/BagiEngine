// XmlDocument::ReadFileInto: the bytes of ReadFileBytes in an arena, and the config manager loading a file too big for the scratch arena.

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/XmlDocument.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Memory/ScratchArena.h>
#include <BECore/Reflection/ReaderLimits.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kUnlimited = ReaderLimits::kMaxXmlDocumentBytes;
        constexpr size_t kBigFileBytes = 64 * 1024;
        /// Bigger than the file size a config is read through the scratch arena up to
        constexpr size_t kBeyondScratchBytes = 512 * 1024;

        /// An XML text of about @p bytes bytes: a root with the attribute `value` and a comment as padding
        String TextOfSize(size_t bytes, StringView value = "x") {
            String text = Format("<Big value=\"{}\"><!--", value);
            text.append(bytes, 'a');
            text += "--></Big>\n";
            return text;
        }

    }  // namespace

    class ConfigReadScratchTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _dir = TestTempDirectory("cfg_scratch");
            MakeEmptyDirectory(_dir);
        }

        void TearDown() override {
            RemoveDirectory(_dir);
        }

        /// What ReadFileInto gives for @p path, as a String, so it can be compared with ReadFileBytes
        Optional<String> ReadInto(const std::filesystem::path& path, size_t maxBytes = kUnlimited) {
            ScratchScope scratch;
            const Optional<StringView> view = XmlDocument::ReadFileInto(path, scratch.GetArena(), maxBytes);
            if (!view) {
                return std::nullopt;
            }
            return String(view->data(), view->size());
        }

        std::filesystem::path _dir;
    };

    TEST_F(ConfigReadScratchTest, GivesTheBytesOfReadFileBytes) {
        const String text = TextOfSize(1000);
        WriteFile(_dir / "File.xml", text);
        WriteFile(_dir / "Empty.xml", "");

        EXPECT_EQ(ReadInto(_dir / "File.xml"), XmlDocument::ReadFileBytes(_dir / "File.xml"));
        EXPECT_EQ(ReadInto(_dir / "File.xml"), Optional<String>(text));
        EXPECT_EQ(ReadInto(_dir / "Empty.xml"), XmlDocument::ReadFileBytes(_dir / "Empty.xml"));
        EXPECT_EQ(ReadInto(_dir / "Empty.xml"), Optional<String>(String{}));
    }

    TEST_F(ConfigReadScratchTest, GivesNothingWhereReadFileBytesGivesNothing) {
        WriteFile(_dir / "Over.xml", TextOfSize(ReaderLimits::kMaxXmlDocumentBytes));

        for (const char* name : {"Missing.xml", "Over.xml", ""}) {
            const std::filesystem::path path = name[0] == '\0' ? _dir : _dir / name;  // a folder is no file
            EXPECT_FALSE(XmlDocument::ReadFileBytes(path).has_value()) << name;
            EXPECT_FALSE(ReadInto(path).has_value()) << name;
        }
    }

    TEST_F(ConfigReadScratchTest, AFileOverTheCallersLimitIsNotRead) {
        WriteFile(_dir / "File.xml", TextOfSize(1000));

        EXPECT_TRUE(ReadInto(_dir / "File.xml", 2000).has_value());
        EXPECT_FALSE(ReadInto(_dir / "File.xml", 500).has_value());
    }

    TEST_F(ConfigReadScratchTest, ASecondReadAllocatesNoBlockOfTheFilesSize) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        WriteFile(_dir / "Big.xml", TextOfSize(kBigFileBytes));
        ASSERT_TRUE(ReadInto(_dir / "Big.xml").has_value());  // the scratch arena takes its chunk

        const AllocCount scratch = CountAllocations([&] {
            ScratchScope arena;
            EXPECT_TRUE(XmlDocument::ReadFileInto(_dir / "Big.xml", arena.GetArena(), kUnlimited).has_value());
        });
        const AllocCount heap = CountAllocations([&] { EXPECT_TRUE(XmlDocument::ReadFileBytes(_dir / "Big.xml").has_value()); });

        EXPECT_GE(heap.bytes, kBigFileBytes);
        EXPECT_LT(scratch.bytes, kBigFileBytes);
    }

    TEST_F(ConfigReadScratchTest, AConfigBiggerThanTheScratchLimitStillLoadsAndReloads) {
        EngineEnvironment::EnsureCoreServices();
        WriteFile(_dir / "BigConfig.xml", TextOfSize(kBeyondScratchBytes));
        WriteFile(_dir / "SmallConfig.xml", TextOfSize(100));
        FileSystem fileSystem;
        ASSERT_TRUE(fileSystem.Mount(MountPoints::config, _dir));
        ConfigManager manager(fileSystem);
        manager.Initialize();

        for (const StringView name : {"BigConfig", "SmallConfig"}) {
            const auto value = manager.GetConfig(name).ParseAttribute<StringView>("value");
            ASSERT_TRUE(value.has_value()) << name.data();
            EXPECT_EQ(*value, "x");
        }

        WriteFile(_dir / "BigConfig.xml", TextOfSize(kBeyondScratchBytes, "y"));
        ASSERT_TRUE(manager.ReloadConfig("BigConfig"_intern));
        EXPECT_EQ(manager.GetConfig("BigConfig").ParseAttribute<StringView>("value"), Optional<StringView>("y"));
    }

}  // namespace BECore::Tests
