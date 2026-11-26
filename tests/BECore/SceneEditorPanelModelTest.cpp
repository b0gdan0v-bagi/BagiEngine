#include <BECore/SceneEditorHostProbe.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Widgets/SceneEditor/SceneEditorPanelModel.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kFirstNode = "Hero";
        constexpr StringView kSecondNode = "Villain";
        constexpr StringView kTypedName = "Heroine";

    }  // namespace

    /// The model of the Scene Editor's bars over a fake host whose inspected node the test sets, with no ImGui
    class SceneEditorPanelModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _handle = SceneEditorHostRegistry::GetInstance().Register(_host);
            _model = New<SceneEditorPanelModel>();
            _model->Initialize();
        }

        void TearDown() override {
            _model.Reset();
            _handle.Reset();
        }

        const SceneEditorPanelView& Updated() {
            _model->Update();
            return _model->GetView();
        }

        ProbeSceneEditorHost _host;
        SceneEditorHostHandle _handle;
        IntrusivePtr<SceneEditorPanelModel> _model;
    };

    TEST_F(SceneEditorPanelModelTest, WithoutAHostTheViewShowsNoSelection) {
        _host.SelectOne(kFirstNode);
        _host.inspected.dirty = true;
        ASSERT_TRUE(Updated().hasSelection);

        _handle.Reset();
        const SceneEditorPanelView& view = Updated();
        EXPECT_FALSE(view.hasSelection);
        EXPECT_FALSE(view.dirty);
        EXPECT_TRUE(view.name.empty());
        EXPECT_FALSE(view.multiSelection);
        EXPECT_FALSE(view.isInstance);
    }

    TEST_F(SceneEditorPanelModelTest, ASceneWithNothingSelectedShowsOnlyItsUnsavedFlag) {
        _host.inspected.hasScene = true;
        _host.inspected.dirty = true;
        const SceneEditorPanelView& view = Updated();
        EXPECT_FALSE(view.hasSelection);
        EXPECT_TRUE(view.dirty);

        _model->GetView().name = String(kTypedName);
        _model->Update();
        EXPECT_TRUE(_host.renames.empty()) << "a name typed with no node shown is no edit";
    }

    TEST_F(SceneEditorPanelModelTest, OneNodeShowsItsNameLayerAndNoInstanceLine) {
        _host.SelectOne(kFirstNode);
        _host.inspected.layer = 3;
        const SceneEditorPanelView& view = Updated();
        EXPECT_TRUE(view.hasSelection);
        EXPECT_EQ(StringView(view.name), kFirstNode);
        EXPECT_EQ(view.layer, 3);
        EXPECT_FALSE(view.multiSelection);
        EXPECT_FALSE(view.locked);
        EXPECT_FALSE(view.isInstance);
        EXPECT_TRUE(view.instanceText.empty());
    }

    TEST_F(SceneEditorPanelModelTest, SeveralNodesSayHowManyAndThatTheLastIsShown) {
        _host.SelectOne(kFirstNode);
        _host.inspected.selectionCount = 3;
        const SceneEditorPanelView& view = Updated();
        EXPECT_TRUE(view.multiSelection);
        EXPECT_EQ(StringView(view.multiText), "3 nodes selected; showing the last one");

        _host.inspected.selectionCount = 1;
        EXPECT_FALSE(Updated().multiSelection);
    }

    TEST_F(SceneEditorPanelModelTest, ALockedNodeTakesNoEditAndTheViewGoesBackToItsName) {
        _host.SelectOne(kFirstNode);
        _host.inspected.locked = true;
        ASSERT_TRUE(Updated().locked);

        _model->GetView().name = String(kTypedName);
        _model->GetView().layer = 7;
        const SceneEditorPanelView& view = Updated();
        EXPECT_EQ(StringView(view.name), kFirstNode);
        EXPECT_EQ(view.layer, 0);
        EXPECT_FALSE(view.dirty);
    }

    TEST_F(SceneEditorPanelModelTest, AnInstanceNamesItsNodeFile) {
        _host.SelectOne(kFirstNode);
        _host.inspected.sourceRef = "ui/HeroCard"_intern;
        const SceneEditorPanelView& view = Updated();
        EXPECT_TRUE(view.isInstance);
        EXPECT_EQ(StringView(view.instanceText), "Instance of ui/HeroCard");

        _host.inspected.sourceRef = PoolString();
        EXPECT_FALSE(Updated().isInstance);
        EXPECT_TRUE(_model->GetView().instanceText.empty());
    }

    TEST_F(SceneEditorPanelModelTest, ATypedNameReachesTheHostOncePerChange) {
        _host.SelectOne(kFirstNode);
        Updated();

        _model->GetView().name = String(kTypedName);
        const SceneEditorPanelView& view = Updated();
        ASSERT_EQ(_host.renames.size(), 1u);
        EXPECT_EQ(StringView(_host.renames[0]), kTypedName);
        EXPECT_EQ(StringView(view.name), kTypedName);
        EXPECT_TRUE(view.dirty);

        Updated();
        EXPECT_EQ(_host.renames.size(), 1u) << "a steady frame sends nothing";

        _model->GetView().name = String(kSecondNode);
        Updated();
        EXPECT_EQ(_host.renames.size(), 2u);
        EXPECT_TRUE(_host.layers.empty());
    }

    TEST_F(SceneEditorPanelModelTest, ALayerEditReachesTheHostOnce) {
        _host.SelectOne(kFirstNode);
        Updated();

        _model->GetView().layer = -4;
        EXPECT_EQ(Updated().layer, -4);
        ASSERT_EQ(_host.layers.size(), 1u);
        EXPECT_EQ(_host.layers[0], -4);

        Updated();
        EXPECT_EQ(_host.layers.size(), 1u);
        EXPECT_TRUE(_host.renames.empty());
    }

    TEST_F(SceneEditorPanelModelTest, ASelectionEventShowsTheNewNodeAndDropsAnEditNotYetApplied) {
        _host.SelectOne(kFirstNode);
        Updated();

        _model->GetView().name = String(kTypedName);
        _model->GetView().layer = 9;
        _host.SelectOne(kSecondNode);
        SceneNodeSelectedEvent::Emit(nullptr, nullptr);
        EXPECT_EQ(StringView(_model->GetView().name), kSecondNode) << "shown before the next Update";

        Updated();
        EXPECT_TRUE(_host.renames.empty());
        EXPECT_TRUE(_host.layers.empty());
        EXPECT_EQ(_model->GetView().layer, 0);
    }

    TEST_F(SceneEditorPanelModelTest, ASteadyUpdateAllocatesNothing) {
        _host.SelectOne(kFirstNode);
        _host.inspected.selectionCount = 2;
        _host.inspected.sourceRef = "ui/HeroCard"_intern;
        Updated();
        _model->GetView().name = String(kTypedName);
        Updated();

        ExpectNoAllocations([&] { _model->Update(); });
    }

}  // namespace BECore::Tests
