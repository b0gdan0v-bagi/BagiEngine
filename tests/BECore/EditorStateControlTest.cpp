#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Widgets/EditorDocuments.h>
#include <BECore/Widgets/EditorStateControl.h>
#include <BECore/Widgets/WidgetManager.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        EditorDocumentState MakeDocument(StringView id, EditorDocumentKind kind, bool dirty) {
            EditorDocumentState state;
            state.id = String(id);
            state.kind = kind;
            state.name = String(id);
            state.dirty = dirty;
            state.path = "config/Some.xml";
            state.file = "/overlay/config/Some.xml";
            return state;
        }

        /// A source that reports @p documents as they are when it is asked, so a test can flip a flag between two calls
        EditorDocuments::Source Reports(const Vector<EditorDocumentState>& documents) {
            return [&documents](Vector<EditorDocumentState>& out) {
                for (const EditorDocumentState& document : documents) {
                    out.push_back(document);
                }
            };
        }

    }  // namespace

    /// `editor_state` over a ControlService and an EditorDocuments of their own; only the run mode is the global one (`status.runMode` reads the same)
    class EditorStateControlTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetWidgetManager().SetWidgetLoader([](RunMode) { return Vector<IntrusivePtr<IWidget>>{}; });
            SetMode(RunMode::Editor);
            RegisterEditorStateControl(_service, _documents);
        }

        void TearDown() override {
            SetMode(RunMode::Game);
            CoreManager::GetWidgetManager().SetWidgetLoader({});
        }

        static void SetMode(RunMode mode) {
            CoreManager::GetWidgetManager().CreateWidgets(mode);
        }

        ControlReply Call() {
            return CallControlText(_service, 1, "editor_state");
        }

        EditorDocuments _documents;
        ControlService _service;
    };

    TEST_F(EditorStateControlTest, NoDocumentIsACleanEditor) {
        const ControlReply reply = Call();

        ASSERT_TRUE(reply.IsOk());
        EXPECT_EQ(reply.Result().Raw(), R"({"anyDirty":false,"documents":[]})");
    }

    TEST_F(EditorStateControlTest, DocumentsAreSortedByIdAndWriteEveryField) {
        const Vector<EditorDocumentState> scene{MakeDocument("scene:Main", EditorDocumentKind::Scene, false)};
        const Vector<EditorDocumentState> others{MakeDocument("library:sprites", EditorDocumentKind::Library, false), MakeDocument("nodeFile:ui", EditorDocumentKind::NodeFile, false),
                                                 MakeDocument("config:App", EditorDocumentKind::Config, false)};
        const EditorDocumentsHandle first = _documents.Add(Reports(scene));
        const EditorDocumentsHandle second = _documents.Add(Reports(others));

        const ControlReply reply = Call();

        ASSERT_TRUE(reply.IsOk());
        const JsonView documents = MemberOf(reply.Result(), "documents");
        ASSERT_EQ(documents.Raw().substr(0, 1), "[");
        Vector<String> ids;
        Vector<String> kinds;
        for (const JsonView item : documents.Items()) {
            ids.push_back(StringOf(item, "id"));
            kinds.push_back(StringOf(item, "kind"));
        }
        EXPECT_EQ(ids, (Vector<String>{"config:App", "library:sprites", "nodeFile:ui", "scene:Main"}));
        EXPECT_EQ(kinds, (Vector<String>{"config", "library", "nodeFile", "scene"}));
        const JsonView firstItem = *documents.Items().begin();
        EXPECT_EQ(StringOf(firstItem, "name"), "config:App");
        EXPECT_EQ(StringOf(firstItem, "path"), "config/Some.xml");
        EXPECT_EQ(StringOf(firstItem, "file"), "/overlay/config/Some.xml");
        EXPECT_EQ(BoolOf(firstItem, "dirty"), false);
    }

    TEST_F(EditorStateControlTest, AnyDirtyFollowsTheFlagsOfTheSources) {
        Vector<EditorDocumentState> documents{MakeDocument("scene:Main", EditorDocumentKind::Scene, false), MakeDocument("config:App", EditorDocumentKind::Config, false)};
        const EditorDocumentsHandle handle = _documents.Add(Reports(documents));
        EXPECT_EQ(BoolOf(Call().Result(), "anyDirty"), false);

        documents[1].dirty = true;

        EXPECT_EQ(BoolOf(Call().Result(), "anyDirty"), true);
    }

    TEST_F(EditorStateControlTest, ASourceEndsWithItsHandle) {
        const Vector<EditorDocumentState> documents{MakeDocument("scene:Main", EditorDocumentKind::Scene, true)};
        EditorDocumentsHandle handle = _documents.Add(Reports(documents));
        ASSERT_EQ(BoolOf(Call().Result(), "anyDirty"), true);

        handle.Reset();

        EXPECT_EQ(Call().Result().Raw(), R"({"anyDirty":false,"documents":[]})");
    }

    TEST_F(EditorStateControlTest, AMovedHandleKeepsTheSourceAndTheDestroyedOneRemovesIt) {
        const Vector<EditorDocumentState> documents{MakeDocument("scene:Main", EditorDocumentKind::Scene, true)};
        {
            EditorDocumentsHandle moved;
            {
                EditorDocumentsHandle original = _documents.Add(Reports(documents));
                moved = std::move(original);
            }
            EXPECT_EQ(BoolOf(Call().Result(), "anyDirty"), true);
        }

        EXPECT_EQ(BoolOf(Call().Result(), "anyDirty"), false);
    }

    TEST_F(EditorStateControlTest, OneIdFromTwoSourcesIsOneRow) {
        const Vector<EditorDocumentState> clean{MakeDocument("scene:Main", EditorDocumentKind::Scene, false)};
        const Vector<EditorDocumentState> dirty{MakeDocument("scene:Main", EditorDocumentKind::Scene, true)};
        const EditorDocumentsHandle first = _documents.Add(Reports(clean));
        const EditorDocumentsHandle second = _documents.Add(Reports(dirty));

        const Vector<EditorDocumentState> collected = _documents.Collect();

        ASSERT_EQ(collected.size(), 1u);
        EXPECT_FALSE(collected[0].dirty) << "the earlier source wins";
    }

    TEST_F(EditorStateControlTest, TheGameModeAnswersNotEditor) {
        SetMode(RunMode::Game);

        const ControlReply reply = Call();

        EXPECT_FALSE(reply.IsOk());
        EXPECT_EQ(reply.ErrorCode(), "not_editor");
    }

    TEST_F(EditorStateControlTest, TheMethodIsDescribed) {
        RegisterControlBuiltins(_service);

        const ControlReply reply = CallControlText(_service, 2, "list_methods");

        ASSERT_TRUE(reply.IsOk());
        EXPECT_NE(String(reply.Text()).find("editor_state"), String::npos);
    }

}  // namespace BECore::Tests
