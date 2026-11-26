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

    }  // namespace

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
