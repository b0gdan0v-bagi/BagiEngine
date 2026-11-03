#include "WidgetListTest.h"

#include <BECore/Application/RunMode.h>
#include <BECore/Config/ConfigFactory.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Tests/TestList.h>
#include <BECore/Widgets/WidgetList.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Generated/WidgetListTest.gen.hpp>

namespace BECore::Tests {

    bool ProbeWidget::Initialize(IDeserializer& deserializer) {
        ++initializeCalls;
        deserializer.ReadAttribute("fail", _fail);
        return !_fail;
    }

    namespace {

        constexpr int32_t kFirstId = 1;
        constexpr int32_t kLastId = 4;

        bool Contains(eastl::string_view text, eastl::string_view part) {
            return text.find(part) != eastl::string_view::npos;
        }

        /// Loads the XML into a fresh holder; the deserializer stays with the caller to read its errors
        template <typename TList>
        auto Load(XmlDeserializer& deserializer, const char* xml) {
            EXPECT_TRUE(deserializer.LoadFromString(xml));
            auto list = New<TList>();
            list->Deserialize(deserializer);
            return list;
        }

        /// The `type` of every <widget> in the config named @p configName, in file order
        eastl::vector<eastl::string> WidgetTypes(PoolString configName) {
            eastl::vector<eastl::string> types;
            const XmlNode widgets = CoreManager::GetConfigManager().GetConfig(configName).GetChild("widgets");
            for (const XmlNode widget : widgets.Children()) {
                if (const auto type = widget.ParseAttribute<eastl::string_view>("type")) {
                    types.emplace_back(type->data(), type->size());
                }
            }
            return types;
        }

    }  // namespace

