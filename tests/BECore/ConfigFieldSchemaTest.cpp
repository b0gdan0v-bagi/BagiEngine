#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/ILogSink.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/ConfigEditor/ConfigEditorWidget.h>
#include <Widgets/ConfigEditor/ConfigFieldSchema.h>

namespace BECore::Tests {

    namespace {

        const PoolString kLoggerConfig = "LoggerConfig"_intern;
        constexpr StringView kSinkTypePath = "sink/@type";
        constexpr StringView kSinkMinLevelPath = "sink/@minLevel";

        bool Contains(const eastl::vector<String>& options, StringView value) {
            return eastl::find(options.begin(), options.end(), value) != options.end();
        }

        /// The `<sink>` elements of the shipped config/LoggerConfig.xml. Read only: never save it.
        eastl::vector<XmlNode> ShippedSinks() {
            eastl::vector<XmlNode> sinks;
            const XmlNode sinksNode = CoreManager::GetConfigManager().GetConfig(kLoggerConfig).GetChild("sinks");
            for (const XmlNode sink : sinksNode.Children()) {
                if (sink.Name() == "sink") {
                    sinks.push_back(sink);
                }
            }
            return sinks;
        }

    }  // namespace

    class ConfigFieldSchemaTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        // The registry is a process-wide singleton: drop what a case registered
        void TearDown() override {
            ConfigFieldSchema::DestroyInstance();
        }
    };

    TEST_F(ConfigFieldSchemaTest, FactoryTypesAreTheClassNamesSorted) {
        ConfigFieldSchema& schema = ConfigFieldSchema::GetInstance();
        schema.RegisterFactoryTypes<ILogSink>(kLoggerConfig, kSinkTypePath);

        const FieldHint hint = schema.Resolve(kLoggerConfig, kSinkTypePath, "ConsoleSink");
        EXPECT_EQ(hint.type, FieldType::Enum);

        const eastl::vector<String> expected{"ConsoleSink", "FileSink", "OutputSink"};
        EXPECT_EQ(hint.options, expected);

        // The names of the removed hand-written enum are not class names, the loader would drop them
        EXPECT_FALSE(Contains(hint.options, "Console"));
        EXPECT_FALSE(Contains(hint.options, "File"));
        EXPECT_FALSE(Contains(hint.options, "Output"));
    }

    TEST_F(ConfigFieldSchemaTest, EveryFactoryTypeIsCreatableByTheName) {
        ConfigFieldSchema& schema = ConfigFieldSchema::GetInstance();
        schema.RegisterFactoryTypes<ILogSink>(kLoggerConfig, kSinkTypePath);

        const FieldHint hint = schema.Resolve(kLoggerConfig, kSinkTypePath, "");
        ASSERT_FALSE(hint.options.empty());
        for (const String& option : hint.options) {
            const auto sink = AbstractFactory<ILogSink>::GetInstance().Create(StringView{option.c_str(), option.size()});
            EXPECT_TRUE(sink) << option.c_str();
        }
    }

    TEST_F(ConfigFieldSchemaTest, ShippedLoggerConfigUsesOnlyOfferedValues) {
        ConfigFieldSchema& schema = ConfigFieldSchema::GetInstance();
        schema.RegisterFactoryTypes<ILogSink>(kLoggerConfig, kSinkTypePath);
        schema.RegisterEnum<LogLevel>(kLoggerConfig, kSinkMinLevelPath);

        const FieldHint types = schema.Resolve(kLoggerConfig, kSinkTypePath, "");
        const FieldHint levels = schema.Resolve(kLoggerConfig, kSinkMinLevelPath, "");
        EXPECT_EQ(levels.type, FieldType::Enum);

        const eastl::vector<XmlNode> sinks = ShippedSinks();
        ASSERT_FALSE(sinks.empty());
        for (const XmlNode& sink : sinks) {
            const auto type = sink.GetAttribute("type");
            ASSERT_TRUE(type.has_value());
            EXPECT_TRUE(Contains(types.options, *type)) << "type: " << String{type->data(), type->size()}.c_str();

            if (const auto minLevel = sink.GetAttribute("minLevel")) {
                EXPECT_TRUE(Contains(levels.options, *minLevel)) << "minLevel: " << String{minLevel->data(), minLevel->size()}.c_str();
            }
        }
    }

    TEST_F(ConfigFieldSchemaTest, ConfigEditorRegistersTheLoggerFields) {
        // The widget's own registration, not a copy of it: Initialize() fills the singleton
        XmlDeserializer deserializer;
        auto widget = New<ConfigEditorWidget>();
        ASSERT_TRUE(widget->Initialize(deserializer));

        const ConfigFieldSchema& schema = ConfigFieldSchema::GetInstance();
        const FieldHint types = schema.Resolve(kLoggerConfig, kSinkTypePath, "ConsoleSink");
        EXPECT_EQ(types.type, FieldType::Enum);
        const eastl::vector<String> expectedTypes{"ConsoleSink", "FileSink", "OutputSink"};
        EXPECT_EQ(types.options, expectedTypes);

        const FieldHint levels = schema.Resolve(kLoggerConfig, kSinkMinLevelPath, "Debug");
        EXPECT_EQ(levels.type, FieldType::Enum);
        EXPECT_EQ(levels.options.size(), EnumUtils<LogLevel>::Count());
        for (const StringView name : EnumUtils<LogLevel>::Names()) {
            EXPECT_TRUE(Contains(levels.options, name)) << String{name.data(), name.size()}.c_str();
        }
    }

    TEST_F(ConfigFieldSchemaTest, UnregisteredFieldsKeepTheHeuristics) {
        ConfigFieldSchema& schema = ConfigFieldSchema::GetInstance();
        schema.RegisterFactoryTypes<ILogSink>(kLoggerConfig, kSinkTypePath);

        // Another path of the same config, and the same path of another config, are not the registered field
        EXPECT_EQ(schema.Resolve(kLoggerConfig, "sink/@filename", "engine.log").type, FieldType::String);
        EXPECT_EQ(schema.Resolve("OtherConfig"_intern, kSinkTypePath, "ConsoleSink").type, FieldType::String);
        EXPECT_EQ(schema.Resolve("OtherConfig"_intern, kSinkTypePath, "true").type, FieldType::Bool);
        EXPECT_EQ(schema.Resolve("OtherConfig"_intern, kSinkTypePath, "42").type, FieldType::Int);

        // A value that is not among the options of the registered field still resolves to the enum, so the editor can show it
        const FieldHint unknown = schema.Resolve(kLoggerConfig, kSinkTypePath, "NoSuchSink");
        EXPECT_EQ(unknown.type, FieldType::Enum);
        EXPECT_FALSE(Contains(unknown.options, "NoSuchSink"));
    }

}  // namespace BECore::Tests
