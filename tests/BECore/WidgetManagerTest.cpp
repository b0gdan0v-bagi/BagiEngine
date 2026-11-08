#include "WidgetManagerTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Widgets/WidgetList.h>
#include <BECore/Widgets/WidgetManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Generated/WidgetManagerTest.gen.hpp>

namespace BECore::Tests {

    LifecycleWidget::~LifecycleWidget() {
        log.push_back("destroy:{}"_format(id));
    }

    bool LifecycleWidget::Initialize(IDeserializer&) {
        log.push_back("create:{}"_format(id));
        return true;
    }

    namespace {

        using Log = eastl::vector<eastl::string>;

        /// Game mode has widget 1, Editor mode has widgets 2 and 3
        constexpr const char* kGameXml = R"(<root><widgets><widget type="LifecycleWidget" id="1"/></widgets></root>)";
        constexpr const char* kEditorXml = R"(<root><widgets><widget type="LifecycleWidget" id="2"/><widget type="LifecycleWidget" id="3"/></widgets></root>)";

        /// Builds the widgets of a mode from the XML above, through WidgetList like the config path does
        eastl::vector<IntrusivePtr<IWidget>> LoadTestWidgets(RunMode mode) {
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(mode == RunMode::Game ? kGameXml : kEditorXml));
            auto list = New<WidgetList>();
            list->Deserialize(deserializer);
            eastl::vector<IntrusivePtr<IWidget>> widgets;
            for (const auto& widget : list->widgets) {
                widgets.push_back(widget);
            }
            return widgets;
        }

        /// A renderer that writes "waitIdle" to the widgets' log, to prove when the manager waits for the GPU
        class IdleLoggingRenderer : public RecordingRenderer {
        public:
            void WaitIdle() override {
                LifecycleWidget::log.push_back("waitIdle");
            }
        };

    }  // namespace

    class WidgetManagerTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            LifecycleWidget::log.clear();
            _manager.SetWidgetLoader(LoadTestWidgets);
        }

        void TearDown() override {
            CoreManager::GetRendererManager().SetRenderer({});
        }

        WidgetManager _manager;
    };

    TEST_F(WidgetManagerTest, CreateWidgetsRemembersTheModeAndCreatesThatSet) {
        _manager.CreateWidgets(RunMode::Game);

        EXPECT_EQ(_manager.GetMode(), RunMode::Game);
        EXPECT_EQ(LifecycleWidget::log, (Log{"create:1"}));
    }

    TEST_F(WidgetManagerTest, WidgetsAreDestroyedWithTheirManager) {
        {
            WidgetManager manager;
            manager.SetWidgetLoader(LoadTestWidgets);
            manager.CreateWidgets(RunMode::Editor);
        }

        EXPECT_EQ(LifecycleWidget::log, (Log{"create:2", "create:3", "destroy:2", "destroy:3"}));
    }

