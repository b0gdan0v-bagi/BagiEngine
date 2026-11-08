#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// The shape of the Chess game's config/TextureLibrary.xml: a header comment, two rows of the atlas
        constexpr eastl::string_view kChessShaped = R"(<?xml version="1.0"?>
<!--
  Sprite atlas, a 3x2 grid. Cell width 116.67, cell height 190.5.
-->
<root>
  <sprites>
    <sprite name="WhiteKing" texturePath="assets/chess_test.png">
      <srcRect x="0" y="0" w="116.67" h="190.5" />
    </sprite>
    <sprite name="WhiteQueen" texturePath="assets/chess_test.png">
      <srcRect x="116.67" y="0" w="116.67" h="190.5" />
    </sprite>
    <sprite name="BlackKing" texturePath="assets/chess_test.png">
      <srcRect x="0" y="190.5" w="116.67" h="190.5" />
    </sprite>
  </sprites>
</root>)";

        /// Reads @p xml (the text of a whole config file) into @p library; the errors go to @p errors when given
        void LoadInto(TextureLibrary& library, eastl::string_view xml, eastl::vector<DeserializeError>* errors = nullptr) {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(xml)) << xml.data();
            library.Load(deserializer);
            if (errors != nullptr) {
                *errors = deserializer.GetErrors();
            }
        }

        SpriteEntry MakeEntry(const char* name, const char* path, Rect rect) {
            SpriteEntry entry;
            entry.name = PoolString::Intern(name);
            entry.texturePath = PoolString::Intern(path);
            entry.srcRect = rect;
            return entry;
        }

    }  // namespace

    TEST(TextureLibraryTest, LoadsTheChessShapedFileInOrder) {
        TextureLibrary library;
        eastl::vector<DeserializeError> errors;
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, kChessShaped, &errors));
        EXPECT_TRUE(errors.empty());

        ASSERT_EQ(library.GetAll().size(), 3u);
        EXPECT_EQ(library.GetAll()[0].name.ToStringView(), "WhiteKing");
        EXPECT_EQ(library.GetAll()[1].name.ToStringView(), "WhiteQueen");
        EXPECT_EQ(library.GetAll()[2].name.ToStringView(), "BlackKing");

        const SpriteEntry* king = library.GetSprite(PoolString::Intern("WhiteKing"));
        ASSERT_NE(king, nullptr);
        EXPECT_EQ(king->texturePath.ToStringView(), "assets/chess_test.png");
        EXPECT_FLOAT_EQ(king->srcRect.x, 0.0f);
        EXPECT_FLOAT_EQ(king->srcRect.y, 0.0f);
        EXPECT_FLOAT_EQ(king->srcRect.w, 116.67f);
        EXPECT_FLOAT_EQ(king->srcRect.h, 190.5f);

        const SpriteEntry* second = library.GetSprite(PoolString::Intern("BlackKing"));
        ASSERT_NE(second, nullptr);
        EXPECT_FLOAT_EQ(second->srcRect.y, 190.5f);
        EXPECT_EQ(library.GetSprite(PoolString::Intern("Nothing")), nullptr);
    }

    TEST(TextureLibraryTest, MissingPartsAreTheDefault) {
        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, R"(<root><sprites>
            <sprite name="NoRect" texturePath="a.png"/>
            <sprite name="NoPath"><srcRect x="1" y="2" w="3" h="4"/></sprite>
        </sprites></root>)"));

        ASSERT_EQ(library.GetAll().size(), 2u);
        const SpriteEntry* noRect = library.GetSprite(PoolString::Intern("NoRect"));
        ASSERT_NE(noRect, nullptr);
        EXPECT_EQ(noRect->texturePath.ToStringView(), "a.png");
        EXPECT_TRUE(noRect->srcRect.IsEmpty());
        EXPECT_FLOAT_EQ(noRect->srcRect.x, 0.0f);

        const SpriteEntry* noPath = library.GetSprite(PoolString::Intern("NoPath"));
        ASSERT_NE(noPath, nullptr);
        EXPECT_TRUE(noPath->texturePath.Empty());
        EXPECT_FLOAT_EQ(noPath->srcRect.w, 3.0f);
        EXPECT_FLOAT_EQ(noPath->srcRect.h, 4.0f);
    }

    TEST(TextureLibraryTest, EntriesWithoutANameAreSkipped) {
        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, R"(<root><sprites>
            <sprite texturePath="a.png"><srcRect x="1" y="1" w="1" h="1"/></sprite>
            <sprite name="" texturePath="b.png"/>
            <!-- a comment is one element without a name -->
            <sprite name="Named" texturePath="c.png"/>
        </sprites></root>)"));

        ASSERT_EQ(library.GetAll().size(), 1u);
        EXPECT_EQ(library.GetAll()[0].name.ToStringView(), "Named");
    }

    TEST(TextureLibraryTest, ADuplicateNameOverwritesInPlace) {
        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, R"(<root><sprites>
            <sprite name="A" texturePath="first.png"/>
            <sprite name="B" texturePath="b.png"/>
            <sprite name="A" texturePath="second.png"><srcRect x="5" y="6" w="7" h="8"/></sprite>
        </sprites></root>)"));

        ASSERT_EQ(library.GetAll().size(), 2u);
        EXPECT_EQ(library.GetAll()[0].name.ToStringView(), "A");
        EXPECT_EQ(library.GetAll()[0].texturePath.ToStringView(), "second.png");
        EXPECT_FLOAT_EQ(library.GetAll()[0].srcRect.w, 7.0f);
        EXPECT_EQ(library.GetAll()[1].name.ToStringView(), "B");
    }

    TEST(TextureLibraryTest, NoSpritesGivesAnEmptyLibraryWithoutErrors) {
        for (const eastl::string_view xml : {eastl::string_view("<root/>"), eastl::string_view("<root><sprites/></root>"), eastl::string_view("<root><other/></root>")}) {
            TextureLibrary library;
            library.AddOrUpdate(MakeEntry("Old", "old.png", Rect(1, 2, 3, 4)));
            eastl::vector<DeserializeError> errors;
            ASSERT_NO_FATAL_FAILURE(LoadInto(library, xml, &errors));
            EXPECT_TRUE(library.GetAll().empty()) << xml.data();
            EXPECT_TRUE(errors.empty()) << xml.data();
        }
    }

    TEST(TextureLibraryTest, LoadReplacesWhatWasThere) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("Old", "old.png", Rect(1, 2, 3, 4)));
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, kChessShaped));
        EXPECT_EQ(library.GetAll().size(), 3u);
        EXPECT_EQ(library.GetSprite(PoolString::Intern("Old")), nullptr);
    }

    TEST(TextureLibraryTest, AMalformedNumberIsReadAsZero) {
        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, R"(<root><sprites>
            <sprite name="Bad" texturePath="a.png"><srcRect x="abc" y="2" w="3" h="4"/></sprite>
        </sprites></root>)"));

        const SpriteEntry* bad = library.GetSprite(PoolString::Intern("Bad"));
        ASSERT_NE(bad, nullptr);
        EXPECT_FLOAT_EQ(bad->srcRect.x, 0.0f);
        EXPECT_FLOAT_EQ(bad->srcRect.y, 2.0f);
        EXPECT_FLOAT_EQ(bad->srcRect.w, 3.0f);
        EXPECT_FLOAT_EQ(bad->srcRect.h, 4.0f);
    }

    TEST(TextureLibraryTest, WriteGivesTheShapeOfTheFile) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("First", "assets/a.png", Rect(0, 190.5f, 116.67f, 20)));
        library.AddOrUpdate(MakeEntry("Second", "assets/b.png", Rect(1, 2, 3, 4)));

        XmlSerializer serializer;
        library.Write(serializer);

        const pugi::xml_node root = serializer.GetRootNode();
        EXPECT_STREQ(root.name(), "root");
        EXPECT_FALSE(root.attribute("name"));
        const pugi::xml_node sprites = root.first_child();
        ASSERT_TRUE(sprites);
        EXPECT_STREQ(sprites.name(), "sprites");
        EXPECT_FALSE(root.first_child().next_sibling());
        EXPECT_FALSE(sprites.first_attribute());

        const pugi::xml_node first = sprites.first_child();
        ASSERT_TRUE(first);
        EXPECT_STREQ(first.name(), "sprite");
        const pugi::xml_attribute nameAttr = first.first_attribute();
        EXPECT_STREQ(nameAttr.name(), "name");
        EXPECT_STREQ(nameAttr.value(), "First");
        EXPECT_STREQ(nameAttr.next_attribute().name(), "texturePath");
        EXPECT_STREQ(nameAttr.next_attribute().value(), "assets/a.png");
        EXPECT_FALSE(nameAttr.next_attribute().next_attribute());

        const pugi::xml_node srcRect = first.first_child();
        ASSERT_TRUE(srcRect);
        EXPECT_STREQ(srcRect.name(), "srcRect");
        EXPECT_FALSE(srcRect.next_sibling());
        // the whole attribute order of srcRect: x y w h and nothing else
        const pugi::xml_attribute xAttr = srcRect.first_attribute();
        EXPECT_STREQ(xAttr.name(), "x");
        EXPECT_STREQ(xAttr.next_attribute().name(), "y");
        EXPECT_STREQ(xAttr.next_attribute().next_attribute().name(), "w");
        EXPECT_STREQ(xAttr.next_attribute().next_attribute().next_attribute().name(), "h");
        EXPECT_FALSE(xAttr.next_attribute().next_attribute().next_attribute().next_attribute());
        EXPECT_EQ(std::distance(srcRect.attributes_begin(), srcRect.attributes_end()), 4);
        EXPECT_STREQ(srcRect.attribute("x").value(), "0");
        EXPECT_STREQ(srcRect.attribute("y").value(), "190.5");
        EXPECT_NEAR(srcRect.attribute("w").as_float(), 116.67f, 1e-4f);
        EXPECT_FLOAT_EQ(srcRect.attribute("h").as_float(), 20.0f);
        EXPECT_FALSE(srcRect.attribute("h").next_attribute());

        const pugi::xml_node second = first.next_sibling();
        ASSERT_TRUE(second);
        EXPECT_STREQ(second.name(), "sprite");
        EXPECT_STREQ(second.attribute("name").value(), "Second");
        EXPECT_FLOAT_EQ(second.child("srcRect").attribute("w").as_float(), 3.0f);
        EXPECT_FALSE(second.next_sibling());
    }

    TEST(TextureLibraryTest, AnEmptyLibraryWritesAnEmptySpritesElement) {
        TextureLibrary library;
        XmlSerializer serializer;
        library.Write(serializer);

        const pugi::xml_node sprites = serializer.GetRootNode().child("sprites");
        ASSERT_TRUE(sprites);
        EXPECT_FALSE(sprites.first_child());
        EXPECT_FALSE(sprites.first_attribute());
    }

    TEST(TextureLibraryTest, WriteThenLoadGivesTheSameLibrary) {
        TextureLibrary original;
        ASSERT_NO_FATAL_FAILURE(LoadInto(original, kChessShaped));

        XmlSerializer serializer;
        original.Write(serializer);
        TextureLibrary copy;
        ASSERT_NO_FATAL_FAILURE(LoadInto(copy, serializer.SaveToString()));

        ASSERT_EQ(copy.GetAll().size(), original.GetAll().size());
        for (size_t i = 0; i < original.GetAll().size(); ++i) {
            const SpriteEntry& a = original.GetAll()[i];
            const SpriteEntry& b = copy.GetAll()[i];
            EXPECT_EQ(a.name, b.name);
            EXPECT_EQ(a.texturePath, b.texturePath);
            EXPECT_FLOAT_EQ(a.srcRect.x, b.srcRect.x);
            EXPECT_FLOAT_EQ(a.srcRect.y, b.srcRect.y);
            EXPECT_FLOAT_EQ(a.srcRect.w, b.srcRect.w);
            EXPECT_FLOAT_EQ(a.srcRect.h, b.srcRect.h);
        }
    }

    TEST(TextureLibraryTest, ARemovedEntryIsNotWritten) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("Keep", "k.png", Rect(1, 2, 3, 4)));
        library.AddOrUpdate(MakeEntry("Drop", "d.png", Rect(5, 6, 7, 8)));
        library.Remove(PoolString::Intern("Drop"));

        XmlSerializer serializer;
        library.Write(serializer);

        const pugi::xml_node sprites = serializer.GetRootNode().child("sprites");
        ASSERT_TRUE(sprites);
        EXPECT_STREQ(sprites.first_child().attribute("name").value(), "Keep");
        EXPECT_FALSE(sprites.first_child().next_sibling());
    }

    TEST(TextureLibraryTest, ASpriteEntryRoundTripsThroughAnObject) {
        const SpriteEntry entry = MakeEntry("Solo", "assets/solo.png", Rect(1.5f, 2.5f, 3.5f, 4.5f));

        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("sprite"));
        entry.Serialize(serializer);
        serializer.EndObject();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(serializer.SaveToString()));
        SpriteEntry read;
        ASSERT_TRUE(deserializer.BeginObject("sprite"));
        read.Deserialize(deserializer);
        deserializer.EndObject();

        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_EQ(read.name, entry.name);
        EXPECT_EQ(read.texturePath, entry.texturePath);
        EXPECT_FLOAT_EQ(read.srcRect.x, 1.5f);
        EXPECT_FLOAT_EQ(read.srcRect.y, 2.5f);
        EXPECT_FLOAT_EQ(read.srcRect.w, 3.5f);
        EXPECT_FLOAT_EQ(read.srcRect.h, 4.5f);
    }

    /// Initialize and Save go through the config manager, which needs the engine services. The engine ships no TextureLibrary.xml
    /// in config/ (the active game does), so the library is empty here; if the engine ever ships one, drop the "empty" expectations.
    class TextureLibraryConfigTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST_F(TextureLibraryConfigTest, InitializeWithoutAConfigLeavesTheLibraryEmptyAndClearsWhatWasThere) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("Old", "assets/old.png", Rect(1, 2, 3, 4)));
        ASSERT_EQ(library.GetAll().size(), 1u);

        library.Initialize();
        EXPECT_TRUE(library.GetAll().empty());
        EXPECT_EQ(library.GetSprite(PoolString::Intern("Old")), nullptr);

        library.AddOrUpdate(MakeEntry("Added", "assets/a.png", Rect()));
        library.Initialize();
        EXPECT_TRUE(library.GetAll().empty());
    }

    TEST_F(TextureLibraryConfigTest, SaveWithoutTheConfigReturnsFalse) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("Solo", "assets/solo.png", Rect(1, 2, 3, 4)));
        EXPECT_FALSE(library.Save());
        EXPECT_EQ(library.GetAll().size(), 1u);
    }

}  // namespace BECore::Tests
