#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/Viewport/ViewportToolbarModel.h>
#include <Widgets/Viewport/ViewportUIDebug.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// What the model tells the editor
        class ToolbarEventRecorder : public SubscriptionHolder {
        public:
            ToolbarEventRecorder() {
                Subscribe<ViewportPreviewChangedEvent, &ToolbarEventRecorder::OnPreview>(this);
                Subscribe<ViewportInputModeChangedEvent, &ToolbarEventRecorder::OnInputMode>(this);
            }

            int previews = 0;
            PreviewSettings lastPreview;
            int inputModes = 0;
            ViewportInputMode lastInputMode = ViewportInputMode::Play;

        private:
            void OnPreview(const ViewportPreviewChangedEvent& event) {
                ++previews;
                lastPreview = event.GetPreview();
            }

            void OnInputMode(const ViewportInputModeChangedEvent& event) {
                ++inputModes;
                lastInputMode = event.mode;
            }
        };

        constexpr int32_t kFirstDeviceRow = static_cast<int32_t>(EnumUtils<ViewportAspect>::Count()) + 1;

        int32_t DeviceCount() {
            return static_cast<int32_t>(UIPreview::GetDevices().size());
        }

        int32_t CustomRow() {
            return kFirstDeviceRow + DeviceCount() + 1;
        }

        // A scene with a named and an unnamed view variant over a default of 1280 x 720, and one with none and two canvases
        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Responsive" viewWidth="1280" viewHeight="720">
    <viewVariants>
      <view name="Portrait" when="orientation:portrait" width="720" height="1280"/>
      <view when="minAspect:2" width="1600" height="600"/>
    </viewVariants>
  </scene>
  <scene type="Scene" name="Plain"><nodes>
    <node name="Hud"><components><component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/></components></node>
    <node name="Menu"><components><component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/></components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        // The same scene with its second variant removed
        constexpr const char* kFewerVariantsXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Responsive" viewWidth="1280" viewHeight="720">
    <viewVariants>
      <view name="Portrait" when="orientation:portrait" width="720" height="1280"/>
    </viewVariants>
  </scene>
