#include <BECore/Config/StringXmlWriter.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeInstance.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UINodeFileSave.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kHudRef = "nodes/Hud";
        constexpr const char* kSeatRef = "nodes/Seat";

        // A real-shaped node file: a comment block first, comments between elements, an element with its attributes on several lines, mixed quotes, two canvases
        constexpr const char* kHudNode = R"(<?xml version="1.0" encoding="UTF-8"?>
<!--
  The HUD of the table.
  Written by hand: keep the comments.
-->
<node name="Hud" layer="10">
  <components>
    <component type="TransformComponent" x="0" y="0" width="10" height="10"/>
    <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
      <elements>
        <!-- the score, top left -->
        <element type="UIElement" name="Score"
                 anchor="TopLeft"
                 x="12" y="8"
                 width="160" height="24"
                 fontSize="20">
        </element>

        <!-- the move line -->
        <element type="UIElement" name="MoveLine" anchor='Bottom' y="-8" width="300" height="30"/>
      </elements>
    </component>
  </components>
</node>
)";

        // The second canvas of a node is a component of its own in the file
        constexpr const char* kTwoCanvasNode = R"(<node name="Hud" layer="10">
  <components>
    <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
      <elements>
        <element type="UIElement" name="Score" x="12" y="8" width="160" height="24"/>
      </elements>
    </component>
    <component type="UICanvasComponent" space="SceneView">
      <elements>
        <element type="UIPanel" name="Banner" anchor="Top" width="50%" height="40" color="1,2,3,255"/>
      </elements>
    </component>
  </components>
</node>
)";

        constexpr const char* kSeatNode = R"(<node name="Seat"><components>
  <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
    <elements><element type="UIElement" name="Plate" x="1" y="2" width="30" height="40"/></elements>
  </component>
