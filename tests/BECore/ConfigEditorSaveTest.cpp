#include <BECore/Config/XmlDocument.h>
#include <BECore/Logger/FileSink.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Logger/OutputSink.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/ConfigEditor/ConfigFieldSchema.h>
#include <Widgets/ConfigEditor/StagedChanges.h>

namespace BECore::Tests {

    namespace {

        const PoolString kLoggerConfig = "LoggerConfig"_intern;
        constexpr eastl::string_view kSinkTypePath = "sink/@type";

        // The key the Config Editor gives the `type` attribute of the first sink: "<nodePath>@<attrName>"
        constexpr eastl::string_view kFirstSinkTypeKey = "root/sinks/sink[0]@type";

        constexpr int kOutputPriority = 7;
        constexpr int kFilePriority = 100;

        /// The `<sink>` elements of a logger config document, in file order
        eastl::vector<pugi::xml_node> SinkElements(const XmlDocument& document) {
            eastl::vector<pugi::xml_node> sinks;
            const XmlNode sinksNode = document.GetRoot().GetChild("sinks");
            for (const XmlNode child : sinksNode.Children()) {
                if (child.Name() == "sink") {
                    sinks.push_back(child.GetPugiNode());
                }
            }
            return sinks;
        }

        /// How many sinks LoggerManager keeps from the document: the ones that are not `enabled="false"`
        size_t CountEnabledSinks(const XmlDocument& document) {
            size_t count = 0;
            for (const pugi::xml_node& sink : SinkElements(document)) {
                if (eastl::string_view{sink.attribute("enabled").as_string("true")} != "false") {
                    ++count;
                }
            }
            return count;
        }

        bool Contains(const eastl::vector<eastl::string>& options, eastl::string_view value) {
            return eastl::find(options.begin(), options.end(), value) != options.end();
        }

        eastl::string TypeNameOf(const ILogSink& sink) {
            const eastl::string_view name = sink.GetTypeMeta().typeName;
            return eastl::string{name.data(), name.size()};
        }

    }  // namespace

