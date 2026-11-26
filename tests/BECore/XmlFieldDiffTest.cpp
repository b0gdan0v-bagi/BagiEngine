#include <BECore/Config/XmlNode.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlFieldDiff.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Hand-written Serialize / Deserialize, shaped as the generated ones

        struct DiffStats {
            int32_t attack = 1;
            int32_t defence = 2;

            void Serialize(ISerializer& s) const {
                Ser::Save(s, attack, "attack");
                Ser::Save(s, defence, "defence");
            }
            void Deserialize(IDeserializer& d) {
                Ser::Load(d, attack, "attack");
                Ser::Load(d, defence, "defence");
            }
        };

        struct DiffUnit {
            int32_t health = 100;
            DiffStats stats{10, 20};  // not the defaults of DiffStats itself
            Vector<int32_t> slots{1, 2};

            void Serialize(ISerializer& s) const {
                Ser::Save(s, health, "health");
                Ser::Save(s, stats, "stats");
                Ser::Save(s, slots, "slots");
            }
            void Deserialize(IDeserializer& d) {
                Ser::Load(d, health, "health");
                Ser::Load(d, stats, "stats");
                Ser::Load(d, slots, "slots");
            }
        };

        /// Leaves an empty list out of what it writes, so only its default object has the field
        struct DiffTerseList {
            Vector<int32_t> slots{1, 2};

            void Serialize(ISerializer& s) const {
                if (!slots.empty()) {
                    Ser::Save(s, slots, "slots");
                }
            }
            void Deserialize(IDeserializer& d) {
                Ser::Load(d, slots, "slots");
            }
        };

        struct DiffShape {
            virtual ~DiffShape() = default;
            int32_t size = 3;

            virtual void Serialize(ISerializer& s) const {
                Ser::Save(s, size, "size");
            }
        };

        struct DiffCircle : DiffShape {
            int32_t segments = 16;

            void Serialize(ISerializer& s) const override {
                DiffShape::Serialize(s);
                Ser::Save(s, segments, "segments");
            }
        };

        /// What AppendChangedFields writes for @p object against @p base, as the compact text of the target element
        template <typename T>
        String Changes(const T& object, const T& base, pugi::xml_document& document) {
            document.reset();
            XmlFieldDiff::AppendChangedFields(object, base, document.append_child("object"));
            return XmlFieldDiff::CompactText(document.document_element());
        }

        /// Reads @p fields into a default object, as a loader does
        template <typename T>
        T Loaded(pugi::xml_node fields) {
            T object;
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromXmlNode(XmlNode{fields}));
            object.Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            return object;
        }

        struct DiffPart {
            int32_t count = 1;
            int32_t weight = 5;

            void Serialize(ISerializer& s) const {
                Ser::Save(s, count, "count");
                Ser::Save(s, weight, "weight");
            }
            void Deserialize(IDeserializer& d) {
                Ser::Load(d, count, "count");
                Ser::Load(d, weight, "weight");
            }
        };

        /// Two types of one member whose defaults differ
        template <int32_t Default>
        struct DiffPick {
            int32_t number = Default;

            void Serialize(ISerializer& s) const {
                Ser::Save(s, number, "number");
            }
            void Deserialize(IDeserializer& d) {
                Ser::Load(d, number, "number");
            }
        };

        struct DiffBag {
            int32_t size = 3;
            Optional<int32_t> limit;
            DiffStats stats;
            Vector<DiffPart> parts;
            HashMap<int32_t, int32_t> prices;
            Vector<Variant<DiffPick<1>, DiffPick<2>>> picks;

            void Serialize(ISerializer& s) const {
                Ser::Save(s, size, "size");
                Ser::Save(s, limit, "limit");
                Ser::Save(s, stats, "stats");
                Ser::Save(s, parts, "parts");
                Ser::Save(s, prices, "prices");
                Ser::Save(s, picks, "picks");
            }
            void Deserialize(IDeserializer& d) {
                Ser::Load(d, size, "size");
                Ser::Load(d, limit, "limit");
                Ser::Load(d, stats, "stats");
                Ser::Load(d, parts, "parts");
                Ser::Load(d, prices, "prices");
                Ser::Load(d, picks, "picks");
            }
        };

        /// A file as a person writes it: comments, fields left to their defaults, elements named after the domain, a key nothing reads
        constexpr const char* kBagXml = R"(<bag note="mine" limit="9">
  <!-- first -->
  <stats attack="4"/>
  <parts>
    <!-- the heavy one -->
    <part weight="8" tag="a"/>
    <part count="2"/>
  </parts>
  <prices><entry key="2" value="20"/><entry key="1" value="10"/></prices>