</scenes></SceneManager>)";

        constexpr int32_t kAutoRow = 0;
        constexpr int32_t kDefaultRow = 1;
        constexpr int32_t kFirstVariantRow = 2;

        IntrusivePtr<SceneManager> LoadScenes(const char* xml) {
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(xml));
            IntrusivePtr<SceneManager> manager = New<SceneManager>();
            manager->Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            manager->Initialize();
            return manager;
        }

        EditorPreferencesChangedEvent PreferencesWith(const PreviewSettings& preview, ViewportInputMode mode) {
            return EditorPreferencesChangedEvent(false, 0, preview, mode, {}, {});
        }

    }  // namespace

    /// The toolbar's model with no ImGui: a control's write is played by writing the view, as `PanelBindingWriter` does
    class ViewportToolbarModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ASSERT_GT(DeviceCount(), 0);
            UseScenes(LoadScenes(kScenesXml));
            _model = New<ViewportToolbarModel>();
            _model->Initialize();
        }

        void TearDown() override {
            _uiDebug.Release();
            _model.Reset();
            EngineEnvironment::UseSceneManager({});
            _scenes.Reset();
        }

        /// Makes @p manager the engine's, with its first scene active
        void UseScenes(IntrusivePtr<SceneManager> manager) {
            _scenes = std::move(manager);
            EngineEnvironment::UseSceneManager(_scenes);
            _scenes->SetActiveScene("Responsive"_intern);
        }

        Scene& Active() const {
            return Require(_scenes->GetActiveScene());
        }

        Scene& Plain() const {
            return Require(_scenes->GetSceneByName("Plain"_intern));
        }

        /// Collects the canvases of @p scene as the viewport's pass does and publishes them to the model
        void PublishCanvasesOf(Scene& scene) {
            _uiDebug.Collect(&scene, [this](const SceneNode* node) { return _model->IsCanvasEnabled(node); });
            _model->PublishCanvases(&scene, _uiDebug.GetCanvasRows());
        }

        const SceneNode* CanvasNode(size_t index) const {
            return At(Plain().GetRootNode().GetChildren(), index).Get();
        }

        /// The object the asset's controls write
        ViewportToolbarView& Controls() {
            return *static_cast<ViewportToolbarView*>(_model->GetObject().Address());
        }

        const ViewportToolbarView& Updated() {
            _model->Update();
            return _model->GetView();
        }

        PreviewShape Shape() const {
            return _model->GetPreview().shape;
        }

        IntrusivePtr<SceneManager> _scenes;
        IntrusivePtr<ViewportToolbarModel> _model;
        ViewportUIDebug _uiDebug;
    };

    TEST_F(ViewportToolbarModelTest, TheRowsAreTheAspectsTheDevicesAndCustomWithALineBetweenTheGroups) {
        const ViewportToolbarView& view = _model->GetView();
        ASSERT_EQ(view.sizeItems.size(), static_cast<size_t>(CustomRow()) + 1);
        EXPECT_TRUE(view.sizeItems[static_cast<size_t>(kFirstDeviceRow) - 1].separator);
        EXPECT_TRUE(view.sizeItems[static_cast<size_t>(CustomRow()) - 1].separator);
        const Span<const PreviewDevice> devices = UIPreview::GetDevices();
        for (int32_t i = 0; i < DeviceCount(); ++i) {
            const String& text = view.sizeItems[static_cast<size_t>(kFirstDeviceRow + i)].text;
            EXPECT_NE(text.find(String(devices[static_cast<size_t>(i)].name.data(), devices[static_cast<size_t>(i)].name.size())), String::npos) << text.c_str();
        }
        EXPECT_EQ(view.safeAreaItems.size(), EnumUtils<SafeAreaSimulation>::Count());
    }

    TEST_F(ViewportToolbarModelTest, EverySizeRowDecodesToItsShapeAndBack) {
        for (int32_t row = 0; row < kFirstDeviceRow - 1; ++row) {
            Controls().sizeRow = row;
            EXPECT_EQ(Updated().sizeRow, row);
            EXPECT_EQ(Shape().aspect, static_cast<ViewportAspect>(row));
            EXPECT_EQ(Shape().device, -1);
            EXPECT_FALSE(Shape().custom);
        }
        for (int32_t i = 0; i < DeviceCount(); ++i) {
            Controls().sizeRow = kFirstDeviceRow + i;
            EXPECT_EQ(Updated().sizeRow, kFirstDeviceRow + i);
            EXPECT_EQ(Shape().device, i);
            EXPECT_FALSE(Shape().custom);
        }
        Controls().sizeRow = CustomRow();
        EXPECT_EQ(Updated().sizeRow, CustomRow());
        EXPECT_TRUE(Shape().custom);
    }

    TEST_F(ViewportToolbarModelTest, ASeparatorRowOrARowPastTheEndChangesNothing) {
        Controls().sizeRow = static_cast<int32_t>(ViewportAspect::Standard4x3);
        _model->Update();
        const PreviewShape before = Shape();

        for (const int32_t row : {kFirstDeviceRow - 1, CustomRow() - 1, CustomRow() + 1, -1}) {
            Controls().sizeRow = row;
            EXPECT_EQ(Updated().sizeRow, static_cast<int32_t>(ViewportAspect::Standard4x3)) << row;
            EXPECT_EQ(Shape(), before) << row;
        }
    }

    TEST_F(ViewportToolbarModelTest, ADeviceBringsTheDeviceSafeAreaAndAnyOtherSizeDropsIt) {
        Controls().sizeRow = kFirstDeviceRow;
        EXPECT_EQ(Updated().safeAreaRow, static_cast<int32_t>(SafeAreaSimulation::Device));
        EXPECT_EQ(_model->GetPreview().safeArea, SafeAreaSimulation::Device);

        Controls().sizeRow = static_cast<int32_t>(ViewportAspect::Wide16x9);
        EXPECT_EQ(Updated().safeAreaRow, static_cast<int32_t>(SafeAreaSimulation::None));

        // Another simulation is the user's and stays over a size change; only Device follows the size
        Controls().safeAreaRow = static_cast<int32_t>(SafeAreaSimulation::TopNotch);
        _model->Update();
        Controls().sizeRow = CustomRow();
        _model->Update();
        EXPECT_EQ(_model->GetPreview().safeArea, SafeAreaSimulation::TopNotch);

        Controls().sizeRow = kFirstDeviceRow;
        _model->Update();
        Controls().sizeRow = CustomRow();
        _model->Update();
        EXPECT_EQ(_model->GetPreview().safeArea, SafeAreaSimulation::None);
    }

    TEST_F(ViewportToolbarModelTest, CustomKeepsTheOtherFieldsOfTheShapeAndTakesTheTypedSides) {
        Controls().sizeRow = kFirstDeviceRow + 1;
        _model->Update();
        Controls().sizeRow = CustomRow();
        const ViewportToolbarView& view = Updated();
        EXPECT_TRUE(view.isCustom);
        EXPECT_FALSE(view.sizeFree);
        EXPECT_EQ(Shape().device, 1);

        Controls().customWidth = 1280;
        Controls().customHeight = 720;
        _model->Update();
        EXPECT_EQ(Shape().customWidth, 1280u);
        EXPECT_EQ(Shape().customHeight, 720u);

        Controls().sizeRow = static_cast<int32_t>(ViewportAspect::Free);
        EXPECT_FALSE(Updated().isCustom);
        EXPECT_EQ(Shape().customWidth, 1280u);
    }

    TEST_F(ViewportToolbarModelTest, RotateIsOffOnlyForTheFreeShape) {
        EXPECT_TRUE(_model->GetView().sizeFree);
        Controls().sizeRow = static_cast<int32_t>(ViewportAspect::Portrait9x16);
        EXPECT_FALSE(Updated().sizeFree);
        Controls().rotated = true;
        _model->Update();
        EXPECT_TRUE(Shape().rotated);
        Controls().sizeRow = kFirstDeviceRow;
        EXPECT_FALSE(Updated().sizeFree);
        Controls().sizeRow = static_cast<int32_t>(ViewportAspect::Free);
        EXPECT_TRUE(Updated().sizeFree);
    }

    TEST_F(ViewportToolbarModelTest, AStoredDeviceThisBuildLacksShowsItsAspectRowAndIsKept) {
        PreviewSettings stored;
        stored.shape.aspect = ViewportAspect::UltraWide21x9;
        stored.shape.device = DeviceCount() + 5;
        EditorPreferencesChangedEvent::Emit(PreferencesWith(stored, ViewportInputMode::Play));

        const ViewportToolbarView& view = Updated();
        EXPECT_EQ(view.sizeRow, static_cast<int32_t>(ViewportAspect::UltraWide21x9));
        EXPECT_FALSE(view.sizeFree);
        EXPECT_EQ(Shape().device, DeviceCount() + 5);
    }

    TEST_F(ViewportToolbarModelTest, TheStoredPreviewIsAppliedSilentlyAndAUserChangeIsToldOnce) {
        ToolbarEventRecorder recorder;
        PreviewSettings stored;
        stored.shape.device = 0;
        stored.safeArea = SafeAreaSimulation::Device;
        stored.overlays.regions = true;
        EditorPreferencesChangedEvent::Emit(PreferencesWith(stored, ViewportInputMode::Play));

        const ViewportToolbarView& view = Updated();
        EXPECT_EQ(view.sizeRow, kFirstDeviceRow);
        EXPECT_TRUE(view.overlayRegions);
        EXPECT_EQ(_model->GetPreview(), stored);
        _model->Update();
        EXPECT_EQ(recorder.previews, 0);

        Controls().overlayFindings = true;
        _model->Update();
        _model->Update();
        ASSERT_EQ(recorder.previews, 1);
        EXPECT_TRUE(recorder.lastPreview.overlays.findings);
        EXPECT_TRUE(recorder.lastPreview.overlays.regions);
        EXPECT_EQ(recorder.lastPreview.shape.device, 0);

        // A stored value that equals what was told sends nothing back either
        EditorPreferencesChangedEvent::Emit(PreferencesWith(recorder.lastPreview, ViewportInputMode::Play));
        _model->Update();
        EXPECT_EQ(recorder.previews, 1);
        EXPECT_EQ(recorder.inputModes, 0);
    }

    TEST_F(ViewportToolbarModelTest, TheInputModeIsAppliedSilentlyAndTheCommandOrTheViewIsToldOnce) {
        ToolbarEventRecorder recorder;
        EditorPreferencesChangedEvent::Emit(PreferencesWith({}, ViewportInputMode::Edit));
        EXPECT_EQ(_model->GetInputMode(), ViewportInputMode::Edit);
        EXPECT_EQ(Updated().inputMode, ViewportInputMode::Edit);
        EXPECT_EQ(recorder.inputModes, 0);

        EXPECT_TRUE(_model->RunCommand(EditorCommandIds::kViewportInputMode.View(), {}));
        EXPECT_EQ(_model->GetInputMode(), ViewportInputMode::Play);
        _model->Update();
        _model->Update();
        ASSERT_EQ(recorder.inputModes, 1);
        EXPECT_EQ(recorder.lastInputMode, ViewportInputMode::Play);

        Controls().inputMode = ViewportInputMode::Edit;
        _model->Update();
        EXPECT_EQ(recorder.inputModes, 2);
        EXPECT_EQ(recorder.lastInputMode, ViewportInputMode::Edit);
        EXPECT_EQ(recorder.previews, 0);

        EXPECT_FALSE(_model->RunCommand(EditorCommandIds::kViewportFrameSelected.View(), {}));
    }

    TEST_F(ViewportToolbarModelTest, TheToolAndSpaceCommandsSetTheGizmoChoicesTheViewShows) {
        EXPECT_TRUE(_model->RunCommand(EditorCommandIds::kViewportToolScale.View(), {}));
        EXPECT_EQ(_model->GetGizmoSettings().tool, Gizmo::Tool::Scale);
        EXPECT_EQ(Updated().tool, Gizmo::Tool::Scale);
        EXPECT_TRUE(_model->RunCommand(EditorCommandIds::kViewportToolTranslate.View(), {}));
        EXPECT_EQ(_model->GetGizmoSettings().tool, Gizmo::Tool::Translate);

        EXPECT_TRUE(_model->RunCommand(EditorCommandIds::kViewportGizmoSpace.View(), {}));
        EXPECT_EQ(_model->GetGizmoSettings().space, Gizmo::Space::Local);
        EXPECT_EQ(Updated().space, Gizmo::Space::Local);
        EXPECT_TRUE(_model->RunCommand(EditorCommandIds::kViewportGizmoSpace.View(), {}));
        EXPECT_EQ(_model->GetGizmoSettings().space, Gizmo::Space::World);
    }

    TEST_F(ViewportToolbarModelTest, TheSnapCommandTurnsEverySwitchOffWhenAnyIsOnOtherwiseOnAndKeepsTheSteps) {
        Controls().snapAngle = true;
        Controls().angleStep = 30.0f;
        _model->Update();
        ASSERT_TRUE(_model->GetGizmoSettings().snap.angle);

        EXPECT_TRUE(_model->RunCommand(EditorCommandIds::kViewportSnap.View(), {}));
        Gizmo::SnapSettings snap = _model->GetGizmoSettings().snap;
        EXPECT_FALSE(snap.position || snap.angle || snap.scale);
        EXPECT_FALSE(Updated().snapAngle);

        EXPECT_TRUE(_model->RunCommand(EditorCommandIds::kViewportSnap.View(), {}));
        snap = _model->GetGizmoSettings().snap;
        EXPECT_TRUE(snap.position && snap.angle && snap.scale);
        EXPECT_EQ(snap.angleStep, 30.0f);
        EXPECT_EQ(snap.positionStep, Gizmo::kDefaultPositionStep);
    }

    TEST_F(ViewportToolbarModelTest, TheKeysSetTheToolAndFlipTheSpace) {
        _model->SetTool(Gizmo::Tool::Rotate);
        _model->ToggleSpace();
        EXPECT_EQ(_model->GetGizmoSettings().tool, Gizmo::Tool::Rotate);
        EXPECT_EQ(_model->GetGizmoSettings().space, Gizmo::Space::Local);
        const ViewportToolbarView& view = Updated();
        EXPECT_EQ(view.tool, Gizmo::Tool::Rotate);
        EXPECT_EQ(view.space, Gizmo::Space::Local);
    }

    TEST_F(ViewportToolbarModelTest, TheModelIsTheRegisteredToolbarWhileItLivesAndTheDefaultsServeWithoutIt) {
        ViewportToolbarRegistry& registry = ViewportToolbarRegistry::GetInstance();
        EXPECT_EQ(registry.Get(), _model.Get());
        EXPECT_EQ(&registry.GetOrDefault(), _model.Get());

        _model.Reset();
        EXPECT_EQ(registry.Get(), nullptr);
        const IViewportToolbar& fallback = registry.GetOrDefault();
        EXPECT_EQ(fallback.GetPreview(), PreviewSettings{});
        EXPECT_EQ(fallback.GetInputMode(), ViewportInputMode::Play);
        const ViewportGizmoSettings gizmo = fallback.GetGizmoSettings();
        EXPECT_EQ(gizmo.tool, Gizmo::Tool::Translate);
        EXPECT_EQ(gizmo.space, Gizmo::Space::World);
        EXPECT_FALSE(gizmo.snap.position || gizmo.snap.angle || gizmo.snap.scale);
    }

    TEST_F(ViewportToolbarModelTest, TheViewRowsAreAutoDefaultAndEachVariantByItsNameOrNumber) {
        const ViewportToolbarView& view = Updated();
        EXPECT_TRUE(view.hasScene);
        EXPECT_TRUE(view.hasViews);
        ASSERT_EQ(view.viewItems.size(), 4u);
        EXPECT_NE(view.viewItems[0].find("Auto"), String::npos);
        EXPECT_NE(view.viewItems[1].find("Default"), String::npos);
        EXPECT_NE(view.viewItems[2].find("Portrait"), String::npos);
        EXPECT_NE(view.viewItems[3].find("#1"), String::npos);

        _scenes->SetActiveScene("Plain"_intern);
        EXPECT_TRUE(Updated().hasScene);
        EXPECT_FALSE(view.hasViews);
        EXPECT_EQ(view.viewItems.size(), 2u);

        EngineEnvironment::UseSceneManager({});
        EXPECT_FALSE(Updated().hasScene);
        EXPECT_FALSE(view.hasViews);
        EXPECT_TRUE(view.viewText.empty());
    }

    TEST_F(ViewportToolbarModelTest, AChosenViewIsForcedOnlyInTheSceneItWasChosenIn) {
        EXPECT_EQ(_model->GetForcedView(&Active()), std::nullopt);
        Controls().viewRow = kFirstVariantRow + 1;
        EXPECT_EQ(Updated().viewRow, kFirstVariantRow + 1);
        EXPECT_EQ(_model->GetForcedView(&Active()), Optional<int32_t>(1));
        Controls().viewRow = kDefaultRow;
        _model->Update();
        EXPECT_EQ(_model->GetForcedView(&Active()), Optional<int32_t>(-1));
        EXPECT_EQ(_model->GetForcedView(_scenes->GetSceneByName("Plain"_intern)), std::nullopt);
        EXPECT_EQ(_model->GetForcedView(nullptr), std::nullopt);

        // Another scene drops the choice, so coming back is Auto
        _scenes->SetActiveScene("Plain"_intern);
        EXPECT_EQ(Updated().viewRow, kAutoRow);
        _scenes->SetActiveScene("Responsive"_intern);
        EXPECT_EQ(Updated().viewRow, kAutoRow);
        EXPECT_EQ(_model->GetForcedView(&Active()), std::nullopt);
    }

    TEST_F(ViewportToolbarModelTest, AForcedVariantTheSceneNoLongerHasFallsBackToAuto) {
        Controls().viewRow = kFirstVariantRow + 1;
        _model->Update();
        ASSERT_EQ(_model->GetForcedView(&Active()), Optional<int32_t>(1));

        UseScenes(LoadScenes(kFewerVariantsXml));
        EXPECT_EQ(_model->GetForcedView(&Active()), std::nullopt);
        const ViewportToolbarView& view = Updated();
        EXPECT_EQ(view.viewRow, kAutoRow);
        EXPECT_EQ(view.viewItems.size(), 3u);
    }

    TEST_F(ViewportToolbarModelTest, TheViewTextIsTheViewTheSceneDrewWithLast) {
        EXPECT_EQ(Updated().viewText, "View: default 1280 x 720");

        const IntrusivePtr<RecordingRenderer> renderer = New<RecordingRenderer>();
        CoreManager::GetRendererManager().SetRenderer(renderer);
        Active().SetViewOverride(0);
        _scenes->DrawAll();
        Active().SetViewOverride(std::nullopt);
        CoreManager::GetRendererManager().SetRenderer({});

        EXPECT_EQ(Updated().viewText, "View: Portrait 720 x 1280");
    }

    TEST_F(ViewportToolbarModelTest, ThePublishedCameraGivesTheZoomTextAndEnablesReset) {
        _model->PublishCamera(49.6f, false);
        EXPECT_EQ(_model->GetView().zoomText, " 50%");
        EXPECT_FALSE(_model->GetView().cameraMoved);
        _model->PublishCamera(150.0f, true);
        EXPECT_EQ(_model->GetView().zoomText, "150%");
        EXPECT_TRUE(_model->GetView().cameraMoved);
        ExpectNoAllocations([this] { _model->PublishCamera(150.2f, true); });
    }

    TEST_F(ViewportToolbarModelTest, TheCanvasRowsFollowThePublishedCanvasesAndASwitchDisablesItsNode) {
        PublishCanvasesOf(Plain());
        const ViewportToolbarView& view = _model->GetView();
        EXPECT_TRUE(view.hasCanvases);
        ASSERT_EQ(view.canvases.size(), 2u);
        EXPECT_EQ(view.canvases[0].name, "Hud");
        EXPECT_EQ(view.canvases[1].name, "Menu");
        EXPECT_TRUE(view.canvases[0].enabled);

        Controls().canvases[0].enabled = false;
        _model->Update();
        EXPECT_FALSE(_model->IsCanvasEnabled(CanvasNode(0)));
        EXPECT_TRUE(_model->IsCanvasEnabled(CanvasNode(1)));
        EXPECT_TRUE(_model->IsCanvasEnabled(&Plain().GetRootNode()));

        // The same list again keeps the rows, and the pass reads the switch through the model
        PublishCanvasesOf(Plain());
        EXPECT_FALSE(view.canvases[0].enabled);

        Controls().canvases[0].enabled = true;
        _model->Update();
        EXPECT_TRUE(_model->IsCanvasEnabled(CanvasNode(0)));
    }

    TEST_F(ViewportToolbarModelTest, ASwitchClickedInTheFrameOfANewCanvasListIsKept) {
        PublishCanvasesOf(Plain());
        Controls().canvases[1].enabled = false;

        const CanvasSwitchRow onlyMenu[] = {{CanvasNode(1), "Menu"_intern}};
        _model->PublishCanvases(&Plain(), onlyMenu);
        EXPECT_FALSE(_model->IsCanvasEnabled(CanvasNode(1)));
        ASSERT_EQ(_model->GetView().canvases.size(), 1u);
        EXPECT_FALSE(_model->GetView().canvases[0].enabled);
    }

    TEST_F(ViewportToolbarModelTest, ACanvasListOfAnotherSceneForgetsTheSwitches) {
        PublishCanvasesOf(Plain());
        Controls().canvases[0].enabled = false;
        _model->Update();
        ASSERT_FALSE(_model->IsCanvasEnabled(CanvasNode(0)));

        PublishCanvasesOf(Active());
        EXPECT_FALSE(_model->GetView().hasCanvases);
        EXPECT_TRUE(_model->IsCanvasEnabled(CanvasNode(0)));

        PublishCanvasesOf(Plain());
        EXPECT_TRUE(_model->GetView().canvases[0].enabled);
    }

    TEST_F(ViewportToolbarModelTest, TheShapeMaskAndLandscapeFollowTheDeviceRows) {
        ASSERT_EQ(_model->GetView().devices.size(), std::min(UIPreview::GetDevices().size(), kMaxShapeDevices));
        ASSERT_GE(DeviceCount(), 3);
        EXPECT_EQ(_model->GetShapeMask(), 0u);
        EXPECT_FALSE(_model->IsLandscapeShapes());

        Controls().devices[0].checked = true;
        Controls().devices[2].checked = true;
        Controls().landscape = true;
        _model->Update();
        EXPECT_EQ(_model->GetShapeMask(), 0b101u);
        EXPECT_TRUE(_model->IsLandscapeShapes());
    }

    TEST_F(ViewportToolbarModelTest, ASteadyUpdateAllocatesNothing) {
        Controls().sizeRow = kFirstDeviceRow;
        Controls().viewRow = kFirstVariantRow;
        PublishCanvasesOf(Plain());
        Controls().canvases[0].enabled = false;
        _model->Update();
        ExpectNoAllocations([this] {
            for (int i = 0; i < 4; ++i) {
                _model->Update();
                PublishCanvasesOf(Plain());
            }
        });
    }

}  // namespace BECore::Tests
