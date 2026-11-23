#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/ILogSink.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLUtils.h>
#include <Widgets/ConfigEditor/ConfigEditorWidget.h>
#include <Widgets/ConfigEditor/ConfigFieldSchema.h>
#include <Widgets/ConfigEditor/FlagValue.h>

namespace BECore::Tests {

    namespace {

        const PoolString kLoggerConfig = "LoggerConfig"_intern;
        const PoolString kWindowConfig = "SDLWindowConfig"_intern;
        constexpr StringView kWindowFlagsPath = "window/@windowFlags";
        constexpr StringView kSinkTypePath = "sink/@type";
        constexpr StringView kSinkMinLevelPath = "sink/@minLevel";

        /// The sink types of the engine plus the one LoggerSinkOrderTest registers: every test sink of this executable is in the factory
        const Vector<String> kOfferedSinkTypes{"ConsoleSink", "FileSink", "LogHistorySink", "OrderSink", "OutputSink"};

        bool Contains(const Vector<String>& options, StringView value) {
            return eastl::find(options.begin(), options.end(), value) != options.end();
        }

        /// The `<sink>` elements of the shipped config/LoggerConfig.xml. Read only: never save it.
        Vector<XmlNode> ShippedSinks() {
            Vector<XmlNode> sinks;
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

        EXPECT_EQ(hint.options, kOfferedSinkTypes);

        // A name that is not a class name is not offered: the loader would drop it
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

        const Vector<XmlNode> sinks = ShippedSinks();
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
        EXPECT_EQ(types.options, kOfferedSinkTypes);

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

    TEST_F(ConfigFieldSchemaTest, ResolveAllocatesNothingAndSharesTheHeuristicHints) {
        ConfigFieldSchema& schema = ConfigFieldSchema::GetInstance();
        schema.RegisterFactoryTypes<ILogSink>(kLoggerConfig, kSinkTypePath);
        const PoolString other = "OtherConfig"_intern;
        // The first calls make the scratch arena's chunk
        const FieldHint* intHint = &schema.Resolve(other, "node/@count", "42");
        schema.Resolve(kLoggerConfig, kSinkTypePath, "ConsoleSink");

        ExpectNoAllocations([&] {
            for (const StringView value : {"42", "true", "x", "1.5"}) {
                schema.Resolve(other, "node/@count", value);
            }
            schema.Resolve(kLoggerConfig, kSinkTypePath, "ConsoleSink");
        });
        EXPECT_EQ(&schema.Resolve(other, "node/@other", "7"), intHint);
        EXPECT_EQ(&schema.Resolve(other, "node/@count", "x"), &schema.Resolve(other, "node/@name", "y"));
    }

    TEST_F(ConfigFieldSchemaTest, HasFlagTokenMatchesWholeTrimmedTokens) {
        EXPECT_TRUE(HasFlagToken("A|B", "A"));
        EXPECT_TRUE(HasFlagToken(" A | B ", "B"));
        EXPECT_FALSE(HasFlagToken("AB|C", "A"));
        EXPECT_FALSE(HasFlagToken("A|B", "A|B"));
        EXPECT_FALSE(HasFlagToken("", "A"));
        EXPECT_FALSE(HasFlagToken(" | ", ""));
    }

    TEST_F(ConfigFieldSchemaTest, ForEachUnknownFlagTokenMatchesUnknownFlagTokens) {
        const Vector<String> options{"A", "B"};
        for (const StringView value : {"Z|A|Y|B|Z", "B|A", "", " Z | Z|Y ", "Y||Z|Y"}) {
            Vector<String> seen;
            ForEachUnknownFlagToken(value, options, [&](StringView token) { seen.emplace_back(token.data(), token.size()); });
            EXPECT_EQ(seen, UnknownFlagTokens(value, options)) << String(value.data(), value.size()).c_str();
        }
    }

    TEST_F(ConfigFieldSchemaTest, SplitFlagTokensTrimsAndSkipsEmpty) {
        const Vector<String> expected{"A", "B C", "D"};
        EXPECT_EQ(SplitFlagTokens(" A | B C ||D |"), expected);
        EXPECT_TRUE(SplitFlagTokens("").empty());
        EXPECT_TRUE(SplitFlagTokens(" | ").empty());
    }

    TEST_F(ConfigFieldSchemaTest, UnknownFlagTokensKeepsOrderAndSkipsOptions) {
        const Vector<String> options{"A", "B"};
        const Vector<String> expected{"Z", "Y"};
        EXPECT_EQ(UnknownFlagTokens("Z|A|Y|B|Z", options), expected);
        EXPECT_TRUE(UnknownFlagTokens("B|A", options).empty());
        EXPECT_TRUE(UnknownFlagTokens("", options).empty());
    }

    TEST_F(ConfigFieldSchemaTest, JoinFlagTokensJoinsWithABar) {
        EXPECT_EQ(JoinFlagTokens({"A", "B", "C"}), "A|B|C");
        EXPECT_EQ(JoinFlagTokens({"A"}), "A");
        EXPECT_EQ(JoinFlagTokens({}), "");
    }

    TEST_F(ConfigFieldSchemaTest, WindowFlagNamesAreAllKnownToTheLoader) {
        const Vector<String> names = SDLUtils::GetWindowFlagNames();
        ASSERT_FALSE(names.empty());
        for (const String& name : names) {
            EXPECT_NE(SDLUtils::GetWindowFlagValue(StringView{name.c_str(), name.size()}), 0u) << name.c_str();
        }
        EXPECT_TRUE(Contains(names, "SDL_WINDOW_MOUSE_RELATIVE_MODE"));
    }

    TEST_F(ConfigFieldSchemaTest, WindowFlagsFieldOffersEveryLoaderFlag) {
        XmlDeserializer deserializer;
        auto widget = New<ConfigEditorWidget>();
        ASSERT_TRUE(widget->Initialize(deserializer));

        const FieldHint hint = ConfigFieldSchema::GetInstance().Resolve(kWindowConfig, kWindowFlagsPath, "SDL_WINDOW_RESIZABLE");
        EXPECT_EQ(hint.type, FieldType::Flags);
        EXPECT_EQ(hint.options, SDLUtils::GetWindowFlagNames());
    }

    TEST_F(ConfigFieldSchemaTest, UnregisteredValueWithABarIsAString) {
        const ConfigFieldSchema& schema = ConfigFieldSchema::GetInstance();
        const FieldHint hint = schema.Resolve("OtherConfig"_intern, "node/@mode", "A|B");
        EXPECT_EQ(hint.type, FieldType::String);
        EXPECT_TRUE(hint.options.empty());
    }

    TEST_F(ConfigFieldSchemaTest, RegisteredFlagsStayFlags) {
        ConfigFieldSchema& schema = ConfigFieldSchema::GetInstance();
        const Vector<String> names{"A", "B", "C"};
        schema.RegisterFlags("OtherConfig"_intern, "node/@mode", names);

        // Whatever the value holds, the options are the registered ones
        for (const StringView value : {"A|B", "A", "", "Z"}) {
            const FieldHint hint = schema.Resolve("OtherConfig"_intern, "node/@mode", value);
            EXPECT_EQ(hint.type, FieldType::Flags);
            EXPECT_EQ(hint.options, names);
        }
    }

}  // namespace BECore::Tests
