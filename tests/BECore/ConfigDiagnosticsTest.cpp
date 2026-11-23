#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Application/CommandLine.h>
#include <BECore/Config/ConfigFactory.h>
#include <BECore/Config/ContentVersion.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Tests/TestList.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kTestsConfig = "TestsConfig";
        constexpr StringView kApplicationConfig = "ApplicationConfig";

        // One letter off `tests`, the child element of the root of TestsConfig
        constexpr const char* kTypoElement = "testz";
        // One letter off `renderer`, an attribute of ApplicationConfig
        constexpr const char* kTypoAttribute = "rendrer";

        constexpr StringView kSceneWithTypo = "<SceneManager><scenes><scene type=\"Scene\" name=\"Main\" nmae=\"x\"><nodes/></scene></scenes></SceneManager>";

        Vector<String> CopyOf(Span<const String> arguments) {
            return Vector<String>(arguments.begin(), arguments.end());
        }

    }  // namespace

    /**
     * The config path of the engine reads with diagnostics on: the cached documents of the ConfigManager get a stray key
     * for a test and lose it in TearDown. The command line is restored too.
     */
    class ConfigDiagnosticsTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _arguments = CopyOf(CoreManager::GetCommandLine().GetArguments());
        }

        void TearDown() override {
            CoreManager::GetCommandLine().Set(_arguments);
            for (auto& [node, name] : _strayKeys) {
                node.remove_child(name);
                node.remove_attribute(name);
            }
        }

        static void SetStrictConfig() {
            const Vector<String> arguments{String(kStrictConfigFlag)};
            CoreManager::GetCommandLine().Set(arguments);
        }

        /// Adds an element and an attribute that no reflected struct knows to the cached document of @p config
        void AddStrayKey(StringView config, const char* name) {
            pugi::xml_node root = CoreManager::GetConfigManager().GetConfig(config).GetPugiNode();
            ASSERT_TRUE(root);
            if (StringView(name) == kTypoAttribute) {
                root.append_attribute(name) = "x";
            } else {
                root.append_child(name);
            }
            _strayKeys.push_back({root, name});
        }

        Vector<String> _arguments;
        Vector<Pair<pugi::xml_node, const char*>> _strayKeys;
    };

    TEST_F(ConfigDiagnosticsTest, TheModeOfTheConfigPathIsWarnAndErrorWithTheStrictFlag) {
        EXPECT_EQ(ConfigManager::GetDiagnostics(), DiagnosticsMode::Warn);
        SetStrictConfig();
        EXPECT_EQ(ConfigManager::GetDiagnostics(), DiagnosticsMode::Error);
    }

    TEST_F(ConfigDiagnosticsTest, CreateDeserializerHasTheMode) {
        const auto warning = CoreManager::GetConfigManager().CreateDeserializer(kTestsConfig);
        ASSERT_NE(warning, nullptr);
        EXPECT_EQ(warning->GetDiagnostics(), DiagnosticsMode::Warn);
        SetStrictConfig();
        const auto strict = CoreManager::GetConfigManager().CreateDeserializer(kTestsConfig);
        ASSERT_NE(strict, nullptr);
        EXPECT_EQ(strict->GetDiagnostics(), DiagnosticsMode::Error);
    }

    TEST_F(ConfigDiagnosticsTest, ABareDeserializerStaysOff) {
        XmlDeserializer deserializer;
        EXPECT_EQ(deserializer.GetDiagnostics(), DiagnosticsMode::Off);
    }

    TEST_F(ConfigDiagnosticsTest, AnUnknownKeyIsOneWarningWithTheConfigThePathAndTheSuggestion) {
        AddStrayKey(kTestsConfig, kTypoElement);

        LogRecorder recorder;
        const auto list = MakeFromConfig<Tests::TestList>(kTestsConfig);
        LogEvent::Flush();

        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"Config 'TestsConfig'"}), 1u);
        EXPECT_NE(recorder.Find(LogLevel::Warning, {"Config 'TestsConfig'", "testz: Unknown element 'testz'", "did you mean 'tests'?"}), nullptr);
        EXPECT_EQ(recorder.Count(LogLevel::Error, {"Config 'TestsConfig'"}), 0u);
        EXPECT_EQ(list->tests.size(), 1u);  // what could be read is kept
    }

    TEST_F(ConfigDiagnosticsTest, TheStrictFlagMakesTheFindingAnError) {
        AddStrayKey(kTestsConfig, kTypoElement);
        SetStrictConfig();

        LogRecorder recorder;
        MakeFromConfig<Tests::TestList>(kTestsConfig);
        LogEvent::Flush();

        EXPECT_EQ(recorder.Count(LogLevel::Error, {"Config 'TestsConfig'", "testz: Unknown element 'testz'"}), 1u);
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"Config 'TestsConfig'"}), 0u);

        auto deserializer = CoreManager::GetConfigManager().CreateDeserializer(kTestsConfig);
        ASSERT_NE(deserializer, nullptr);
        const auto list = New<Tests::TestList>();
        list->Deserialize(*deserializer);
        deserializer->FinishLoad();
        EXPECT_TRUE(deserializer->HasErrors());
    }

    TEST_F(ConfigDiagnosticsTest, AWarningLeavesHasErrorsFalse) {
        AddStrayKey(kTestsConfig, kTypoElement);

        auto deserializer = CoreManager::GetConfigManager().CreateDeserializer(kTestsConfig);
        ASSERT_NE(deserializer, nullptr);
        const auto list = New<Tests::TestList>();
        list->Deserialize(*deserializer);
        deserializer->FinishLoad();

        EXPECT_FALSE(deserializer->HasErrors());
        EXPECT_EQ(deserializer->GetWarnings().size(), 1u);
    }

    TEST_F(ConfigDiagnosticsTest, ACleanConfigLogsNothing) {
        LogRecorder recorder;
        const auto list = MakeFromConfig<Tests::TestList>(kTestsConfig);
        LogEvent::Flush();

        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"Config 'TestsConfig'"}), 0u);
        EXPECT_EQ(recorder.Count(LogLevel::Error, {"Config 'TestsConfig'"}), 0u);
        EXPECT_EQ(list->tests.size(), 1u);
    }

    TEST_F(ConfigDiagnosticsTest, ApplicationConfigWithATypoCarriesAWarning) {
        AddStrayKey(kApplicationConfig, kTypoAttribute);

        const auto load = LoadApplicationConfig();
        EXPECT_TRUE(load.errors.empty());
        ASSERT_EQ(load.warnings.size(), 1u);
        EXPECT_EQ(load.warnings[0].path, kTypoAttribute);
        EXPECT_NE(load.warnings[0].errorMessage.find("did you mean 'renderer'?"), StringView::npos);
    }

    TEST_F(ConfigDiagnosticsTest, ApplicationConfigWithATypoUnderTheStrictFlagIsAnError) {
        AddStrayKey(kApplicationConfig, kTypoAttribute);
        SetStrictConfig();

        const auto load = LoadApplicationConfig();
        EXPECT_TRUE(load.warnings.empty());
        ASSERT_EQ(load.errors.size(), 1u);
        EXPECT_EQ(load.errors[0].path, kTypoAttribute);
    }

    TEST_F(ConfigDiagnosticsTest, TheShippedApplicationConfigHasNoWarnings) {
        const auto load = LoadApplicationConfig();
        EXPECT_TRUE(load.errors.empty());
        EXPECT_TRUE(load.warnings.empty());
    }

    TEST_F(ConfigDiagnosticsTest, LogDeserializeErrorsLogsWarningsAsWarnings) {
        DeserializeError error{"a.b", "bad value", 0};
        DeserializeError warning{"c", "odd key", 3};

        LogRecorder recorder;
        LogDeserializeErrors("Probe", Span<const DeserializeError>(&error, 1), Span<const DeserializeError>(&warning, 1));
        LogDeserializeErrors("ProbeErrorsOnly", Span<const DeserializeError>(&error, 1));
        LogEvent::Flush();

        const auto* loggedError = recorder.Find(LogLevel::Error, {"Config 'Probe'"});
        ASSERT_NE(loggedError, nullptr);
        EXPECT_EQ(loggedError->message, "Config 'Probe': a.b: bad value");
        const auto* loggedWarning = recorder.Find(LogLevel::Warning, {"Config 'Probe'"});
        ASSERT_NE(loggedWarning, nullptr);
        EXPECT_EQ(loggedWarning->message, "Config 'Probe': c: odd key (line 3)");
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"ProbeErrorsOnly"}), 0u);
    }

    TEST_F(ConfigDiagnosticsTest, TheSceneLoaderReportsAnUnknownKeyAndKnowsTheContentVersion) {
        pugi::xml_document doc;
        ASSERT_TRUE(doc.load_buffer(kSceneWithTypo.data(), kSceneWithTypo.size()));
        const NodeFileLoader noNodeFiles = [](StringView, pugi::xml_document&, String& error) {
            error = "no node files in this test";
            return false;
        };

        SceneLoadFindings findings;
        LogRecorder recorder;
        LoadSceneManager(XmlNode{doc.document_element()}, noNodeFiles, "ProbeScene", &findings);
        LogEvent::Flush();

        EXPECT_TRUE(findings.errors.empty());
        ASSERT_EQ(findings.warnings.size(), 1u);
        EXPECT_EQ(findings.warnings[0].path, "scenes[0].nmae");
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"Config 'ProbeScene'", "scenes[0].nmae: Unknown attribute 'nmae'"}), 1u);

        doc.document_element().append_attribute("contentVersion") = ToString(kContentVersion).c_str();
        SceneLoadFindings stamped;
        LoadSceneManager(XmlNode{doc.document_element()}, noNodeFiles, "ProbeScene", &stamped);
        EXPECT_EQ(stamped.warnings.size(), 1u);  // the stamp adds none
    }

    TEST_F(ConfigDiagnosticsTest, AFileOpenedByOpenDeserializerStaysOff) {
        const std::filesystem::path dir = TestTempDirectory("BagiConfigDiagnostics");
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        std::ofstream(dir / "probe.xml") << "<root/>";  //-NoStd: writes a probe file, as ConfigDeserializerTest does
        const PoolString mount = "BagiConfigDiagnosticsTest"_intern;
        ASSERT_TRUE(CoreManager::GetFileSystem().Mount(mount, dir));

        const auto opened = OpenDeserializer("BagiConfigDiagnosticsTest/probe.xml");
        ASSERT_TRUE(opened.has_value());
        EXPECT_EQ((*opened)->GetDiagnostics(), DiagnosticsMode::Off);

        CoreManager::GetFileSystem().Unmount(mount);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

}  // namespace BECore::Tests
