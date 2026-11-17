#include <BECore/Config/XmlNode.h>
#include <BECore/Config/XmlTextPatch.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIElementStyle.h>
#include <BECore/UI/UIPanel.h>
#include <BECore/UI/UIRectFilePatch.h>
#include <BECore/UI/UIStack.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Two canvases in one node; the first has a nested tree and a stack, the second one element with a long attribute list
        constexpr const char* kNodeFile = R"(<?xml version="1.0"?>
<!-- a node file -->
<node name="Hud" layer="10">
  <components>
    <component type="TransformComponent" x="0" y="0" width="10" height="10"/>
    <component type="UICanvasComponent" space="Screen">
      <elements>
        <element type="UIPanel" name="Top" anchor="Top" y="8" width="100" height="20" color="1,2,3,255">
          <children>
            <element type="UIElement" name="Inner" x="5" y="6" width="40" height="10" fontSize="14"/>
            <element type="UIElement" name="Deep">
              <children>
                <element type="UIElement" name="Deeper" anchor="Stretch" margin="2"/>
              </children>
            </element>
          </children>
        </element>
        <element type="UIStack" name="Column" direction="Horizontal" spacing="4" x="10" visible="false">
          <children>
            <element type="UIElement" name="Cell" width="30" height="30" flex="1"/>
          </children>
        </element>
      </elements>
    </component>
    <component type="UICanvasComponent" space="Screen">
      <elements>
        <element type="UIElement" name="Lone"
                 anchor="BottomRight" x="-10" y="-10" width="50%" height="20" color="9,9,9,255"/>
      </elements>
    </component>
  </components>
