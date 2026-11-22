#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Common/ContentBinaryFiles.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kRootName = "TextureLibraryFile";
        const PoolString kConfig = "TextureLibrary"_intern;

        /// Every shape of an entry: plain, grouped but not packed, packed with and without the pack record; a nameless entry (skipped by
        /// `Load`) and a repeated name (overwritten in place), so the file carries what `Load` made of them
        constexpr StringView kLibraryXml = R"(<root>
  <sprites>
    <sprite name="Plain" texturePath="assets/a.png"><srcRect x="1" y="2" w="3" h="4"/></sprite>
    <sprite name="Grouped" texturePath="assets/b.png"><srcRect x="0" y="0" w="8" h="8"/><atlas name="ui"/></sprite>
    <sprite name="PackedBare" texturePath="assets/atlas.png"><srcRect x="5" y="6" w="7" h="8"/>
      <atlas name="ui"><source texturePath="assets/c.png"><rect x="1" y="1" w="2" h="2"/></source></atlas></sprite>
    <sprite name="Packed" texturePath="assets/atlas.png"><srcRect x="9" y="9" w="4" h="4"/>
      <atlas name="ui"><source texturePath="assets/d.png"><rect x="3" y="3" w="4" h="4"/></source><pack padding="2" sizeMode="Pot" maxSide="1024"/></atlas></sprite>
    <sprite texturePath="assets/nameless.png"/>
    <sprite name="Plain" texturePath="assets/a2.png"><srcRect x="10" y="20" w="30" h="40"/></sprite>
  </sprites>
</root>)";

        /// `TextureLibrary` can be neither copied nor moved: it is filled in place
        void LoadXml(StringView xml, TextureLibrary& library) {
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(xml));
            library.Load(deserializer);
        }

        String ToXml(const TextureLibrary& library) {
            XmlSerializer serializer;
            library.Write(serializer);
            return serializer.SaveToString();
        }

        TextureLibraryFile AsFile(const TextureLibrary& library) {
            TextureLibraryFile file;
            file.sprites = library.GetAll();
            return file;
        }

        /// The cooked file of a library: the stamp of the holder, then `Write`
        Vector<uint8_t> CookedBytes(const TextureLibrary& library) {
            BinarySerializer writer;
            EXPECT_TRUE(WriteContentStamp(writer, kRootName));
            library.Write(writer);
            return writer.GetData();
        }

    }  // namespace

    TEST(TextureLibraryBinary, WriteGivesTheBytesOfTheHolderSoTheHolderCanStandForTheLibrary) {
        TextureLibrary library;
        LoadXml(kLibraryXml, library);
        ASSERT_EQ(library.GetAll().size(), 4u) << "the nameless entry is skipped and the repeated name is one entry";

        BinarySerializer byLibrary;
        library.Write(byLibrary);
        BinarySerializer byHolder;
        AsFile(library).Serialize(byHolder);
        EXPECT_EQ(byLibrary.GetData(), byHolder.GetData());
    }

    TEST(TextureLibraryBinary, AnEmptyLibraryAndTheEmptyHolderGiveTheSameBytes) {
        BinarySerializer byLibrary;
        TextureLibrary().Write(byLibrary);
        BinarySerializer byHolder;
        TextureLibraryFile().Serialize(byHolder);
        EXPECT_EQ(byLibrary.GetData(), byHolder.GetData());
    }

    /// Managers of their own over a folder of a temporary directory, the way `TextureLibrary::Initialize` reads through the global one
    class TextureLibraryBinaryConfigTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _folder = TestTempDirectory("texlib_bin");
            MakeEmptyDirectory(_folder);
        }

        void TearDown() override {
            RemoveDirectory(_folder);
        }

        /// What `TextureLibrary::Initialize` does with the deserializer of the config, over the folder
        void InitializeFromFolder(TextureLibrary& library) {
            EXPECT_TRUE(_fileSystem.Mount(MountPoints::config, _folder));
            ConfigManager manager(_fileSystem);
            manager.Initialize();
            const auto reader = manager.CreateDeserializer(kConfig);
            EXPECT_NE(reader, nullptr);
            if (reader) {
                library.Load(*reader);
                reader->FinishLoad();
                EXPECT_FALSE(reader->HasErrors());
            }
        }

        std::filesystem::path _folder;
        FileSystem _fileSystem;
    };

    TEST_F(TextureLibraryBinaryConfigTest, ACookedLibraryIsAcceptedAndLoadsEqualToTheXml) {
        TextureLibrary fromXml;
        LoadXml(kLibraryXml, fromXml);
        WriteContentBytes(_folder / "TextureLibrary.bin", CookedBytes(fromXml));

        TextureLibrary fromBin;
        InitializeFromFolder(fromBin);
        ASSERT_EQ(fromBin.GetAll().size(), fromXml.GetAll().size());
        EXPECT_EQ(ToXml(fromBin), ToXml(fromXml));

        const SpriteEntry* packed = fromBin.GetSprite("Packed"_intern);
        ASSERT_NE(packed, nullptr);
        ASSERT_TRUE(packed->atlas.has_value());
        EXPECT_TRUE(packed->atlas->source.has_value());
        EXPECT_TRUE(packed->atlas->pack.has_value());
        const SpriteEntry* bare = fromBin.GetSprite("PackedBare"_intern);
        ASSERT_NE(bare, nullptr);
        ASSERT_TRUE(bare->atlas.has_value());
        EXPECT_TRUE(bare->atlas->source.has_value());
        EXPECT_FALSE(bare->atlas->pack.has_value());
        const SpriteEntry* plain = fromBin.GetSprite("Plain"_intern);
        ASSERT_NE(plain, nullptr);
        EXPECT_FALSE(plain->atlas.has_value());
        EXPECT_EQ(plain->texturePath.ToStringView(), "assets/a2.png") << "the later of two entries of one name is what the file carries";
    }

    TEST_F(TextureLibraryBinaryConfigTest, ALibraryStampedWithAnotherLayoutIsRefused) {
        TextureLibrary library;
        LoadXml(kLibraryXml, library);
        const uint64_t layout = ComputeLayoutHash(kRootName).value_or(0);
        BinarySerializer writer;
        ASSERT_TRUE(writer.BeginObject(BinaryFormat::ContentStampName));
        writer.Write("root", kRootName);
        writer.Write("layout", layout ^ 1);
        writer.EndObject();
        library.Write(writer);
        WriteContentBytes(_folder / "TextureLibrary.bin", writer.GetData());

        ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _folder));
        ConfigManager manager(_fileSystem);
        manager.Initialize();
        EXPECT_FALSE(manager.HasConfig(kConfig));
    }

}  // namespace BECore::Tests
