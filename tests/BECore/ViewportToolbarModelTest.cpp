#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/Viewport/ViewportToolbarModel.h>
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
            _model = New<ViewportToolbarModel>();
            _model->Initialize();
        }

        void TearDown() override {
            _model.Reset();
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

        IntrusivePtr<ViewportToolbarModel> _model;
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

    TEST_F(ViewportToolbarModelTest, TheModelIsTheRegisteredToolbarWhileItLivesAndTheDefaultsServeWithoutIt) {
        ViewportToolbarRegistry& registry = ViewportToolbarRegistry::GetInstance();
        EXPECT_EQ(registry.Get(), _model.Get());
        EXPECT_EQ(&registry.GetOrDefault(), _model.Get());

        _model.Reset();
        EXPECT_EQ(registry.Get(), nullptr);
        const IViewportToolbar& fallback = registry.GetOrDefault();
        EXPECT_EQ(fallback.GetPreview(), PreviewSettings{});
        EXPECT_EQ(fallback.GetInputMode(), ViewportInputMode::Play);
    }

    TEST_F(ViewportToolbarModelTest, ASteadyUpdateAllocatesNothing) {
        Controls().sizeRow = kFirstDeviceRow;
        _model->Update();
        ExpectNoAllocations([this] {
            for (int i = 0; i < 4; ++i) {
                _model->Update();
            }
        });
    }

}  // namespace BECore::Tests
