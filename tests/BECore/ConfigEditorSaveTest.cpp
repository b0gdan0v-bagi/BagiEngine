#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/XmlDocument.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Logger/FileSink.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/ConfigEvents.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/ConfigEditor/ConfigEditorPanelModel.h>
#include <Widgets/ConfigEditor/ReflectedConfigDocument.h>
#include <Widgets/ConfigEditor/StagedChanges.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kLoggerStem = "LoggerConfig";

        constexpr int kOutputPriority = 7;
        constexpr int kFilePriority = 100;

        /// The `<sink>` elements of a logger config document, in file order
        Vector<pugi::xml_node> SinkElements(const XmlDocument& document) {
            Vector<pugi::xml_node> sinks;
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
                if (StringView{sink.attribute("enabled").as_string("true")} != "false") {
                    ++count;
                }
            }
            return count;
        }

        String TypeNameOf(const ILogSink& sink) {
            const StringView name = sink.GetTypeMeta().typeName;
            return String{name.data(), name.size()};
        }

    }  // namespace

    /**
     * The save path of the Config Editor without ImGui, on its own copy of the logger config in a temporary directory: the
     * edit goes into the config's ReflectedConfigDocument, Apply writes it into the pugixml DOM, XmlDocument::SaveToFile
     * writes the file. ConfigManager::SaveConfig is not used: it would overwrite config/LoggerConfig.xml.
     */
    class ConfigEditorSaveTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            // One directory per test: ctest runs the tests in parallel processes, and SetUp clears the directory
            _dir = TestTempDirectory("BagiEngineConfigEditorSaveTest");
            MakeEmptyDirectory(_dir);
        }

        void TearDown() override {
            RemoveDirectory(_dir);
        }

        Path SavedPath() const {
            return _dir / "LoggerConfig.xml";
        }

        /// The file of the first sink once its type is FileSink (the shipped OutputSink has no file of its own)
        Path FirstLogPath() const {
            return _dir / "first.log";
        }

        Path EngineLogPath() const {
            return _dir / "engine.log";
        }

        /// The shape of config/LoggerConfig.xml, with the files of the FileSinks in the temporary directory. A FileSink
        /// opens its `filename` relative to the working directory, which is inside the repository. The values differ from the
        /// defaults, so a save that loses an attribute shows. The first sink carries a `filename` that OutputSink ignores:
        /// the editor changes its type to FileSink as well, and it must not fall back to ./engine.log.
        String TemplateText() const {
            const String firstLog = String(FirstLogPath().View());
            const String engineLog = String(EngineLogPath().View());
            return Format(R"(<?xml version="1.0" encoding="UTF-8"?>
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
        }

        bool LoadTemplate(XmlDocument& document) const {
            const String xml = TemplateText();
            return document.LoadFromString(StringView{xml.c_str(), xml.size()});
        }

        /// The types the editor offers for a sink
        Vector<const TypeInfo*> OfferedSinkTypes() const {
            XmlDocument dom;
            if (!LoadTemplate(dom)) {
                return {};
            }
            const auto document = ReflectedConfigDocument::Open(kLoggerStem, dom.GetRoot().GetPugiNode());
            if (!document || document->GetLists().size() != 1) {
                return {};
            }
            return document->GetLists().front().types;
        }

        /// Makes the first sink of @p dom a @p type the way the editor does, and applies it to the DOM
        static bool ChangeFirstSink(XmlDocument& dom, const TypeInfo& type) {
            auto document = ReflectedConfigDocument::Open(kLoggerStem, dom.GetRoot().GetPugiNode());
            if (!document || !document->ChangeType(0, 0, type)) {
                return false;
            }
            document->Apply();
            return true;
        }

        /// Plays an edit and the Save button on a fresh copy of the config, then reads the saved file into @p saved
        bool SaveEditedType(const TypeInfo& type, XmlDeserializer& saved) const {
            XmlDocument edited;
            return LoadTemplate(edited) && ChangeFirstSink(edited, type) && edited.SaveToFile(SavedPath()) && saved.LoadFromFile(SavedPath());
        }

    private:
        Path _dir;
    };

    TEST_F(ConfigEditorSaveTest, EveryOfferedSinkTypeSurvivesEditSaveReload) {
        const Vector<const TypeInfo*> offered = OfferedSinkTypes();
        for (const StringView engineSink : {StringView{"ConsoleSink"}, StringView{"FileSink"}, StringView{"LogHistorySink"}, StringView{"OutputSink"}}) {
            EXPECT_TRUE(eastl::any_of(offered.begin(), offered.end(), [engineSink](const TypeInfo* type) { return type->Name() == engineSink; })) << String(engineSink).c_str();
        }

        XmlDocument original;
        ASSERT_TRUE(LoadTemplate(original));
        const size_t enabledSinks = CountEnabledSinks(original);
        ASSERT_EQ(enabledSinks, 2u);

        for (const TypeInfo* type : offered) {
            const String option(type->Name());
            SCOPED_TRACE(option.c_str());

            XmlDeserializer saved;
            ASSERT_TRUE(SaveEditedType(*type, saved));

            // The manager goes out of scope with the iteration: its sinks subscribe to the global LogEvent
            const auto manager = New<LoggerManager>();
            manager->Deserialize(saved);

            EXPECT_FALSE(saved.HasErrors());
            ASSERT_EQ(manager->GetSinks().size(), enabledSinks);
            EXPECT_EQ(TypeNameOf(*manager->GetSinks()[0]), option);
            EXPECT_EQ(TypeNameOf(*manager->GetSinks()[1]), "FileSink");

            for (const auto& sink : manager->GetSinks()) {
                if (const auto* fileSink = dynamic_cast<const FileSink*>(sink.Get())) {
                    EXPECT_TRUE(fileSink->IsOpen());
                }
            }
        }
    }

    TEST_F(ConfigEditorSaveTest, TheSavedFileKeepsTheOtherAttributes) {
        const Vector<const TypeInfo*> offered = OfferedSinkTypes();
        ASSERT_FALSE(offered.empty());

        XmlDocument original;
        ASSERT_TRUE(LoadTemplate(original));
        const Vector<pugi::xml_node> originalSinks = SinkElements(original);
        ASSERT_EQ(originalSinks.size(), 3u);

        for (const TypeInfo* type : offered) {
            const String option(type->Name());
            SCOPED_TRACE(option.c_str());

            XmlDeserializer saved;
            ASSERT_TRUE(SaveEditedType(*type, saved));

            XmlDocument reloaded;
            ASSERT_TRUE(reloaded.LoadFromFile(SavedPath()));
            const Vector<pugi::xml_node> reloadedSinks = SinkElements(reloaded);
            ASSERT_EQ(reloadedSinks.size(), originalSinks.size());
            for (size_t i = 0; i < originalSinks.size(); ++i) {
                size_t attributeCount = 0;
                for (pugi::xml_attribute attr = originalSinks[i].first_attribute(); attr; attr = attr.next_attribute()) {
                    ++attributeCount;
                    if (i == 0 && StringView{attr.name()} == "type") {
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

            RemoveFileIfPresent(EngineLogPath());
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
            EXPECT_TRUE(PathExists(EngineLogPath()));
        }
    }

    TEST_F(ConfigEditorSaveTest, SavingOverTheFileOfAPersonChangesTheEditedLineOnly) {
        const String original = TemplateText();
        WriteFile(SavedPath(), original);

        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(SavedPath()));
        const TypeInfo* fileSink = TypeRegistry::GetInstance().Find("FileSink");
        ASSERT_NE(fileSink, nullptr);
        ASSERT_TRUE(ChangeFirstSink(document, *fileSink));
        ASSERT_TRUE(document.SaveToFile(SavedPath()));

        const String saved = ReadFile(SavedPath());

        // The comments, the disabled sink, the four-space indent and the blank lines of the file are the ones it had: one line differs
        String expected = original;
        const size_t at = expected.find("type=\"OutputSink\"");
        ASSERT_NE(at, String::npos);
        expected.replace(at, StringView("type=\"OutputSink\"").size(), "type=\"FileSink\"");
        EXPECT_EQ(saved, expected);
    }

    TEST_F(ConfigEditorSaveTest, StagedChangesAreFoundByAViewOfAnyText) {
        XmlDocument document;
        const String xml = TemplateText();
        ASSERT_TRUE(document.LoadFromString(StringView{xml.c_str(), xml.size()}));
        const Vector<pugi::xml_node> sinks = SinkElements(document);
        ASSERT_FALSE(sinks.empty());

        StagedChanges staged;
        staged.Set("sinks/sink[0]@type", sinks.front().attribute("type"), "FileSink");

        // A key that is neither a String nor a whole literal: a part of a longer text
        const StringView longer = "prefix sinks/sink[0]@type suffix";
        const StringView key = longer.substr(7, 18);
        EXPECT_TRUE(staged.Has(key));
        EXPECT_FALSE(staged.Has(longer));
        const String* value = staged.Get(key);
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(*value, "FileSink");
        EXPECT_EQ(staged.Get("sinks/sink[0]"), nullptr);

        // Staging the same key again replaces the value and adds no entry
        staged.Set(key, sinks.front().attribute("type"), "ConsoleSink");
        size_t entries = 0;
        staged.ForEach([&](const String&, const String&) { ++entries; });
        EXPECT_EQ(entries, 1u);
        EXPECT_EQ(*staged.Get(key), "ConsoleSink");
    }

    namespace {

        const PoolString kPanelConfig = "PanelConfig"_intern;
        const PoolString kOtherPanelConfig = "OtherPanelConfig"_intern;
        constexpr StringView kPanelText = "<PanelConfig value=\"one\"/>\n";
        constexpr StringView kOtherPanelText = "<OtherPanelConfig value=\"other\"/>\n";
        constexpr StringView kValueKey = "root@value";

        class ChangeCounter : public SubscriptionHolder {
        public:
            ChangeCounter() {
                Subscribe<ConfigEvents::ConfigChangedEvent, &ChangeCounter::OnChanged>(this);
            }

            size_t Count(PoolString name) const {
                size_t count = 0;
                for (const PoolString& seen : _names) {
                    count += seen == name ? 1 : 0;
                }
                return count;
            }

        private:
            void OnChanged(const ConfigEvents::ConfigChangedEvent& event) {
                _names.push_back(event.name);
            }

            Vector<PoolString> _names;
        };

    }  // namespace

    /// The Config Editor over a manager of its own on a `config` folder in a temporary directory: the engine's files are never written
    class ConfigEditorChangeTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("cfg_editor_change");
            MakeEmptyDirectory(_dir);
            WriteFile(_dir / "PanelConfig.xml", kPanelText);
            WriteFile(_dir / "OtherPanelConfig.xml", kOtherPanelText);
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();

            _model = New<ConfigEditorPanelModel>();
            _model->Initialize();
            _model->UseConfigManager(&_manager);
            _model->SelectConfig(kPanelConfig);
        }

        void TearDown() override {
            _model.Reset();
            RemoveDirectory(_dir);
        }

        void StageNewValue(StringView value) {
            const XmlNode root = _manager.GetConfig(kPanelConfig);
            _model->GetStagedChanges().Set(String{kValueKey.data(), kValueKey.size()}, root.GetPugiNode().attribute("value"), value);
        }

        Path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        IntrusivePtr<ConfigEditorPanelModel> _model;
    };

    TEST_F(ConfigEditorChangeTest, SavingThroughTheEditorPublishesTheChangeOnceAndTheEditorStaysQuiet) {
        const ChangeCounter changes;
        StageNewValue("two");

        _model->SaveSelectedConfig();

        EXPECT_EQ(changes.Count(kPanelConfig), 1u);
        EXPECT_NE(ReadFile(_dir / "PanelConfig.xml").find("two"), String::npos);
        EXPECT_TRUE(_model->GetStagedChanges().IsEmpty());
        EXPECT_FALSE(_model->ShowsChangedOnDiskNotice()) << "the event of its own save is not news to the panel";
    }

    TEST_F(ConfigEditorChangeTest, AChangeOfTheSelectedConfigDropsTheStagedEditsAndSaysSo) {
        StageNewValue("two");

        _manager.ReloadConfig(kPanelConfig);  // what a file change on disk does, not the panel's own call

        EXPECT_TRUE(_model->GetStagedChanges().IsEmpty());
        EXPECT_TRUE(_model->ShowsChangedOnDiskNotice());
    }

    TEST_F(ConfigEditorChangeTest, AChangeOfAnotherConfigKeepsTheStagedEdits) {
        StageNewValue("two");

        EXPECT_FALSE(_model->DropStagedForChange(kOtherPanelConfig));
        _manager.ReloadConfig(kOtherPanelConfig);

        EXPECT_FALSE(_model->GetStagedChanges().IsEmpty());
        EXPECT_FALSE(_model->ShowsChangedOnDiskNotice());
    }

    TEST_F(ConfigEditorChangeTest, AChangeWithNothingStagedSaysNothing) {
        EXPECT_FALSE(_model->DropStagedForChange(kPanelConfig));
        EXPECT_FALSE(_model->ShowsChangedOnDiskNotice());
    }

}  // namespace BECore::Tests