    class WidgetListTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ProbeWidget::initializeCalls = 0;
        }
    };

    TEST_F(WidgetListTest, WidgetsLoadInFileOrderAndTheirFieldsAreRead) {
        XmlDeserializer deserializer;
        const auto list = Load<WidgetList>(deserializer,
                                           R"(<root><widgets>
                                                <widget type="ProbeWidget" id="1"/>
                                                <widget type="ProbeWidget" id="4"/>
                                              </widgets></root>)");

        EXPECT_FALSE(deserializer.HasErrors());
        ASSERT_EQ(list->widgets.size(), 2u);
        EXPECT_EQ(list->widgets[0]->Cast<ProbeWidget>()->id, kFirstId);
        EXPECT_EQ(list->widgets[1]->Cast<ProbeWidget>()->id, kLastId);
        EXPECT_EQ(ProbeWidget::initializeCalls, 2);
    }

    TEST_F(WidgetListTest, AnUnknownTypeIsDroppedAndReportedOnce) {
        XmlDeserializer deserializer;
        const auto list = Load<WidgetList>(deserializer,
                                           R"(<root><widgets>
                                                <widget type="ProbeWidget" id="1"/>
                                                <widget type="NoSuchWidget"/>
                                                <widget type="ProbeWidget" id="4"/>
                                              </widgets></root>)");

        ASSERT_EQ(deserializer.GetErrors().size(), 1u);
        const eastl::string_view message = deserializer.GetErrors()[0].errorMessage.ToStringView();
        EXPECT_TRUE(Contains(message, "Unknown factory type")) << message.data();
        EXPECT_TRUE(Contains(message, "'NoSuchWidget'")) << message.data();
        ASSERT_EQ(list->widgets.size(), 2u);
        EXPECT_EQ(list->widgets[1]->Cast<ProbeWidget>()->id, kLastId);
    }

    TEST_F(WidgetListTest, AWidgetWithoutATypeIsDroppedWithoutAMessage) {
        XmlDeserializer deserializer;
        const auto list = Load<WidgetList>(deserializer,
                                           R"(<root><widgets>
                                                <widget id="2"/>
                                                <widget type="ProbeWidget" id="1"/>
                                              </widgets></root>)");

        EXPECT_FALSE(deserializer.HasErrors());
        ASSERT_EQ(list->widgets.size(), 1u);
        EXPECT_EQ(list->widgets[0]->Cast<ProbeWidget>()->id, kFirstId);
    }

    TEST_F(WidgetListTest, ADisabledWidgetIsSkippedWithoutInitialize) {
        XmlDeserializer deserializer;
        const auto list = Load<WidgetList>(deserializer,
                                           R"(<root><widgets>
                                                <widget type="ProbeWidget" id="2" enabled="false"/>
                                                <widget type="ProbeWidget" id="1"/>
                                              </widgets></root>)");

        EXPECT_FALSE(deserializer.HasErrors());
        ASSERT_EQ(list->widgets.size(), 1u);
        EXPECT_EQ(list->widgets[0]->Cast<ProbeWidget>()->id, kFirstId);
        EXPECT_EQ(ProbeWidget::initializeCalls, 1);
    }

    TEST_F(WidgetListTest, AWidgetWhoseInitializeFailsIsDroppedAndReported) {
        XmlDeserializer deserializer;
        const auto list = Load<WidgetList>(deserializer,
                                           R"(<root><widgets>
                                                <widget type="ProbeWidget" id="1"/>
                                                <widget type="ProbeWidget" id="2" fail="true"/>
                                                <widget type="ProbeWidget" id="4"/>
                                              </widgets></root>)");

        ASSERT_EQ(deserializer.GetErrors().size(), 1u);
        const eastl::string_view message = deserializer.GetErrors()[0].errorMessage.ToStringView();
        EXPECT_TRUE(Contains(message, "ProbeWidget")) << message.data();
        EXPECT_TRUE(Contains(message, "failed to initialize")) << message.data();
        ASSERT_EQ(list->widgets.size(), 2u);
        EXPECT_EQ(list->widgets[0]->Cast<ProbeWidget>()->id, kFirstId);
        EXPECT_EQ(list->widgets[1]->Cast<ProbeWidget>()->id, kLastId);
    }

    TEST_F(WidgetListTest, AnXmlCommentBetweenEntriesCreatesNoNullEntry) {
        XmlDeserializer deserializer;
        const auto list = Load<WidgetList>(deserializer,
                                           R"(<root><widgets>
                                                <!-- first -->
                                                <widget type="ProbeWidget" id="1"/>
                                                <!-- second -->
                                                <widget type="ProbeWidget" id="4"/>
                                              </widgets></root>)");

        EXPECT_FALSE(deserializer.HasErrors());
        ASSERT_EQ(list->widgets.size(), 2u);
        EXPECT_NE(list->widgets[0].Get(), nullptr);
        EXPECT_NE(list->widgets[1].Get(), nullptr);
    }

    /// The widget classes live in RuntimeModule (not registered in this executable) and the Editor ones build ImGui,
    /// so the types of the files are compared. BagiMatch3ControlSmoke runs the Game set for real.
    TEST_F(WidgetListTest, GameModeConfigListsTheClearScreenAndSceneRenderWidgetsInOrder) {
        const auto types = WidgetTypes(GetWidgetsConfigName(RunMode::Game));

        const eastl::vector<eastl::string> expected{"ClearScreenWidget", "SceneRenderWidget"};
        EXPECT_EQ(types, expected);
    }

    TEST_F(WidgetListTest, EditorModeConfigListsTheEditorWidgetsInOrder) {
        const auto types = WidgetTypes(GetWidgetsConfigName(RunMode::Editor));

        const eastl::vector<eastl::string> expected{"ImGuiWidget", "EditorLayoutWidget", "ViewportWidget", "ClearScreenWidget", "ConfigEditorWidget", "SceneEditorWidget", "TextureEditorWidget"};
        EXPECT_EQ(types, expected);
    }

    TEST_F(WidgetListTest, ATestListSkipsDisabledEntriesAndDropsUnknownTypes) {
        XmlDeserializer deserializer;
        const auto list = Load<TestList>(deserializer,
                                         R"(<root><tests>
                                              <test type="ProbeRuntimeTest" id="1" enabled="true"/>
                                              <test type="NoSuchTest" enabled="true"/>
                                              <test type="ProbeRuntimeTest" id="2" enabled="false"/>
                                              <test id="3"/>
                                              <test type="ProbeRuntimeTest" id="4"/>
                                            </tests></root>)");

        ASSERT_EQ(deserializer.GetErrors().size(), 1u);
        const eastl::string_view message = deserializer.GetErrors()[0].errorMessage.ToStringView();
        EXPECT_TRUE(Contains(message, "Unknown factory type")) << message.data();
        EXPECT_TRUE(Contains(message, "'NoSuchTest'")) << message.data();
        ASSERT_EQ(list->tests.size(), 2u);
        EXPECT_EQ(list->tests[0]->Cast<ProbeRuntimeTest>()->id, kFirstId);
        EXPECT_EQ(list->tests[1]->Cast<ProbeRuntimeTest>()->id, kLastId);
    }

    TEST_F(WidgetListTest, TheShippedTestsConfigEnablesTheEngineSmokeTest) {
        const auto list = MakeFromConfig<TestList>("TestsConfig");

        ASSERT_EQ(list->tests.size(), 1u);
        EXPECT_EQ(list->tests[0]->GetName(), "EngineSmokeTest");
    }

}  // namespace BECore::Tests