</components></node>
)";

        Vector<String> Lines(const String& text) {
            Vector<String> lines;
            size_t begin = 0;
            while (begin <= text.size()) {
                size_t end = text.find('\n', begin);
                if (end == String::npos) {
                    end = text.size();
                }
                lines.push_back(text.substr(begin, end - begin));
                begin = end + 1;
            }
            return lines;
        }

        /// The numbers (0-based) of the lines that differ between two texts of the same number of lines
        Vector<size_t> DifferingLines(const String& before, const String& after) {
            const Vector<String> a = Lines(before);
            const Vector<String> b = Lines(after);
            EXPECT_EQ(a.size(), b.size());
            Vector<size_t> differing;
            for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
                if (a[i] != b[i]) {
                    differing.push_back(i);
                }
            }
            return differing;
        }

        String Printed(pugi::xml_node node) {
            StringXmlWriter writer;
            node.print(writer, "", pugi::format_raw);
            return eastl::move(writer.text);
        }

        bool Mentions(const Vector<String>& notes, StringView text) {
            for (const String& note : notes) {
                if (note.find(String(text)) != String::npos) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    class UINodeFileSaveTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("BagiUINodeFileSaveTest");
            RemoveDirectory(_dir);
            WriteFile(_dir / "nodes" / "Hud.xml", kHudNode);
            WriteFile(_dir / "nodes" / "Seat.xml", kSeatNode);
            _loader = MakeDirectoryNodeLoader(_dir);
        }

        void TearDown() override {
            _manager.Reset();
            RemoveDirectory(_dir);
        }

        /// Loads the scene Main with @p nodesXml as its nodes; the scene is kept in `_scene`
        void LoadScene(const String& nodesXml) {
            const String text = Format("<SceneManager><scenes><scene type=\"Scene\" name=\"Main\"><nodes>{}</nodes></scene></scenes></SceneManager>", nodesXml);
            const pugi::xml_parse_result parsed = _doc.load_string(text.c_str());
            ASSERT_TRUE(parsed) << parsed.description();
            _manager = LoadSceneManager(XmlNode{_doc.document_element()}, _loader);
            _manager->Initialize();
            _scene = _manager->GetSceneByName("Main"_intern);
            ASSERT_NE(_scene, nullptr);
        }

        SceneNode& NodeAt(size_t index) {
            return *_scene->GetRootNode().GetChildren().at(index);
        }

        /// The @p ordinal-th canvas of @p node
        static UICanvasComponent& CanvasOf(SceneNode& node, size_t ordinal) {
            size_t seen = 0;
            for (const auto& component : node.GetComponents()) {
                if (component->GetTypeMeta().typeName == "UICanvasComponent" && seen++ == ordinal) {
                    return static_cast<UICanvasComponent&>(*component);
                }
            }
            ADD_FAILURE() << "no canvas " << ordinal;
            return *New<UICanvasComponent>();
        }

        UINodeFileReport Save(Span<const IntrusivePtr<UIElement>> edited) {
            // Named: UINodeFileIo holds FunctionRefs, which would dangle on temporaries once this statement ends
            const auto read = [&](StringView ref, String& text, String& error) { return Read(ref, text, error); };
            const auto write = [&](StringView ref, StringView text, String& error) { return Write(ref, text, error); };
            const UINodeFileIo io{read, write};
            return SaveUIRectsToNodeFiles(*_scene, edited, io);
        }

        bool Read(StringView ref, String& text, String& error) {
            ++_reads;
            if (_failRead) {
                error = "disk on fire";
                return false;
            }
            const auto path = _dir / Format("{}.xml", ref).c_str();
            if (!PathExists(path)) {
                error = "no file";
                return false;
            }
            text = ReadFile(path);
            return true;
        }

        bool Write(StringView ref, StringView text, String& error) {
            ++_writes;
            if (_failWrite) {
                error = "read only";
                return false;
            }
            WriteFile(_dir / Format("{}.xml", ref).c_str(), String(text));
            return true;
        }

        String FileText(const char* ref) const {
            return ReadFile(_dir / Format("{}.xml", ref).c_str());
        }

        /// The reference of @p node as the editor would save it, printed
        String BuiltReference(const SceneNode& node) {
            _out.reset();
            pugi::xml_node nodesEl = _out.append_child("nodes");
            String reason;
            EXPECT_TRUE(BuildNodeReference(node, _loader, nodesEl, reason)) << reason.c_str();
            return Printed(nodesEl);
        }

        Path _dir;
        NodeFileLoader _loader;
        pugi::xml_document _doc;
        pugi::xml_document _out;
        IntrusivePtr<SceneManager> _manager;
        Scene* _scene = nullptr;
        int _reads = 0;
        int _writes = 0;
        bool _failRead = false;
        bool _failWrite = false;
    };

    TEST_F(UINodeFileSaveTest, OneEditedElementChangesItsStartTagAlone) {
        LoadScene(R"(<node ref="nodes/Hud"/>)");
        UICanvasComponent& canvas = CanvasOf(NodeAt(0), 0);
        UIElement& score = *canvas._elements[0];
        score.style.rect.x = UILength::Px(40.0f);
        score.style.rect.height = UILength::Percent(10.0f);

        const Vector<IntrusivePtr<UIElement>> edited = {canvas._elements[0]};
        const UINodeFileReport report = Save(edited);

        EXPECT_EQ(report.filesWritten, 1U);
        EXPECT_EQ(report.elementsPatched, 1U);
        EXPECT_TRUE(report.notes.empty());
        const String after = FileText(kHudRef);
        ASSERT_NE(after, kHudNode);
        // The start tag of Score is on lines 11 to 15 (0-based): only those lines differ
        const Vector<size_t> differing = DifferingLines(kHudNode, after);
        ASSERT_FALSE(differing.empty());
        for (size_t line : differing) {
            EXPECT_GE(line, 11U);
            EXPECT_LE(line, 15U);
        }
        EXPECT_NE(after.find("x=\"40\""), String::npos);
        EXPECT_NE(after.find("height=\"10%\""), String::npos);
        EXPECT_NE(after.find("<!-- the score, top left -->"), String::npos);
        EXPECT_NE(after.find("anchor='Bottom'"), String::npos);
    }

    TEST_F(UINodeFileSaveTest, NothingEditedOrEditedBackWritesNothing) {
        LoadScene(R"(<node ref="nodes/Hud"/>)");
        UICanvasComponent& canvas = CanvasOf(NodeAt(0), 0);

        UINodeFileReport report = Save(Span<const IntrusivePtr<UIElement>>{});
        EXPECT_EQ(report.filesWritten, 0U);
        EXPECT_EQ(_reads, 0);

        const UIRectSpec original = canvas._elements[0]->style.rect;
        canvas._elements[0]->style.rect.x = UILength::Px(99.0f);
        canvas._elements[0]->style.rect.x = original.x;
        const Vector<IntrusivePtr<UIElement>> edited = {canvas._elements[0], canvas._elements[1]};
        report = Save(edited);
        EXPECT_EQ(report.filesWritten, 0U);
        EXPECT_EQ(report.elementsPatched, 0U);
        EXPECT_TRUE(report.notes.empty());
        EXPECT_EQ(_writes, 0);
        EXPECT_EQ(FileText(kHudRef), kHudNode);
    }

    TEST_F(UINodeFileSaveTest, TheTextOfAnUnchangedFileIsTheSameBytes) {
        LoadScene(R"(<node ref="nodes/Hud"/>)");
        UICanvasComponent& canvas = CanvasOf(NodeAt(0), 0);
        String patched;
        size_t patchedElements = 7;
        Vector<String> notes;
        const Vector<IntrusivePtr<UIElement>> edited = {canvas._elements[1]};
        EXPECT_FALSE(PatchNodeFileText(kHudNode, NodeAt(0), edited, patched, patchedElements, notes));
        EXPECT_EQ(patched, kHudNode);
        EXPECT_EQ(patchedElements, 0U);
        EXPECT_TRUE(notes.empty());
    }

    TEST_F(UINodeFileSaveTest, TwoElementsOfTwoCanvasesOfOneNodeAreBothWritten) {
        WriteFile(_dir / "nodes" / "Hud.xml", kTwoCanvasNode);
        LoadScene(R"(<node ref="nodes/Hud"/>)");
        UICanvasComponent& first = CanvasOf(NodeAt(0), 0);
        UICanvasComponent& second = CanvasOf(NodeAt(0), 1);
        ASSERT_NE(&first, &second);
        first._elements[0]->style.rect.y = UILength::Px(20.0f);
        second._elements[0]->style.rect.anchor = UIAnchor::Bottom;
        second._elements[0]->style.rect.width = UILength::Percent(75.0f);

        const Vector<IntrusivePtr<UIElement>> edited = {second._elements[0], first._elements[0]};
        const UINodeFileReport report = Save(edited);

        EXPECT_EQ(report.filesWritten, 1U);
        EXPECT_EQ(report.elementsPatched, 2U);
        EXPECT_TRUE(report.notes.empty());
        const String after = FileText(kHudRef);
        EXPECT_NE(after.find(R"(name="Score" x="12" y="20" width="160" height="24")"), String::npos) << after.c_str();
        EXPECT_NE(after.find(R"(name="Banner" anchor="Bottom" width="75%" height="40")"), String::npos) << after.c_str();
        EXPECT_EQ(Lines(after).size(), Lines(kTwoCanvasNode).size());
    }

    TEST_F(UINodeFileSaveTest, AFileWithTwoInstancesInTheSceneIsSkippedWithANote) {
        LoadScene(R"(<node ref="nodes/Seat"/><node ref="nodes/Seat" name="Seat2"/>)");
        UICanvasComponent& canvas = CanvasOf(NodeAt(0), 0);
        canvas._elements[0]->style.rect.x = UILength::Px(50.0f);

        const Vector<IntrusivePtr<UIElement>> edited = {canvas._elements[0]};
        const UINodeFileReport report = Save(edited);

        EXPECT_EQ(report.filesWritten, 0U);
        EXPECT_EQ(_writes, 0);
        ASSERT_EQ(report.notes.size(), 1U);
        EXPECT_TRUE(Mentions(report.notes, kSeatRef));
        EXPECT_TRUE(Mentions(report.notes, "2 instances"));
        EXPECT_EQ(FileText(kSeatRef), kSeatNode);
    }

    TEST_F(UINodeFileSaveTest, AnElementTheFileDoesNotHoldAsTheSceneHasItIsSkippedAndTheOthersAreWritten) {
        // The scene's override replaces the whole list: the second element has another name than the file's
        LoadScene(R"(<node ref="nodes/Hud"><components><component type="UICanvasComponent"><elements>
            <element type="UIElement" name="Score" anchor="TopLeft" x="12" y="8" width="160" height="24" fontSize="20"/>
            <element type="UIElement" name="Renamed" anchor="Bottom" y="-8" width="300" height="30"/>
        </elements></component></components></node>)");
        UICanvasComponent& canvas = CanvasOf(NodeAt(0), 0);
        ASSERT_EQ(canvas._elements.size(), 2U);
        canvas._elements[0]->style.rect.x = UILength::Px(1.0f);
        canvas._elements[1]->style.rect.x = UILength::Px(2.0f);

        const Vector<IntrusivePtr<UIElement>> edited = {canvas._elements[1], canvas._elements[0]};
        const UINodeFileReport report = Save(edited);

        EXPECT_EQ(report.filesWritten, 1U);
        EXPECT_EQ(report.elementsPatched, 1U);
        ASSERT_EQ(report.notes.size(), 1U);
        EXPECT_TRUE(Mentions(report.notes, "Hud/Renamed"));
        EXPECT_TRUE(Mentions(report.notes, kHudRef));
        const String after = FileText(kHudRef);
        EXPECT_NE(after.find("x=\"1\""), String::npos);
        EXPECT_EQ(after.find("x=\"2\""), String::npos);
    }

    TEST_F(UINodeFileSaveTest, AnElementAddedInCodeIsSkippedWithANote) {
        LoadScene(R"(<node ref="nodes/Hud"/>)");
        UICanvasComponent& canvas = CanvasOf(NodeAt(0), 0);
        auto added = canvas.GetRoot().AddChild("Late"_intern);
        added->style.rect.x = UILength::Px(3.0f);

        const Vector<IntrusivePtr<UIElement>> edited = {added};
        const UINodeFileReport report = Save(edited);

        EXPECT_EQ(report.filesWritten, 0U);
        ASSERT_EQ(report.notes.size(), 1U);
        EXPECT_TRUE(Mentions(report.notes, "Hud/Late"));
    }

    TEST_F(UINodeFileSaveTest, AFailedReadAndAFailedWriteAreNotesAndNoException) {
        LoadScene(R"(<node ref="nodes/Hud"/>)");
        UICanvasComponent& canvas = CanvasOf(NodeAt(0), 0);
        canvas._elements[0]->style.rect.x = UILength::Px(5.0f);
        const Vector<IntrusivePtr<UIElement>> edited = {canvas._elements[0]};

        _failRead = true;
        UINodeFileReport report = Save(edited);
        EXPECT_EQ(report.filesWritten, 0U);
        ASSERT_EQ(report.notes.size(), 1U);
        EXPECT_TRUE(Mentions(report.notes, "disk on fire"));
        EXPECT_TRUE(Mentions(report.notes, kHudRef));

        _failRead = false;
        _failWrite = true;
        report = Save(edited);
        EXPECT_EQ(report.filesWritten, 0U);
        EXPECT_EQ(report.elementsPatched, 0U);
        ASSERT_EQ(report.notes.size(), 1U);
        EXPECT_TRUE(Mentions(report.notes, "read only"));
        EXPECT_EQ(FileText(kHudRef), kHudNode);
    }

    TEST_F(UINodeFileSaveTest, AFileThatDoesNotParseIsOneNote) {
        WriteFile(_dir / "nodes" / "Seat.xml", kSeatNode);
        LoadScene(R"(<node ref="nodes/Seat"/>)");
        UICanvasComponent& canvas = CanvasOf(NodeAt(0), 0);
        canvas._elements[0]->style.rect.x = UILength::Px(5.0f);
        WriteFile(_dir / "nodes" / "Seat.xml", "<node><components></node>");

        const Vector<IntrusivePtr<UIElement>> edited = {canvas._elements[0]};
        const UINodeFileReport report = Save(edited);

        EXPECT_EQ(report.filesWritten, 0U);
        ASSERT_EQ(report.notes.size(), 1U);
        EXPECT_TRUE(Mentions(report.notes, kSeatRef));
        EXPECT_EQ(FileText(kSeatRef), "<node><components></node>");
    }

    TEST_F(UINodeFileSaveTest, ANodeWrittenInlineInTheSceneIsNotInTheReport) {
        LoadScene(R"(<node name="Inline"><components>
            <component type="UICanvasComponent"><elements><element type="UIElement" name="A" x="1" width="5" height="5"/></elements></component>
        </components></node>)");
        UICanvasComponent& canvas = CanvasOf(NodeAt(0), 0);
        canvas._elements[0]->style.rect.x = UILength::Px(9.0f);

        const Vector<IntrusivePtr<UIElement>> edited = {canvas._elements[0]};
        const UINodeFileReport report = Save(edited);

        EXPECT_EQ(report.filesWritten, 0U);
        EXPECT_TRUE(report.notes.empty());
        EXPECT_EQ(_reads, 0);
    }

    TEST_F(UINodeFileSaveTest, AfterTheSaveTheNodeIsABareReferenceAndBeforeItAnOverride) {
        LoadScene(R"(<node ref="nodes/Hud"/>)");
        UICanvasComponent& canvas = CanvasOf(NodeAt(0), 0);
        canvas._elements[1]->style.rect.anchor = UIAnchor::Top;
        canvas._elements[1]->style.rect.y = UILength::Px(30.0f);
        canvas._elements[1]->style.rect.region = "board"_intern;

        // Without the patch the edit can only be written as an override in the scene file
        EXPECT_NE(BuiltReference(NodeAt(0)).find("<elements>"), String::npos);

        const Vector<IntrusivePtr<UIElement>> edited = {canvas._elements[1]};
        const UINodeFileReport report = Save(edited);
        ASSERT_EQ(report.filesWritten, 1U);

        EXPECT_EQ(BuiltReference(NodeAt(0)), R"(<nodes><node ref="nodes/Hud"/></nodes>)");

        // The patched text read again gives the rect of the live element
        pugi::xml_document file;
        ASSERT_TRUE(file.load_string(FileText(kHudRef).c_str()));
        pugi::xml_node moveLine;
        for (pugi::xml_node element : file.document_element().child("components").children("component")) {
            for (pugi::xml_node candidate : element.child("elements").children("element")) {
                if (StringView{candidate.attribute("name").value()} == "MoveLine") {
                    moveLine = candidate;
                }
            }
        }
        ASSERT_TRUE(moveLine);
        EXPECT_STREQ(moveLine.attribute("anchor").value(), "Top");
        EXPECT_STREQ(moveLine.attribute("y").value(), "30");
        EXPECT_STREQ(moveLine.attribute("region").value(), "board");
    }

}  // namespace BECore::Tests