</bag>)";

        /// An object loaded from a file, the file's element and what the object wrote right after the load
        struct DiffFile {
            pugi::xml_document document;
            DiffBag object;
            XmlSerializer base;

            explicit DiffFile(const char* text = kBagXml) {
                EXPECT_TRUE(document.load_string(text, pugi::parse_default | pugi::parse_comments));
                object = Loaded<DiffBag>(Element());
                object.Serialize(base);
            }

            pugi::xml_node Element() {
                return document.document_element();
            }

            String Text() {
                return XmlFieldDiff::CompactText(Element());
            }

            void Patch() {
                XmlSerializer current;
                object.Serialize(current);
                XmlFieldDiff::PatchChangedFields(current.GetRootNode(), base.GetRootNode(), Element());
            }

            /// What the patched element loads as, written in full
            String Reloaded() {
                return Written(Loaded<DiffBag>(Element()));
            }

            static String Written(const DiffBag& bag) {
                XmlSerializer text;
                bag.Serialize(text);
                return text.SaveToString();
            }
        };

        /// @p text with its one occurrence of @p from replaced by @p to
        String With(String text, const char* from, const char* to) {
            const size_t at = text.find(from);
            EXPECT_NE(at, String::npos) << from;
            if (at != String::npos) {
                text.replace(at, String(from).size(), to);
            }
            return text;
        }

    }  // namespace

    TEST(XmlFieldDiffTest, APatchOfAnUnchangedObjectLeavesTheElementAsItWas) {
        DiffFile file;
        const String before = file.Text();

        file.Patch();

        EXPECT_EQ(file.Text(), before);
    }

    TEST(XmlFieldDiffTest, APatchTouchesTheChangedAttributeOnly) {
        DiffFile file;
        const String before = file.Text();
        file.object.limit = 12;
        file.object.size = 7;

        file.Patch();

        // An attribute keeps its place, one the file lacked goes after the others
        EXPECT_EQ(file.Text(), With(before, R"(limit="9")", R"(limit="12" size="7")"));
    }

    TEST(XmlFieldDiffTest, APatchRemovesAFieldTheObjectNoLongerWrites) {
        DiffFile file;
        file.object.limit.reset();

        file.Patch();

        EXPECT_FALSE(file.Element().attribute("limit"));
        EXPECT_EQ(file.Reloaded(), DiffFile::Written(file.object));
    }

    TEST(XmlFieldDiffTest, APatchReplacesAChangedNestedObjectWholeWhereItStands) {
        DiffFile file;
        const String before = file.Text();
        file.object.stats.defence = 6;

        file.Patch();

        EXPECT_EQ(file.Text(), With(before, R"(<stats attack="4"/>)", R"(<stats attack="4" defence="6"/>)"));
    }

    TEST(XmlFieldDiffTest, APatchRemovesTheOtherSpellingOfAChangedField) {
        DiffFile file(R"(<bag size="5"><stats attack="4"/></bag>)");
        file.Element().append_child("size");
        file.Element().append_attribute("stats") = "old";
        file.object.size = 6;
        file.object.stats.attack = 7;

        file.Patch();

        EXPECT_FALSE(file.Element().child("size"));
        EXPECT_FALSE(file.Element().attribute("stats"));
        EXPECT_EQ(file.Reloaded(), DiffFile::Written(file.object));
    }

    TEST(XmlFieldDiffTest, APatchOfOneListElementKeepsTheRestOfTheList) {
        DiffFile file;
        const String before = file.Text();
        file.object.parts[1].weight = 9;

        file.Patch();

        EXPECT_EQ(file.Text(), With(before, R"(<part count="2"/>)", R"(<part count="2" weight="9"/>)"));
        EXPECT_EQ(file.Reloaded(), DiffFile::Written(file.object));
    }

    TEST(XmlFieldDiffTest, APatchWritesAResizedListAnewUnderTheNameOfItsElements) {
        DiffFile file;
        file.object.parts.push_back(DiffPart{7, 7});

        file.Patch();

        int32_t parts = 0;
        for (const pugi::xml_node part : file.Element().child("parts").children()) {
            EXPECT_STREQ(part.name(), "part");
            ++parts;
        }
        EXPECT_EQ(parts, 3);
        EXPECT_STREQ(file.Element().attribute("note").value(), "mine");
        EXPECT_EQ(file.Reloaded(), DiffFile::Written(file.object));
    }

    TEST(XmlFieldDiffTest, APatchOfAMapWhoseFileOrderDiffersChangesTheRightEntry) {
        DiffFile file;
        file.object.prices[1] = 11;

        file.Patch();

        EXPECT_EQ(file.Reloaded(), DiffFile::Written(file.object));
    }

    TEST(XmlFieldDiffTest, APatchReplacesAListElementWhoseTypeChanged) {
        DiffFile file(R"(<bag><picks><pick index="0"/><pick index="0"/></picks></bag>)");
        // The value reads the same in both types, so a patch of the differing attributes would leave the file to the other default
        file.object.picks[0] = DiffPick<2>{1};

        file.Patch();

        EXPECT_EQ(file.Element().child("picks").first_child().attribute("index").as_int(), 1);
        EXPECT_STREQ(file.Element().child("picks").first_child().name(), "pick");
        EXPECT_EQ(file.Reloaded(), DiffFile::Written(file.object));
    }

    TEST(XmlFieldDiffTest, AnUntouchedObjectWritesNothing) {
        pugi::xml_document document;

        EXPECT_EQ(Changes(DiffUnit{}, DiffUnit{}, document), "<object/>");
    }

    TEST(XmlFieldDiffTest, AValueSetToZeroUnderAnotherDefaultIsWrittenAndLoadsBack) {
        DiffUnit unit;
        unit.health = 0;
        pugi::xml_document document;

        EXPECT_EQ(Changes(unit, DiffUnit{}, document), R"(<object health="0"/>)");
        EXPECT_EQ(Loaded<DiffUnit>(document.document_element()).health, 0);
    }

    TEST(XmlFieldDiffTest, ANestedObjectWithOneChangedMemberIsWrittenWhole) {
        DiffUnit unit;
        // The default of DiffStats, not of the owner: pruned against the former it would load as 10
        unit.stats.attack = 1;
        pugi::xml_document document;

        EXPECT_EQ(Changes(unit, DiffUnit{}, document), R"(<object><stats attack="1" defence="20"/></object>)");
        const DiffUnit loaded = Loaded<DiffUnit>(document.document_element());
        EXPECT_EQ(loaded.stats.attack, 1);
        EXPECT_EQ(loaded.stats.defence, 20);
    }

    TEST(XmlFieldDiffTest, AnEmptiedListThatTheDefaultFillsLoadsBackEmpty) {
        DiffUnit unit;
        unit.slots.clear();
        pugi::xml_document document;

        EXPECT_EQ(Changes(unit, DiffUnit{}, document), "<object><slots/></object>");
        EXPECT_TRUE(Loaded<DiffUnit>(document.document_element()).slots.empty());
    }

    TEST(XmlFieldDiffTest, AFieldOnlyTheBaseHasIsOneEmptyElement) {
        DiffTerseList list;
        list.slots.clear();
        pugi::xml_document document;

        EXPECT_EQ(Changes(list, DiffTerseList{}, document), "<object><slots/></object>");
        EXPECT_TRUE(Loaded<DiffTerseList>(document.document_element()).slots.empty());
    }

    TEST(XmlFieldDiffTest, ADerivedTypeIsComparedThroughABaseReference) {
        DiffCircle circle;
        circle.segments = 32;
        const DiffCircle base;
        pugi::xml_document document;

        EXPECT_EQ(Changes<DiffShape>(circle, base, document), R"(<object segments="32"/>)");
    }

}  // namespace BECore::Tests