#if BE_WITH_EDITOR
    TEST_F(WidgetManagerTest, ARequestChangesNothingUntilItIsApplied) {
        _manager.CreateWidgets(RunMode::Game);
        LifecycleWidget::log.clear();

        _manager.RequestMode(RunMode::Editor);
        _manager.RequestToggle();
        _manager.RequestMode(RunMode::Editor);

        EXPECT_TRUE(_manager.HasPendingMode());
        EXPECT_EQ(_manager.GetMode(), RunMode::Game);
        EXPECT_TRUE(LifecycleWidget::log.empty());
    }

    TEST_F(WidgetManagerTest, ApplyWaitsForTheGpuThenDestroysTheOldWidgetsThenCreatesTheNewOnes) {
        CoreManager::GetRendererManager().SetRenderer(New<IdleLoggingRenderer>());
        _manager.CreateWidgets(RunMode::Game);
        LifecycleWidget::log.clear();

        _manager.RequestToggle();
        EXPECT_TRUE(_manager.ApplyPendingMode());

        EXPECT_EQ(_manager.GetMode(), RunMode::Editor);
        EXPECT_FALSE(_manager.HasPendingMode());
        EXPECT_EQ(LifecycleWidget::log, (Log{"waitIdle", "destroy:1", "create:2", "create:3"}));
    }

    TEST_F(WidgetManagerTest, ApplyWorksWithoutARenderer) {
        _manager.CreateWidgets(RunMode::Game);
        LifecycleWidget::log.clear();

        _manager.RequestToggle();
        EXPECT_TRUE(_manager.ApplyPendingMode());

        EXPECT_EQ(LifecycleWidget::log, (Log{"destroy:1", "create:2", "create:3"}));
    }

    TEST_F(WidgetManagerTest, TwoTogglesInARowEndInTheOriginalMode) {
        _manager.CreateWidgets(RunMode::Game);
        LifecycleWidget::log.clear();

        _manager.RequestToggle();
        _manager.RequestToggle();

        EXPECT_FALSE(_manager.HasPendingMode());
        EXPECT_FALSE(_manager.ApplyPendingMode());
        EXPECT_EQ(_manager.GetMode(), RunMode::Game);
        EXPECT_TRUE(LifecycleWidget::log.empty());
    }

    TEST_F(WidgetManagerTest, TwoAppliedTogglesEndInTheOriginalSet) {
        _manager.CreateWidgets(RunMode::Game);
        LifecycleWidget::log.clear();

        _manager.RequestToggle();
        EXPECT_TRUE(_manager.ApplyPendingMode());
        _manager.RequestToggle();
        EXPECT_TRUE(_manager.ApplyPendingMode());

        EXPECT_EQ(_manager.GetMode(), RunMode::Game);
        EXPECT_EQ(LifecycleWidget::log, (Log{"destroy:1", "create:2", "create:3", "destroy:2", "destroy:3", "create:1"}));
    }

    TEST_F(WidgetManagerTest, ARequestForTheCurrentModeAppliesNothing) {
        _manager.CreateWidgets(RunMode::Game);
        LifecycleWidget::log.clear();

        _manager.RequestMode(RunMode::Game);

        EXPECT_FALSE(_manager.HasPendingMode());
        EXPECT_FALSE(_manager.ApplyPendingMode());
        EXPECT_TRUE(LifecycleWidget::log.empty());
    }

    TEST_F(WidgetManagerTest, ARequestForTheCurrentModeClearsAPendingOne) {
        _manager.CreateWidgets(RunMode::Game);

        _manager.RequestMode(RunMode::Editor);
        _manager.RequestMode(RunMode::Game);

        EXPECT_FALSE(_manager.HasPendingMode());
        EXPECT_FALSE(_manager.ApplyPendingMode());
    }

    TEST_F(WidgetManagerTest, TheLastRequestBeforeTheApplyWins) {
        _manager.CreateWidgets(RunMode::Game);

        _manager.RequestMode(RunMode::Editor);
        _manager.RequestToggle();
        _manager.RequestToggle();

        EXPECT_TRUE(_manager.HasPendingMode());
        EXPECT_TRUE(_manager.ApplyPendingMode());
        EXPECT_EQ(_manager.GetMode(), RunMode::Editor);
    }

    TEST_F(WidgetManagerTest, ANewModeWithNoWidgetsStillDestroysTheOldOnes) {
        _manager.SetWidgetLoader([](RunMode mode) { return mode == RunMode::Game ? LoadTestWidgets(mode) : eastl::vector<IntrusivePtr<IWidget>>{}; });
        _manager.CreateWidgets(RunMode::Game);
        LifecycleWidget::log.clear();

        _manager.RequestToggle();
        EXPECT_TRUE(_manager.ApplyPendingMode());

        EXPECT_EQ(_manager.GetMode(), RunMode::Editor);
        EXPECT_EQ(LifecycleWidget::log, (Log{"destroy:1"}));
    }
#endif

}  // namespace BECore::Tests