    /**
     * The save path of the Config Editor without ImGui: the edit goes into StagedChanges, ApplyTo writes it into the
     * pugixml DOM, XmlDocument::SaveToFile writes the file. The test does that on its own copy of the logger config in
     * a temporary directory. ConfigManager::SaveConfig is not used: it would overwrite config/LoggerConfig.xml.
     */
    class ConfigEditorSaveTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            // One directory per test: ctest runs the tests in parallel processes, and SetUp clears the directory
            _dir = std::filesystem::temp_directory_path() / Format("BagiEngineConfigEditorSaveTest_{}", ::testing::UnitTest::GetInstance()->current_test_info()->name()).c_str();
            std::filesystem::remove_all(_dir);
            std::filesystem::create_directories(_dir);
        }

        void TearDown() override {
            // The registry is a process-wide singleton: drop what a case registered
            ConfigFieldSchema::DestroyInstance();
            std::error_code ignored;
            std::filesystem::remove_all(_dir, ignored);
        }

        std::filesystem::path SavedPath() const {
            return _dir / "LoggerConfig.xml";
        }

        /// The file of the first sink once its type is FileSink (the shipped OutputSink has no file of its own)
        std::filesystem::path FirstLogPath() const {
            return _dir / "first.log";
        }

        std::filesystem::path EngineLogPath() const {
            return _dir / "engine.log";
        }

        /// The shape of config/LoggerConfig.xml, with the files of the FileSinks in the temporary directory. A FileSink
        /// opens its `filename` relative to the working directory, which is inside the repository. The values differ from the
        /// defaults, so a save that loses an attribute shows. The first sink carries a `filename` that OutputSink ignores:
        /// the editor changes its type to FileSink as well, and it must not fall back to ./engine.log.
        bool LoadTemplate(XmlDocument& document) const {
            const std::string firstLog = FirstLogPath().generic_string();
            const std::string engineLog = EngineLogPath().generic_string();
            const eastl::string xml = Format(R"(<?xml version="1.0" encoding="UTF-8"?>
<root>
    <!-- Logger Sinks Configuration -->
    <sinks>
        <!-- Output sink runs first -->
        <sink type="OutputSink" enabled="true" priority="{}" minLevel="Error" filename="{}" />

        <!-- Console sink disabled -->
        <sink type="ConsoleSink" enabled="false" priority="0" minLevel="Debug" colorEnabled="true" />

        <!-- File sink runs after Output -->
        <sink type="FileSink" enabled="true" priority="{}" minLevel="Warning" filename="{}" append="false" />
    </sinks>
</root>
)",
                                             kOutputPriority, firstLog, kFilePriority, engineLog);
            return document.LoadFromString(eastl::string_view{xml.c_str(), xml.size()});
        }

        /// Plays the Save button on a fresh copy of the config: stages @p type as the `type` of the first sink, applies it to
        /// the DOM, saves the file and reads it back into @p saved. @p edited receives the document as it was saved.
        bool SaveEditedType(eastl::string_view type, XmlDocument& edited, XmlDeserializer& saved) const {
            if (!LoadTemplate(edited)) {
                return false;
            }
            const eastl::vector<pugi::xml_node> sinks = SinkElements(edited);
            if (sinks.empty()) {
                return false;
            }

            StagedChanges staged;
            staged.Set(eastl::string{kFirstSinkTypeKey.data(), kFirstSinkTypeKey.size()}, sinks.front().attribute("type"), type);
            staged.ApplyTo(edited.GetRoot().GetPugiNode());

            return edited.SaveToFile(SavedPath()) && saved.LoadFromFile(SavedPath());
        }

        FieldHint OfferedSinkTypes() const {
            ConfigFieldSchema& schema = ConfigFieldSchema::GetInstance();
            schema.RegisterFactoryTypes<ILogSink>(kLoggerConfig, kSinkTypePath);
            return schema.Resolve(kLoggerConfig, kSinkTypePath, "OutputSink");
        }

    private:
        std::filesystem::path _dir;
    };

    TEST_F(ConfigEditorSaveTest, EveryOfferedSinkTypeSurvivesEditSaveReload) {
        const FieldHint offered = OfferedSinkTypes();
        ASSERT_EQ(offered.type, FieldType::Enum);
        ASSERT_FALSE(offered.options.empty());

        XmlDocument original;
        ASSERT_TRUE(LoadTemplate(original));
        const size_t enabledSinks = CountEnabledSinks(original);
        ASSERT_EQ(enabledSinks, 2u);

        for (const eastl::string& option : offered.options) {
            SCOPED_TRACE(option.c_str());

            XmlDocument edited;
            XmlDeserializer saved;
            ASSERT_TRUE(SaveEditedType(eastl::string_view{option.c_str(), option.size()}, edited, saved));

            // The manager goes out of scope with the iteration: its sinks subscribe to the global LogEvent
            const auto manager = New<LoggerManager>();
            manager->Deserialize(saved);

            EXPECT_FALSE(saved.HasErrors());
            ASSERT_EQ(manager->GetSinks().size(), enabledSinks);
            EXPECT_EQ(TypeNameOf(*manager->GetSinks()[0]), option);
            EXPECT_EQ(TypeNameOf(*manager->GetSinks()[1]), "FileSink");

            // A file sink the loader built is working: it has opened its file
            for (const auto& sink : manager->GetSinks()) {
                if (const auto* fileSink = dynamic_cast<const FileSink*>(sink.Get())) {
                    EXPECT_TRUE(fileSink->IsOpen());
                }
            }
        }
    }

    TEST_F(ConfigEditorSaveTest, TheSavedFileKeepsTheOtherAttributes) {
        const FieldHint offered = OfferedSinkTypes();
        ASSERT_FALSE(offered.options.empty());

        XmlDocument original;
        ASSERT_TRUE(LoadTemplate(original));
        const eastl::vector<pugi::xml_node> originalSinks = SinkElements(original);
        ASSERT_EQ(originalSinks.size(), 3u);

        for (const eastl::string& option : offered.options) {
            SCOPED_TRACE(option.c_str());

            XmlDocument edited;
            XmlDeserializer saved;
            ASSERT_TRUE(SaveEditedType(eastl::string_view{option.c_str(), option.size()}, edited, saved));

            // In the file: only the `type` of the first sink differs from the copy that was edited
            XmlDocument reloaded;
            ASSERT_TRUE(reloaded.LoadFromFile(SavedPath()));
            const eastl::vector<pugi::xml_node> reloadedSinks = SinkElements(reloaded);
            ASSERT_EQ(reloadedSinks.size(), originalSinks.size());
            for (size_t i = 0; i < originalSinks.size(); ++i) {
                size_t attributeCount = 0;
                for (pugi::xml_attribute attr = originalSinks[i].first_attribute(); attr; attr = attr.next_attribute()) {
                    ++attributeCount;
                    if (i == 0 && eastl::string_view{attr.name()} == "type") {
                        EXPECT_STREQ(reloadedSinks[i].attribute("type").value(), option.c_str());
                    } else {
                        EXPECT_STREQ(reloadedSinks[i].attribute(attr.name()).value(), attr.value()) << "sink " << i << " attribute " << attr.name();
                    }
                }
                size_t reloadedAttributeCount = 0;
                for (pugi::xml_attribute attr = reloadedSinks[i].first_attribute(); attr; attr = attr.next_attribute()) {
                    ++reloadedAttributeCount;
                }
                EXPECT_EQ(reloadedAttributeCount, attributeCount) << "sink " << i;
            }

            // In the manager: the edited sink keeps its priority and level, the file sink its settings and its file
            std::filesystem::remove(EngineLogPath());
            const auto manager = New<LoggerManager>();
            manager->Deserialize(saved);

            ASSERT_EQ(manager->GetSinks().size(), 2u);
            const ILogSink& first = *manager->GetSinks()[0];
            EXPECT_EQ(first.GetPriority(), kOutputPriority);
            EXPECT_EQ(first.GetMinLevel(), LogLevel::Error);

            const ILogSink& second = *manager->GetSinks()[1];
            EXPECT_EQ(TypeNameOf(second), "FileSink");
            EXPECT_EQ(second.GetPriority(), kFilePriority);
            EXPECT_EQ(second.GetMinLevel(), LogLevel::Warning);
            const auto* fileSink = dynamic_cast<const FileSink*>(&second);
            ASSERT_NE(fileSink, nullptr);
            EXPECT_TRUE(fileSink->IsOpen());
            // The sink opened the `filename` of the saved file, not the default ./engine.log
            EXPECT_TRUE(std::filesystem::exists(EngineLogPath()));
        }
    }

    TEST_F(ConfigEditorSaveTest, AValueOutsideTheDropdownIsReportedOnReload) {
        const FieldHint offered = OfferedSinkTypes();
        // The old, wrong enum name: it is not offered, and the loader cannot resolve it
        ASSERT_FALSE(Contains(offered.options, "Console"));

        XmlDocument edited;
        XmlDeserializer saved;
        ASSERT_TRUE(SaveEditedType("Console", edited, saved));

        const auto manager = New<LoggerManager>();
        manager->Deserialize(saved);

        ASSERT_TRUE(saved.HasErrors());
        ASSERT_EQ(saved.GetErrors().size(), 1u);
        EXPECT_EQ(saved.GetErrors()[0].path.ToStringView(), "sinks[0].type");
        EXPECT_EQ(saved.GetErrors()[0].errorMessage.ToStringView(), "Unknown factory type: 'Console'");

        // The entry is dropped, the FileSink after it stays
        EXPECT_EQ(manager->GetSink<OutputSink>(), nullptr);
        ASSERT_EQ(manager->GetSinks().size(), 1u);
        EXPECT_EQ(TypeNameOf(*manager->GetSinks()[0]), "FileSink");
    }

}  // namespace BECore::Tests
