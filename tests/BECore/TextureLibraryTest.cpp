#include <BECore/Config/XmlDocument.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// A texture library file as a game writes it: a header comment, then sprites from two rows of an atlas
        constexpr StringView kAtlasFile = R"(<?xml version="1.0"?>
<!--
  Sprite atlas, a 3x2 grid. Cell width 116.67, cell height 190.5.
-->
<root>
  <sprites>
    <sprite name="RowOneFirst" texturePath="assets/sheet_test.png">
      <srcRect x="0" y="0" w="116.67" h="190.5" />
    </sprite>
    <sprite name="RowOneSecond" texturePath="assets/sheet_test.png">
      <srcRect x="116.67" y="0" w="116.67" h="190.5" />
    </sprite>
    <sprite name="RowTwoFirst" texturePath="assets/sheet_test.png">
      <srcRect x="0" y="190.5" w="116.67" h="190.5" />
    </sprite>
  </sprites>
</root>)";

        /// Reads @p xml (the text of a whole config file) into @p library; the errors go to @p errors when given
        void LoadInto(TextureLibrary& library, StringView xml, Vector<DeserializeError>* errors = nullptr) {
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

    TEST(TextureLibraryTest, LoadsAnAtlasFileInOrder) {
        TextureLibrary library;
        Vector<DeserializeError> errors;
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, kAtlasFile, &errors));
        EXPECT_TRUE(errors.empty());

        ASSERT_EQ(library.GetAll().size(), 3u);
        EXPECT_EQ(library.GetAll()[0].name.ToStringView(), "RowOneFirst");
        EXPECT_EQ(library.GetAll()[1].name.ToStringView(), "RowOneSecond");
        EXPECT_EQ(library.GetAll()[2].name.ToStringView(), "RowTwoFirst");

        const SpriteEntry* first = library.GetSprite("RowOneFirst"_intern);
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(first->texturePath.ToStringView(), "assets/sheet_test.png");
        EXPECT_FLOAT_EQ(first->srcRect.x, 0.0f);
        EXPECT_FLOAT_EQ(first->srcRect.y, 0.0f);
        EXPECT_FLOAT_EQ(first->srcRect.w, 116.67f);
        EXPECT_FLOAT_EQ(first->srcRect.h, 190.5f);

        const SpriteEntry* secondRow = library.GetSprite("RowTwoFirst"_intern);
        ASSERT_NE(secondRow, nullptr);
        EXPECT_FLOAT_EQ(secondRow->srcRect.y, 190.5f);
        EXPECT_EQ(library.GetSprite("Nothing"_intern), nullptr);
    }

    TEST(TextureLibraryTest, MissingPartsAreTheDefault) {
        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, R"(<root><sprites>
            <sprite name="NoRect" texturePath="a.png"/>
            <sprite name="NoPath"><srcRect x="1" y="2" w="3" h="4"/></sprite>
        </sprites></root>)"));

        ASSERT_EQ(library.GetAll().size(), 2u);
        const SpriteEntry* noRect = library.GetSprite("NoRect"_intern);
        ASSERT_NE(noRect, nullptr);
        EXPECT_EQ(noRect->texturePath.ToStringView(), "a.png");
        EXPECT_TRUE(noRect->srcRect.IsEmpty());
        EXPECT_FLOAT_EQ(noRect->srcRect.x, 0.0f);

        const SpriteEntry* noPath = library.GetSprite("NoPath"_intern);
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

    /// The loader reserves for `count` before it knows which entries are skipped or repeated: the reserve must change neither
    /// the content nor the order
    TEST(TextureLibraryTest, ALargeFileWithARepeatedAndANamelessEntryKeepsContentAndOrder) {
        constexpr size_t kSprites = 500;
        String xml = "<root><sprites>";
        for (size_t i = 0; i < kSprites; ++i) {
            xml += Format("<sprite name=\"Big{}\" texturePath=\"first{}.png\"/>", i, i);
            if (i == 100) {
                xml += "<sprite texturePath=\"nameless.png\"/>";
            }
        }
        xml += "<sprite name=\"Big7\" texturePath=\"second.png\"><srcRect x=\"1\" y=\"2\" w=\"3\" h=\"4\"/></sprite>";
        xml += "</sprites></root>";

        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, xml));

        ASSERT_EQ(library.GetAll().size(), kSprites);
        for (size_t i = 0; i < kSprites; ++i) {
            const SpriteEntry& entry = library.GetAll()[i];
            EXPECT_EQ(entry.name.ToStringView(), StringView(Format("Big{}", i))) << i;
            EXPECT_EQ(entry.texturePath.ToStringView(), i == 7 ? StringView("second.png") : StringView(Format("first{}.png", i))) << i;
        }
        EXPECT_FLOAT_EQ(library.GetAll()[7].srcRect.h, 4.0f);
        const SpriteEntry* found = library.GetSprite("Big499"_intern);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found, &library.GetAll()[499]);
    }

    TEST(TextureLibraryTest, NoSpritesGivesAnEmptyLibraryWithoutErrors) {
        for (const StringView xml : {StringView("<root/>"), StringView("<root><sprites/></root>"), StringView("<root><other/></root>")}) {
            TextureLibrary library;
            library.AddOrUpdate(MakeEntry("Old", "old.png", Rect(1, 2, 3, 4)));
            Vector<DeserializeError> errors;
            ASSERT_NO_FATAL_FAILURE(LoadInto(library, xml, &errors));
            EXPECT_TRUE(library.GetAll().empty()) << xml.data();
            EXPECT_TRUE(errors.empty()) << xml.data();
        }
    }

    TEST(TextureLibraryTest, LoadReplacesWhatWasThere) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("Old", "old.png", Rect(1, 2, 3, 4)));
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, kAtlasFile));
        EXPECT_EQ(library.GetAll().size(), 3u);
        EXPECT_EQ(library.GetSprite("Old"_intern), nullptr);
    }

    TEST(TextureLibraryTest, AMalformedNumberIsReadAsZero) {
        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, R"(<root><sprites>
            <sprite name="Bad" texturePath="a.png"><srcRect x="abc" y="2" w="3" h="4"/></sprite>
        </sprites></root>)"));

        const SpriteEntry* bad = library.GetSprite("Bad"_intern);
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
        ASSERT_NO_FATAL_FAILURE(LoadInto(original, kAtlasFile));

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
        library.Remove("Drop"_intern);

        XmlSerializer serializer;
        library.Write(serializer);

        const pugi::xml_node sprites = serializer.GetRootNode().child("sprites");
        ASSERT_TRUE(sprites);
        EXPECT_STREQ(sprites.first_child().attribute("name").value(), "Keep");
        EXPECT_FALSE(sprites.first_child().next_sibling());
    }

    TEST(TextureLibraryTest, RenameKeepsPositionAndData) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("A", "a.png", Rect(1, 2, 3, 4)));
        library.AddOrUpdate(MakeEntry("B", "b.png", Rect(5, 6, 7, 8)));
        library.AddOrUpdate(MakeEntry("C", "c.png", Rect(9, 10, 11, 12)));

        EXPECT_TRUE(library.Rename("B"_intern, "Renamed"_intern));

        ASSERT_EQ(library.GetAll().size(), 3u);
        EXPECT_EQ(library.GetAll()[1].name.ToStringView(), "Renamed");
        EXPECT_EQ(library.GetAll()[1].texturePath.ToStringView(), "b.png");
        EXPECT_FLOAT_EQ(library.GetAll()[1].srcRect.w, 7.0f);
        EXPECT_EQ(library.GetSprite("B"_intern), nullptr);
        EXPECT_EQ(library.GetSprite("Renamed"_intern), &library.GetAll()[1]);
        EXPECT_EQ(library.GetSprite("C"_intern), &library.GetAll()[2]);

        EXPECT_TRUE(library.Rename("A"_intern, "A"_intern));
        EXPECT_EQ(library.GetAll()[0].name.ToStringView(), "A");
    }

    /// The name index grows past its first size while entries are added one by one, and a Remove shifts the positions it points at
    TEST(TextureLibraryTest, EveryNameIsFoundAfterTheIndexGrowsAndAfterARemove) {
        constexpr size_t kCount = 40;
        constexpr size_t kRemoved = 17;
        TextureLibrary library;
        for (size_t i = 0; i < kCount; ++i) {
            library.AddOrUpdate(MakeEntry(Format("Grow{}", i).c_str(), "g.png", Rect(1, 2, 3, 4)));
        }
        ASSERT_EQ(library.GetAll().size(), kCount);
        for (size_t i = 0; i < kCount; ++i) {
            EXPECT_EQ(library.GetSprite(PoolString::Intern(Format("Grow{}", i))), &library.GetAll()[i]) << i;
        }
        EXPECT_EQ(library.GetSprite("GrowMissing"_intern), nullptr);

        library.Remove(PoolString::Intern(Format("Grow{}", kRemoved)));
        ASSERT_EQ(library.GetAll().size(), kCount - 1);
        EXPECT_EQ(library.GetSprite(PoolString::Intern(Format("Grow{}", kRemoved))), nullptr);
        for (size_t i = 0; i < kCount - 1; ++i) {
            const size_t original = i < kRemoved ? i : i + 1;
            EXPECT_EQ(library.GetSprite(PoolString::Intern(Format("Grow{}", original))), &library.GetAll()[i]) << original;
        }

        EXPECT_TRUE(library.Rename("Grow3"_intern, "GrowRenamed"_intern));
        EXPECT_EQ(library.GetSprite("Grow3"_intern), nullptr);
        EXPECT_EQ(library.GetSprite("GrowRenamed"_intern), &library.GetAll()[3]);
    }

    TEST(TextureLibraryTest, RenameOntoAnExistingNameChangesNothing) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("A", "a.png", Rect(1, 2, 3, 4)));
        library.AddOrUpdate(MakeEntry("B", "b.png", Rect(5, 6, 7, 8)));

        EXPECT_FALSE(library.Rename("A"_intern, "B"_intern));

        ASSERT_EQ(library.GetAll().size(), 2u);
        EXPECT_EQ(library.GetAll()[0].name.ToStringView(), "A");
        EXPECT_EQ(library.GetAll()[0].texturePath.ToStringView(), "a.png");
        EXPECT_EQ(library.GetAll()[1].name.ToStringView(), "B");
        EXPECT_EQ(library.GetAll()[1].texturePath.ToStringView(), "b.png");
        EXPECT_EQ(library.GetSprite("A"_intern), &library.GetAll()[0]);
        EXPECT_EQ(library.GetSprite("B"_intern), &library.GetAll()[1]);
    }

    TEST(TextureLibraryTest, RenameRefusesAnEmptyOrMissingName) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("A", "a.png", Rect(1, 2, 3, 4)));

        EXPECT_FALSE(library.Rename("A"_intern, PoolString{}));
        EXPECT_FALSE(library.Rename("Missing"_intern, "Other"_intern));

        ASSERT_EQ(library.GetAll().size(), 1u);
        EXPECT_EQ(library.GetAll()[0].name.ToStringView(), "A");
        EXPECT_EQ(library.GetSprite("Other"_intern), nullptr);
    }

    TEST(TextureLibraryTest, MakeUniqueNameSkipsTakenNames) {
        TextureLibrary library;
        EXPECT_EQ(library.MakeUniqueName("new_sprite").ToStringView(), "new_sprite");

        library.AddOrUpdate(MakeEntry("new_sprite", "", Rect{}));
        EXPECT_EQ(library.MakeUniqueName("new_sprite").ToStringView(), "new_sprite_2");

        library.AddOrUpdate(MakeEntry("new_sprite_2", "", Rect{}));
        library.AddOrUpdate(MakeEntry("new_sprite_4", "", Rect{}));
        EXPECT_EQ(library.MakeUniqueName("new_sprite").ToStringView(), "new_sprite_3");
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

    namespace {

        /// A group member that is not packed
        SpriteEntry MakeGroupMember(const char* name, const char* path, Rect rect, const char* group) {
            SpriteEntry entry = MakeEntry(name, path, rect);
            SpriteAtlasLink link;
            link.name = PoolString::Intern(group);
            entry.atlas = link;
            return entry;
        }

        /// A packed sprite: its own path and rect are the atlas's, `source` is where the pixels came from
        SpriteEntry MakePacked(const char* name, const char* group, Rect placed, const char* sourcePath, Rect sourceRect) {
            SpriteEntry entry = MakeGroupMember(name, "assets/atlases/ui.png", placed, group);
            SpriteSource source;
            source.texturePath = PoolString::Intern(sourcePath);
            source.rect = sourceRect;
            entry.atlas->source = source;
            return entry;
        }

        String WriteToString(const TextureLibrary& library) {
            XmlSerializer serializer;
            library.Write(serializer);
            return serializer.SaveToString();
        }

        void ExpectSameRect(const Rect& a, const Rect& b) {
            EXPECT_FLOAT_EQ(a.x, b.x);
            EXPECT_FLOAT_EQ(a.y, b.y);
            EXPECT_FLOAT_EQ(a.w, b.w);
            EXPECT_FLOAT_EQ(a.h, b.h);
        }

    }  // namespace

    TEST(TextureLibraryTest, AFileWithoutAtlasesLoadsWithNoLinkAndWritesNone) {
        TextureLibrary library;
        Vector<DeserializeError> errors;
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, kAtlasFile, &errors));
        EXPECT_TRUE(errors.empty());
        for (const SpriteEntry& entry : library.GetAll()) {
            EXPECT_FALSE(entry.atlas.has_value()) << entry.name.ToStringView().data();
        }

        const String written = WriteToString(library);
        EXPECT_EQ(written.find("atlas"), String::npos) << written.c_str();
        EXPECT_EQ(written.find("source"), String::npos) << written.c_str();

        // The same text as for entries built by hand, which never had the field
        TextureLibrary byHand;
        byHand.AddOrUpdate(MakeEntry("RowOneFirst", "assets/sheet_test.png", Rect(0, 0, 116.67f, 190.5f)));
        byHand.AddOrUpdate(MakeEntry("RowOneSecond", "assets/sheet_test.png", Rect(116.67f, 0, 116.67f, 190.5f)));
        byHand.AddOrUpdate(MakeEntry("RowTwoFirst", "assets/sheet_test.png", Rect(0, 190.5f, 116.67f, 190.5f)));
        EXPECT_EQ(written, WriteToString(byHand));
    }

    TEST(TextureLibraryTest, AtlasLinksRoundTripInOrder) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("Plain", "assets/p.png", Rect(1, 2, 3, 4)));
        library.AddOrUpdate(MakeGroupMember("Member", "assets/m.png", Rect(5, 6, 7, 8), "ui"));
        library.AddOrUpdate(MakePacked("Packed", "ui", Rect(0, 8.5f, 12.25f, 9), "assets/artgen/a.png", Rect(116.67f, 190.5f, 12.25f, 9)));

        TextureLibrary copy;
        ASSERT_NO_FATAL_FAILURE(LoadInto(copy, WriteToString(library)));

        ASSERT_EQ(copy.GetAll().size(), 3u);
        EXPECT_EQ(copy.GetAll()[0].name.ToStringView(), "Plain");
        EXPECT_FALSE(copy.GetAll()[0].atlas.has_value());

        EXPECT_EQ(copy.GetAll()[1].name.ToStringView(), "Member");
        ASSERT_TRUE(copy.GetAll()[1].atlas.has_value());
        EXPECT_EQ(copy.GetAll()[1].atlas->name.ToStringView(), "ui");
        EXPECT_FALSE(copy.GetAll()[1].atlas->source.has_value());

        const SpriteEntry& packed = copy.GetAll()[2];
        EXPECT_EQ(packed.name.ToStringView(), "Packed");
        EXPECT_EQ(packed.texturePath.ToStringView(), "assets/atlases/ui.png");
        ExpectSameRect(packed.srcRect, Rect(0, 8.5f, 12.25f, 9));
        ASSERT_TRUE(packed.atlas.has_value());
        EXPECT_EQ(packed.atlas->name.ToStringView(), "ui");
        ASSERT_TRUE(packed.atlas->source.has_value());
        EXPECT_EQ(packed.atlas->source->texturePath.ToStringView(), "assets/artgen/a.png");
        ExpectSameRect(packed.atlas->source->rect, Rect(116.67f, 190.5f, 12.25f, 9));
    }

    TEST(TextureLibraryTest, WriteGivesTheShapeOfAnAtlasLink) {
        TextureLibrary library;
        library.AddOrUpdate(MakeGroupMember("Member", "assets/m.png", Rect(5, 6, 7, 8), "ui"));
        library.AddOrUpdate(MakePacked("Packed", "ui", Rect(0, 0, 2, 3), "assets/artgen/a.png", Rect(10, 20, 2, 3)));

        XmlSerializer serializer;
        library.Write(serializer);
        const pugi::xml_node sprites = serializer.GetRootNode().child("sprites");

        const pugi::xml_node member = sprites.first_child();
        ASSERT_TRUE(member);
        const pugi::xml_node memberAtlas = member.child("atlas");
        ASSERT_TRUE(memberAtlas);
        EXPECT_STREQ(memberAtlas.attribute("name").value(), "ui");
        EXPECT_FALSE(memberAtlas.child("source"));

        const pugi::xml_node packed = member.next_sibling();
        const pugi::xml_node packedAtlas = packed.child("atlas");
        ASSERT_TRUE(packedAtlas);
        EXPECT_STREQ(packedAtlas.attribute("name").value(), "ui");
        const pugi::xml_node source = packedAtlas.child("source");
        ASSERT_TRUE(source);
        EXPECT_STREQ(source.attribute("texturePath").value(), "assets/artgen/a.png");
        const pugi::xml_node rect = source.child("rect");
        ASSERT_TRUE(rect);
        EXPECT_FLOAT_EQ(rect.attribute("x").as_float(), 10.0f);
        EXPECT_FLOAT_EQ(rect.attribute("y").as_float(), 20.0f);
        EXPECT_FLOAT_EQ(rect.attribute("w").as_float(), 2.0f);
        EXPECT_FLOAT_EQ(rect.attribute("h").as_float(), 3.0f);
    }

    TEST(TextureLibraryTest, APackRecordRoundTripsBesideTheSource) {
        SpriteEntry packed = MakePacked("Packed", "ui", Rect(0, 0, 2, 3), "assets/artgen/a.png", Rect(10, 20, 2, 3));
        AtlasPackRecord record;
        record.padding = 3;
        record.sizeMode = "Tight"_intern;
        record.maxSide = 2048;
        packed.atlas->pack = record;
        SpriteEntry member = MakeGroupMember("Member", "assets/m.png", Rect(5, 6, 7, 8), "ui");
        member.atlas->pack = record;

        TextureLibrary library;
        library.AddOrUpdate(member);
        library.AddOrUpdate(packed);

        TextureLibrary copy;
        ASSERT_NO_FATAL_FAILURE(LoadInto(copy, WriteToString(library)));

        const SpriteEntry* readMember = copy.GetSprite("Member"_intern);
        ASSERT_NE(readMember, nullptr);
        ASSERT_TRUE(readMember->atlas.has_value());
        EXPECT_FALSE(readMember->atlas->source.has_value());
        ASSERT_TRUE(readMember->atlas->pack.has_value());
        EXPECT_EQ(readMember->atlas->pack->padding, 3);
        EXPECT_EQ(readMember->atlas->pack->sizeMode.ToStringView(), "Tight");
        EXPECT_EQ(readMember->atlas->pack->maxSide, 2048u);

        const SpriteEntry* readPacked = copy.GetSprite("Packed"_intern);
        ASSERT_NE(readPacked, nullptr);
        EXPECT_EQ(readPacked->atlas->name.ToStringView(), "ui");
        ASSERT_TRUE(readPacked->atlas->source.has_value());
        EXPECT_EQ(readPacked->atlas->source->texturePath.ToStringView(), "assets/artgen/a.png");
        ExpectSameRect(readPacked->atlas->source->rect, Rect(10, 20, 2, 3));
        ASSERT_TRUE(readPacked->atlas->pack.has_value());
        EXPECT_EQ(readPacked->atlas->pack->padding, 3);
        EXPECT_EQ(readPacked->atlas->pack->sizeMode.ToStringView(), "Tight");
        EXPECT_EQ(readPacked->atlas->pack->maxSide, 2048u);
    }

    TEST(TextureLibraryTest, AnAtlasWithoutAPackRecordWritesNoPackElement) {
        TextureLibrary library;
        library.AddOrUpdate(MakeGroupMember("Member", "assets/m.png", Rect(5, 6, 7, 8), "ui"));
        library.AddOrUpdate(MakePacked("Packed", "ui", Rect(0, 0, 2, 3), "assets/artgen/a.png", Rect(10, 20, 2, 3)));

        const String written = WriteToString(library);
        EXPECT_EQ(written.find("pack"), String::npos) << written.c_str();

        TextureLibrary copy;
        ASSERT_NO_FATAL_FAILURE(LoadInto(copy, written));
        for (const SpriteEntry& entry : copy.GetAll()) {
            ASSERT_TRUE(entry.atlas.has_value());
            EXPECT_FALSE(entry.atlas->pack.has_value());
        }
    }

    TEST(TextureLibraryTest, ASourceWithoutARectLoadsWithAZeroRect) {
        TextureLibrary library;
        Vector<DeserializeError> errors;
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, R"(<root><sprites>
            <sprite name="Odd" texturePath="a.png">
                <atlas name="ui"><source texturePath="b.png"/></atlas>
            </sprite>
        </sprites></root>)",
                                         &errors));

        const SpriteEntry* odd = library.GetSprite("Odd"_intern);
        ASSERT_NE(odd, nullptr);
        ASSERT_TRUE(odd->atlas.has_value());
        EXPECT_EQ(odd->atlas->name.ToStringView(), "ui");
        ASSERT_TRUE(odd->atlas->source.has_value());
        EXPECT_EQ(odd->atlas->source->texturePath.ToStringView(), "b.png");
        EXPECT_TRUE(odd->atlas->source->rect.IsEmpty());
    }

    TEST(TextureLibraryTest, AnAtlasWithoutANameIsReadAsTheEmptyGroup) {
        TextureLibrary library;
        ASSERT_NO_FATAL_FAILURE(LoadInto(library, R"(<root><sprites>
            <sprite name="Odd" texturePath="a.png"><atlas/></sprite>
        </sprites></root>)"));

        const SpriteEntry* odd = library.GetSprite("Odd"_intern);
        ASSERT_NE(odd, nullptr);
        ASSERT_TRUE(odd->atlas.has_value());
        EXPECT_TRUE(odd->atlas->name.Empty());
    }

    TEST(TextureLibraryTest, AddOrUpdateAndCopiesKeepTheLink) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("Sprite", "a.png", Rect(1, 2, 3, 4)));
        library.AddOrUpdate(MakePacked("Sprite", "ui", Rect(0, 0, 3, 4), "a.png", Rect(1, 2, 3, 4)));

        ASSERT_EQ(library.GetAll().size(), 1u);
        const SpriteEntry* stored = library.GetSprite("Sprite"_intern);
        ASSERT_NE(stored, nullptr);
        ASSERT_TRUE(stored->atlas.has_value());
        ASSERT_TRUE(stored->atlas->source.has_value());

        const SpriteEntry copy = *stored;
        ASSERT_TRUE(copy.atlas.has_value());
        EXPECT_EQ(copy.atlas->name.ToStringView(), "ui");
        ASSERT_TRUE(copy.atlas->source.has_value());
        EXPECT_EQ(copy.atlas->source->texturePath.ToStringView(), "a.png");
        ExpectSameRect(copy.atlas->source->rect, Rect(1, 2, 3, 4));
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
        EXPECT_EQ(library.GetSprite("Old"_intern), nullptr);

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

    TEST(TextureLibraryTest, CommentsInSpritesAreSkippedAndTheIndexCountsElements) {
        pugi::xml_document document;
        ASSERT_TRUE(document.load_string(R"(<?xml version="1.0"?>
<!-- header -->
<root>
  <sprites>
    <!-- first -->
    <sprite name="A" texturePath="a.png" />
    <!-- between -->
    <sprite name="B" texturePath="b.png" />
    <sprite texturePath="no-name.png" />
    <!-- last -->
  </sprites>
</root>)",
                                         pugi::parse_full));
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromXmlNode(XmlNode(document.document_element())));
        deserializer.SetDiagnostics(DiagnosticsMode::Error);
        TextureLibrary library;

        library.Load(deserializer);
        deserializer.FinishLoad();

        EXPECT_FALSE(deserializer.HasErrors());
        ASSERT_EQ(library.GetAll().size(), 2u);
        EXPECT_EQ(library.GetAll()[0].name.ToStringView(), "A");
        EXPECT_EQ(library.GetAll()[1].name.ToStringView(), "B");
    }

    TEST(TextureLibraryTest, ABadSpriteNamesItsIndexAmongTheElements) {
        pugi::xml_document document;
        ASSERT_TRUE(
            document.load_string(R"(<root><sprites><!-- c --><sprite name="A"/><!-- c --><sprite name="B"/><!-- c --><sprite name="C"><srcRect x="x"/></sprite></sprites></root>)", pugi::parse_full));
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromXmlNode(XmlNode(document.document_element())));
        deserializer.SetStrictValues(true);
        TextureLibrary library;

        library.Load(deserializer);

        ASSERT_EQ(deserializer.GetErrors().size(), 1u);
        EXPECT_NE(deserializer.GetErrors()[0].path.find("sprites[2]"), String::npos) << deserializer.GetErrors()[0].path.c_str();
    }

    namespace {

        /// Canonical (what XmlDocument writes), with a `groups` element after `sprites` that a replace must leave where it is
        constexpr StringView kWithNeighbours = R"(<?xml version="1.0"?>
<!-- header -->
<root>
  <sprites>
    <sprite name="WhiteKing" texturePath="assets/chess_test.png">
      <srcRect x="0" y="0" w="116.67" h="190.5" />
    </sprite>
    <sprite name="WhiteQueen" texturePath="assets/chess_test.png">
      <srcRect x="116.67" y="0" w="116.67" h="190.5" />
    </sprite>
  </sprites>
  <groups />
</root>
)";

        constexpr StringView kKingLines = R"(    <sprite name="WhiteKing" texturePath="assets/chess_test.png">
      <srcRect x="0" y="0" w="116.67" h="190.5" />
    </sprite>
)";

        constexpr StringView kQueenLines = R"(    <sprite name="WhiteQueen" texturePath="assets/chess_test.png">
      <srcRect x="116.67" y="0" w="116.67" h="190.5" />
    </sprite>
)";

        constexpr StringView kRookLines = R"(    <sprite name="WhiteRook" texturePath="assets/chess_test.png">
      <srcRect x="233" y="0" w="116" h="190" />
    </sprite>
)";

        /// The file is loaded into an XmlDocument, the library is read from its root, @p edit changes the library, WriteInto and a
        /// save to a temporary file give the text that is returned
        template <typename Edit>
        String SavedAfter(StringView fileText, Edit edit) {
            XmlDocument document;
            EXPECT_TRUE(document.LoadFromString(fileText));
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromXmlNode(document.GetRoot()));
            TextureLibrary library;
            library.Load(deserializer);

            edit(library);
            EXPECT_TRUE(library.WriteInto(document.GetRoot().GetPugiNode()));

            const std::filesystem::path path = TestTempPath("TextureLibraryTest_Saved.xml");
            EXPECT_TRUE(document.SaveToFile(path));
            String bytes = ReadFile(path);
            std::error_code error;
            std::filesystem::remove(path, error);
            return bytes;
        }

        /// @p text with the first @p from replaced by @p to
        String Replaced(StringView text, StringView from, StringView to) {
            String result(text);
            const size_t at = result.find(String(from));
            EXPECT_NE(at, String::npos) << from.data();
            if (at != String::npos) {
                result.replace(at, from.size(), String(to));
            }
            return result;
        }

    }  // namespace

    TEST(TextureLibraryTest, SavingTheLibraryAsLoadedChangesNothingInTheFile) {
        EXPECT_EQ(SavedAfter(kWithNeighbours, [](TextureLibrary&) {}), String(kWithNeighbours));
    }

    TEST(TextureLibraryTest, AnAcceptedSpriteAddsItsLinesAndNothingElse) {
        const String saved = SavedAfter(kWithNeighbours, [](TextureLibrary& library) { library.AddOrUpdate(MakeEntry("WhiteRook", "assets/chess_test.png", Rect(233, 0, 116, 190))); });

        EXPECT_EQ(saved, Replaced(kWithNeighbours, "  </sprites>\n", String("") + String(kRookLines) + "  </sprites>\n"));
    }

    TEST(TextureLibraryTest, AnUpdatedSpriteChangesItsOwnLinesOnly) {
        const String saved = SavedAfter(kWithNeighbours, [](TextureLibrary& library) { library.AddOrUpdate(MakeEntry("WhiteQueen", "assets/chess_test.png", Rect(233, 0, 116, 190))); });

        EXPECT_EQ(saved, Replaced(kWithNeighbours, kQueenLines, Replaced(kRookLines, "WhiteRook", "WhiteQueen")));
    }

    TEST(TextureLibraryTest, ARemovedSpriteTakesOnlyItsLines) {
        const String saved = SavedAfter(kWithNeighbours, [](TextureLibrary& library) { library.Remove("WhiteKing"_intern); });

        EXPECT_EQ(saved, Replaced(kWithNeighbours, kKingLines, ""));
    }

    TEST(TextureLibraryTest, ARenamedSpriteChangesTheNameLineOnly) {
        const String saved = SavedAfter(kWithNeighbours, [](TextureLibrary& library) { EXPECT_TRUE(library.Rename("WhiteKing"_intern, "BlackKing"_intern)); });

        EXPECT_EQ(saved, Replaced(kWithNeighbours, "name=\"WhiteKing\"", "name=\"BlackKing\""));
    }

    TEST(TextureLibraryTest, SpritesStayBeforeTheElementsThatFollowThem) {
        const String saved = SavedAfter(kWithNeighbours, [](TextureLibrary& library) { library.Remove("WhiteKing"_intern); });

        EXPECT_LT(saved.find("</sprites>"), saved.find("<groups />"));
        EXPECT_LT(saved.find("<!-- header -->"), saved.find("<root>"));
    }

    TEST(TextureLibraryTest, AFileWithoutSpritesGetsThemAfterItsOtherChildren) {
        constexpr StringView noSprites = R"(<root>
  <groups />
</root>
)";
        const String saved = SavedAfter(noSprites, [](TextureLibrary& library) { library.AddOrUpdate(MakeEntry("WhiteRook", "assets/chess_test.png", Rect(233, 0, 116, 190))); });

        EXPECT_EQ(saved, String("<root>\n  <groups />\n  <sprites>\n") + String(kRookLines) + "  </sprites>\n</root>\n");
    }

    namespace {

        /// A file as a person lays it out: a blank line between the rows
        constexpr StringView kHandLaidFile = R"(<?xml version="1.0"?>
<!-- header -->
<root>
  <sprites>
    <sprite name="WhiteKing" texturePath="assets/chess_test.png">
      <srcRect x="0" y="0" w="116.67" h="190.5" />
    </sprite>

    <sprite name="WhiteQueen" texturePath="assets/chess_test.png">
      <srcRect x="116.67" y="0" w="116.67" h="190.5" />
    </sprite>
  </sprites>
  <groups />
</root>
)";

        /// The Accept of the Generate panel on a file on disk: load, edit the library, WriteInto, SaveToFile over the same file
        template <typename Edit>
        String AcceptedOnDisk(Edit edit) {
            const std::filesystem::path path = TestTempPath("TextureLibraryTest_HandLaid.xml");
            WriteFile(path, kHandLaidFile);

            XmlDocument document;
            EXPECT_TRUE(document.LoadFromFile(path));
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromXmlNode(document.GetRoot()));
            TextureLibrary library;
            library.Load(deserializer);
            edit(library);
            EXPECT_TRUE(library.WriteInto(document.GetRoot().GetPugiNode()));
            EXPECT_TRUE(document.SaveToFile(path));

            String bytes = ReadFile(path);
            std::error_code error;
            std::filesystem::remove(path, error);
            return bytes;
        }

    }  // namespace

    TEST(TextureLibraryTest, AnAcceptOnAHandLaidFileChangesTheSpriteLinesAndNothingElse) {
        const String saved = AcceptedOnDisk([](TextureLibrary& library) { library.AddOrUpdate(MakeEntry("WhiteRook", "assets/chess_test.png", Rect(233, 0, 116, 190))); });

        EXPECT_EQ(saved, Replaced(kHandLaidFile, "  </sprites>\n", String(kRookLines) + "  </sprites>\n"));
    }

    TEST(TextureLibraryTest, AnUpdateOnAHandLaidFileKeepsTheBlankLineBetweenTheRows) {
        const String saved = AcceptedOnDisk([](TextureLibrary& library) { library.AddOrUpdate(MakeEntry("WhiteQueen", "assets/chess_test.png", Rect(233, 0, 116, 190))); });

        EXPECT_EQ(saved, Replaced(kHandLaidFile, kQueenLines, Replaced(kRookLines, "WhiteRook", "WhiteQueen")));
    }

    TEST(TextureLibraryTest, ARemoveOnAHandLaidFileTakesTheLinesOfTheRowOnly) {
        const String saved = AcceptedOnDisk([](TextureLibrary& library) { library.Remove("WhiteKing"_intern); });

        EXPECT_EQ(saved, Replaced(kHandLaidFile, kKingLines, ""));
    }

    TEST(TextureLibraryTest, WriteIntoAnInvalidNodeIsRefused) {
        TextureLibrary library;
        EXPECT_FALSE(library.WriteInto(pugi::xml_node()));
    }

}  // namespace BECore::Tests