</node>)";

        constexpr float kStep = 0.25f;

        class UIRectFilePatchTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void Load(const char* text = kNodeFile) {
                _text = text;
                String error;
                ASSERT_TRUE(XmlTextPatch::Parse(_text, _doc, error)) << error.c_str();
            }

            [[nodiscard]] pugi::xml_node NodeEl() const {
                return _doc.child("node");
            }

            /// The live canvas made from the @p ordinal-th canvas of the file, as the scene loader reads it
            IntrusivePtr<UICanvasComponent> MakeCanvas(uint32_t ordinal) {
                uint32_t seen = 0;
                for (pugi::xml_node component : NodeEl().child("components").children("component")) {
                    if (StringView{component.attribute("type").value()} == "UICanvasComponent" && seen++ == ordinal) {
                        XmlDeserializer deserializer;
                        EXPECT_TRUE(deserializer.LoadFromXmlNode(XmlNode{component}));
                        auto canvas = New<UICanvasComponent>();
                        canvas->Deserialize(deserializer);
                        EXPECT_FALSE(deserializer.HasErrors());
                        canvas->Initialize();
                        return canvas;
                    }
                }
                return {};
            }

            pugi::xml_document _doc;
            String _text;
        };

        UIElement& Child(UIElement& parent, size_t index) {
            return *parent.children.at(index);
        }

        UIRectSpec ReadRect(pugi::xml_node element) {
            XmlDeserializer deserializer;
            UIElementStyle style;
            EXPECT_TRUE(deserializer.LoadFromXmlNode(XmlNode{element}));
            style.Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            return style.rect;
        }

        pugi::xml_node FindByName(const pugi::xml_document& doc, StringView name) {
            pugi::xml_node found;
            const auto visit = [&](auto&& self, pugi::xml_node node) -> void {
                for (pugi::xml_node child : node.children()) {
                    if (child.type() == pugi::node_element && StringView{child.attribute("name").value()} == name && StringView{child.name()} == "element") {
                        found = child;
                    }
                    self(self, child);
                }
            };
            visit(visit, doc);
            return found;
        }

        struct Patched {
            bool ok = false;
            String text;
            String error;
            Vector<XmlTextPatch::AttributeEdit> edits;
        };

        /// The edits for the file element called @p name to become @p live, applied to @p text
        Patched PatchRect(const String& text, const pugi::xml_document& doc, StringView name, const UIRectSpec& live) {
            Patched result;
            const pugi::xml_node element = FindByName(doc, name);
            result.edits = UIRectFilePatch::RectAttributeEdits(element, live);
            const Vector<XmlTextPatch::ElementEdit> edits = {{element, result.edits}};
            result.ok = XmlTextPatch::Apply(text, edits, result.text, result.error);
            return result;
        }

        bool AttributeNamed(const Vector<XmlTextPatch::AttributeEdit>& edits, StringView name, StringView value) {
            for (const auto& edit : edits) {
                if (edit.name == name) {
                    return edit.value && *edit.value == value;
                }
            }
            return false;
        }

        bool Removes(const Vector<XmlTextPatch::AttributeEdit>& edits, StringView name) {
            for (const auto& edit : edits) {
                if (edit.name == name) {
                    return !edit.value;
                }
            }
            return false;
        }

        class Random {
        public:
            explicit Random(uint32_t seed) : _state(seed) {}

            uint32_t Next() {
                _state ^= _state << 13;
                _state ^= _state >> 17;
                _state ^= _state << 5;
                return _state;
            }

            size_t Below(size_t bound) {
                return Next() % bound;
            }

            bool Chance() {
                return Below(2) == 0;
            }

            float Step(size_t steps) {
                return static_cast<float>(Below(steps)) * kStep;
            }

            UILength Length() {
                return Chance() ? UILength::Px(Step(400) - 20.0f) : UILength::Percent(Step(400) - 20.0f);
            }

            Vec2 Fraction() {
                return {Step(5), Step(5)};
            }

        private:
            uint32_t _state;
        };

        UIRectSpec RandomRect(Random& random) {
            UIRectSpec rect;
            rect.anchor = static_cast<UIAnchor>(random.Below(10));
            if (random.Chance()) {
                rect.anchorMin = random.Fraction();
            }
            if (random.Chance()) {
                rect.anchorMax = random.Fraction();
            }
            if (random.Chance()) {
                rect.pivot = random.Fraction();
            }
            rect.x = random.Chance() ? UILength{} : random.Length();
            rect.y = random.Chance() ? UILength{} : random.Length();
            rect.width = random.Chance() ? UILength{} : random.Length();
            rect.height = random.Chance() ? UILength{} : random.Length();
            if (random.Chance()) {
                rect.minWidth = random.Length();
            }
            if (random.Chance()) {
                rect.maxWidth = random.Length();
            }
            if (random.Chance()) {
                rect.minHeight = random.Length();
            }
            if (random.Chance()) {
                rect.maxHeight = random.Length();
            }
            rect.aspectRatio = random.Chance() ? 0.0f : random.Step(20);
            rect.aspectMode = random.Chance() ? UIAspectMode::Fit : UIAspectMode::Fill;
            if (random.Chance()) {
                rect.margin = {random.Step(40), random.Step(40), random.Step(40), random.Step(40)};
            }
            rect.flex = random.Chance() ? 0.0f : random.Step(12);
            if (random.Chance()) {
                rect.region = PoolString::Intern(random.Chance() ? "board" : "side");
            }
            return rect;
        }

        /// The attributes of @p element that are not part of the rect, as "name=value" in file order
        Vector<String> OtherAttributes(pugi::xml_node element) {
            constexpr Array<StringView, 17> kRectNames = {"anchor",   "anchorMin", "anchorMax", "pivot",       "x",          "y",      "width", "height", "minWidth",
                                                          "maxWidth", "minHeight", "maxHeight", "aspectRatio", "aspectMode", "margin", "flex",  "region"};
            Vector<String> others;
            for (pugi::xml_attribute attribute : element.attributes()) {
                bool isRect = false;
                for (StringView name : kRectNames) {
                    isRect = isRect || name == attribute.name();
                }
                if (!isRect) {
                    others.push_back(String(attribute.name()) + "=" + attribute.value());
                }
            }
            return others;
        }

    }  // namespace

    // ---- FindAuthoredPath -------------------------------------------------------------------------------------------------

    TEST_F(UIRectFilePatchTest, ThePathOfATopElementIsItsIndex) {
        Load();
        const auto canvas = MakeCanvas(0);
        ASSERT_TRUE(canvas);
        const auto path = UIRectFilePatch::FindAuthoredPath(*canvas, *canvas->_elements[1]);
        ASSERT_TRUE(path);
        EXPECT_EQ(*path, (Vector<uint32_t>{1}));
    }

    TEST_F(UIRectFilePatchTest, ThePathOfANestedElementFollowsTheChildren) {
        Load();
        const auto canvas = MakeCanvas(0);
        UIElement& deeper = Child(Child(*canvas->_elements[0], 1), 0);
        EXPECT_EQ(deeper.name.ToStringView(), "Deeper");
        const auto path = UIRectFilePatch::FindAuthoredPath(*canvas, deeper);
        ASSERT_TRUE(path);
        EXPECT_EQ(*path, (Vector<uint32_t>{0, 1, 0}));
    }

    TEST_F(UIRectFilePatchTest, ThePathOfAChildOfAStackIsFoundToo) {
        Load();
        const auto canvas = MakeCanvas(0);
        const auto path = UIRectFilePatch::FindAuthoredPath(*canvas, Child(*canvas->_elements[1], 0));
        ASSERT_TRUE(path);
        EXPECT_EQ(*path, (Vector<uint32_t>{1, 0}));
    }

    TEST_F(UIRectFilePatchTest, AnElementAddedInCodeAndTheRootHaveNoPath) {
        Load();
        const auto canvas = MakeCanvas(0);
        auto added = New<UIElement>();
        canvas->GetRoot().children.push_back(added);
        EXPECT_FALSE(UIRectFilePatch::FindAuthoredPath(*canvas, *added));
        EXPECT_FALSE(UIRectFilePatch::FindAuthoredPath(*canvas, canvas->GetRoot()));

        auto addedBelow = New<UIElement>();
        canvas->_elements[0]->children.push_back(addedBelow);
        const auto path = UIRectFilePatch::FindAuthoredPath(*canvas, *addedBelow);
        ASSERT_TRUE(path);
        EXPECT_EQ(*path, (Vector<uint32_t>{0, 2}));  // below an authored element it is found, in the list the code appended to
    }

    // ---- FindFileElement --------------------------------------------------------------------------------------------------

    TEST_F(UIRectFilePatchTest, EveryAuthoredElementFindsItsFileElement) {
        Load();
        for (uint32_t ordinal = 0; ordinal < 2; ++ordinal) {
            const auto canvas = MakeCanvas(ordinal);
            ASSERT_TRUE(canvas);
            const auto visit = [&](auto&& self, UIElement& element) -> void {
                const auto path = UIRectFilePatch::FindAuthoredPath(*canvas, element);
                ASSERT_TRUE(path) << String(element.name.ToStringView()).c_str();
                String why;
                const pugi::xml_node found = UIRectFilePatch::FindFileElement(NodeEl(), ordinal, *path, element, why);
                ASSERT_TRUE(found) << String(element.name.ToStringView()).c_str() << ": " << why.c_str();
                EXPECT_EQ(StringView{found.attribute("name").value()}, element.name.ToStringView());
                for (const auto& child : element.children) {
                    self(self, *child);
                }
            };
            for (const auto& element : canvas->_elements) {
                visit(visit, *element);
            }
        }
    }

    TEST_F(UIRectFilePatchTest, TheSecondCanvasIsFoundByItsOrdinal) {
        Load();
        const auto canvas = MakeCanvas(1);
        String why;
        const pugi::xml_node found = UIRectFilePatch::FindFileElement(NodeEl(), 1, Vector<uint32_t>{0}, *canvas->_elements[0], why);
        ASSERT_TRUE(found) << why.c_str();
        EXPECT_STREQ(found.attribute("name").value(), "Lone");

        EXPECT_FALSE(UIRectFilePatch::FindFileElement(NodeEl(), 0, Vector<uint32_t>{0}, *canvas->_elements[0], why));
        EXPECT_FALSE(why.empty());
    }

    TEST_F(UIRectFilePatchTest, ACanvasPastTheEndIsNotFound) {
        Load();
        const auto canvas = MakeCanvas(0);
        String why;
        EXPECT_FALSE(UIRectFilePatch::FindFileElement(NodeEl(), 2, Vector<uint32_t>{0}, *canvas->_elements[0], why));
        EXPECT_NE(why.find("canvas"), String::npos);
    }

    TEST_F(UIRectFilePatchTest, APathPastTheEndOrAnEmptyOneIsNotFound) {
        Load();
        const auto canvas = MakeCanvas(0);
        String why;
        EXPECT_FALSE(UIRectFilePatch::FindFileElement(NodeEl(), 0, Vector<uint32_t>{5}, *canvas->_elements[0], why));
        EXPECT_FALSE(UIRectFilePatch::FindFileElement(NodeEl(), 0, Vector<uint32_t>{0, 7}, *canvas->_elements[0], why));
        EXPECT_FALSE(UIRectFilePatch::FindFileElement(NodeEl(), 0, Vector<uint32_t>{}, *canvas->_elements[0], why));
    }

    TEST_F(UIRectFilePatchTest, ATypeOrANameThatDiffersIsNotTheSameElement) {
        Load();
        const auto canvas = MakeCanvas(0);
        UIElement& top = *canvas->_elements[0];
        String why;

        top.name = "Renamed"_intern;
        EXPECT_FALSE(UIRectFilePatch::FindFileElement(NodeEl(), 0, Vector<uint32_t>{0}, top, why));
        EXPECT_NE(why.find("Renamed"), String::npos);

        // The second element of the file is a UIStack: a panel at its place is another element
        why.clear();
        EXPECT_FALSE(UIRectFilePatch::FindFileElement(NodeEl(), 0, Vector<uint32_t>{1}, *New<UIPanel>(), why));
        EXPECT_FALSE(why.empty());
    }

    // ---- RectAttributeEdits -----------------------------------------------------------------------------------------------

    TEST_F(UIRectFilePatchTest, AnUnchangedRectIsNoEdit) {
        Load();
        const auto canvas = MakeCanvas(0);
        for (StringView name : {"Top", "Inner", "Deeper", "Column", "Cell"}) {
            const pugi::xml_node element = FindByName(_doc, name);
            EXPECT_TRUE(UIRectFilePatch::RectAttributeEdits(element, ReadRect(element)).empty()) << String(name).c_str();
        }
    }

    TEST_F(UIRectFilePatchTest, ASpellingThatParsesEqualIsLeftAlone) {
        Load(R"(<node><components><component type="UICanvasComponent"><elements>
  <element type="UIElement" name="A" x="10.0" anchor="TopLeft" y="0" width="5e1" anchorMin="10%,0" flex="0"/>
</elements></component></components></node>)");
        UIRectSpec live;
        live.x = UILength::Px(10.0f);
        live.width = UILength::Px(50.0f);
        live.anchorMin = Vec2{0.1f, 0.0f};
        const auto edits = UIRectFilePatch::RectAttributeEdits(FindByName(_doc, "A"), live);
        EXPECT_TRUE(edits.empty());
    }

    TEST_F(UIRectFilePatchTest, ANumberThatChangedIsSetInTheWritersSpelling) {
        Load();
        UIRectSpec live = ReadRect(FindByName(_doc, "Top"));
        live.x = UILength::Percent(12.5f);
        live.y = UILength::Px(-4.0f);
        live.width = UILength::Px(160.0f);
        const auto edits = UIRectFilePatch::RectAttributeEdits(FindByName(_doc, "Top"), live);
        EXPECT_EQ(edits.size(), 3U);
        EXPECT_TRUE(AttributeNamed(edits, "x", "12.5%"));
        EXPECT_TRUE(AttributeNamed(edits, "y", "-4"));
        EXPECT_TRUE(AttributeNamed(edits, "width", "160"));
        EXPECT_TRUE(AttributeNamed(edits, "height", "20") == false);
    }

    TEST_F(UIRectFilePatchTest, ALostAttributeIsRemovedAndANewOneAdded) {
        Load();
        UIRectSpec live = ReadRect(FindByName(_doc, "Top"));
        live.anchor = UIAnchor::TopLeft;  // the file writes anchor="Top": the default is not written
        live.height = UILength{};         // and a size back to 0
        live.pivot = Vec2{0.5f, 0.0f};
        live.minWidth = UILength::Percent(10.0f);
        live.margin = {1.0f, 2.0f, 3.0f, 4.0f};
        live.region = "board"_intern;
        live.aspectRatio = 1.5f;
        live.aspectMode = UIAspectMode::Fill;
        live.flex = 2.0f;
        live.anchorMin = Vec2{0.0f, 0.0f};
        live.anchorMax = Vec2{1.0f, 0.0f};
        live.maxHeight = UILength::Px(90.0f);
        const auto edits = UIRectFilePatch::RectAttributeEdits(FindByName(_doc, "Top"), live);
        EXPECT_TRUE(Removes(edits, "anchor"));
        EXPECT_TRUE(Removes(edits, "height"));
        EXPECT_TRUE(AttributeNamed(edits, "pivot", "0.5,0"));
        EXPECT_TRUE(AttributeNamed(edits, "minWidth", "10%"));
        EXPECT_TRUE(AttributeNamed(edits, "margin", "1,2,3,4"));
        EXPECT_TRUE(AttributeNamed(edits, "region", "board"));
        EXPECT_TRUE(AttributeNamed(edits, "aspectRatio", "1.5"));
        EXPECT_TRUE(AttributeNamed(edits, "aspectMode", "Fill"));
        EXPECT_TRUE(AttributeNamed(edits, "flex", "2"));
        EXPECT_TRUE(AttributeNamed(edits, "anchorMin", "0,0"));
        EXPECT_TRUE(AttributeNamed(edits, "anchorMax", "1,0"));
        EXPECT_TRUE(AttributeNamed(edits, "maxHeight", "90"));

        const Patched patched = PatchRect(_text, _doc, "Top", live);
        ASSERT_TRUE(patched.ok) << patched.error.c_str();
        pugi::xml_document after;
        String error;
        ASSERT_TRUE(XmlTextPatch::Parse(patched.text, after, error)) << error.c_str();
        EXPECT_EQ(ReadRect(FindByName(after, "Top")), live);
    }

    TEST_F(UIRectFilePatchTest, AnUnsetAnchorPointGoesAway) {
        Load(R"(<node><components><component type="UICanvasComponent"><elements>
  <element type="UIElement" name="A" anchorMin="0,0" anchorMax="1,1" pivot="0.5,0.5" minHeight="5" maxWidth="9%"/>
</elements></component></components></node>)");
        const auto edits = UIRectFilePatch::RectAttributeEdits(FindByName(_doc, "A"), UIRectSpec{});
        EXPECT_EQ(edits.size(), 5U);
        for (StringView name : {"anchorMin", "anchorMax", "pivot", "minHeight", "maxWidth"}) {
            EXPECT_TRUE(Removes(edits, name)) << String(name).c_str();
        }
    }

    TEST_F(UIRectFilePatchTest, OnlyTheRectIsEverNamed) {
        Load();
        UIRectSpec live = ReadRect(FindByName(_doc, "Column"));
        live.x = UILength::Px(99.0f);
        const auto edits = UIRectFilePatch::RectAttributeEdits(FindByName(_doc, "Column"), live);
        ASSERT_EQ(edits.size(), 1U);
        EXPECT_EQ(edits[0].name, "x");

        const Patched patched = PatchRect(_text, _doc, "Column", live);
        ASSERT_TRUE(patched.ok);
        EXPECT_NE(patched.text.find(R"(direction="Horizontal" spacing="4" x="99" visible="false")"), String::npos) << patched.text.c_str();
    }

    TEST_F(UIRectFilePatchTest, ARandomRectOverARandomFileRectReadsBackAsLiveAndTouchesNothingElse) {
        Random random(0xB4A0U);
        const String tail = "\n      <components><component type=\"UITextComponent\" text=\"hi\" x=\"3\"/></components>\n  </element>\n</elements></component></components></node>";
        const String head = "<node><components><component type=\"UICanvasComponent\"><elements>\n  ";
        for (int round = 0; round < 300; ++round) {
            const UIRectSpec fileRect = RandomRect(random);
            const UIRectSpec live = random.Chance() ? RandomRect(random) : fileRect;

            // The file's rect attributes (the writer's spelling) with the element's other attributes among them, in a random order and layout
            Vector<Pair<String, String>> attributes = {{"name", "P"}, {"color", "1,2,3,255"}, {"fontSize", "14"}, {"visible", "false"}};
            {
                UIElementStyle style;
                style.rect = fileRect;
                XmlSerializer serializer;
                serializer.BeginObject("e");
                style.Serialize(serializer);
                serializer.EndObject();
                for (pugi::xml_attribute attribute : serializer.GetRootNode().child("e").attributes()) {
                    attributes.push_back({String(attribute.name()), String(attribute.value())});
                }
            }
            for (size_t i = attributes.size(); i > 1; --i) {
                eastl::swap(attributes[i - 1], attributes[random.Below(i)]);
            }
            String element = "<element type=\"UIPanel\"";
            for (const auto& [name, value] : attributes) {
                element += random.Chance() ? "\n      " : " ";
                const char quote = random.Chance() ? '"' : '\'';
                element += name + (random.Chance() ? "=" : " = ") + quote + value + quote;
            }
            element += ">";
            const String text = head + element + tail;

            pugi::xml_document doc;
            String error;
            ASSERT_TRUE(XmlTextPatch::Parse(text, doc, error)) << error.c_str();
            ASSERT_EQ(ReadRect(FindByName(doc, "P")), fileRect) << text.c_str();

            const Patched patched = PatchRect(text, doc, "P", live);
            ASSERT_TRUE(patched.ok) << patched.error.c_str() << "\n" << text.c_str();
            pugi::xml_document after;
            ASSERT_TRUE(XmlTextPatch::Parse(patched.text, after, error)) << error.c_str() << "\n" << patched.text.c_str();
            EXPECT_EQ(ReadRect(FindByName(after, "P")), live) << text.c_str() << "\n" << patched.text.c_str();
            EXPECT_EQ(OtherAttributes(FindByName(after, "P")), OtherAttributes(FindByName(doc, "P"))) << patched.text.c_str();

            ASSERT_TRUE(StringView(patched.text).starts_with(StringView(head))) << patched.text.c_str();
            ASSERT_TRUE(StringView(patched.text).ends_with(StringView(tail))) << patched.text.c_str();
        }
    }

}  // namespace BECore::Tests
