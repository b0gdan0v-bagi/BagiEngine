#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/XmlDocument.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Logger/FileSink.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Logger/OutputSink.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/ConfigEvents.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/ConfigEditor/ConfigEditorWidget.h>
#include <Widgets/ConfigEditor/ConfigFieldSchema.h>
#include <Widgets/ConfigEditor/StagedChanges.h>

namespace BECore::Tests {

    namespace {

        const PoolString kLoggerConfig = "LoggerConfig"_intern;
        constexpr StringView kSinkTypePath = "sink/@type";

        // The key the Config Editor gives the `type` attribute of the first sink: "<nodePath>@<attrName>"
        constexpr StringView kFirstSinkTypeKey = "root/sinks/sink[0]@type";

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

        bool Contains(const Vector<String>& options, StringView value) {
            return eastl::find(options.begin(), options.end(), value) != options.end();
        }

        String TypeNameOf(const ILogSink& sink) {
            const StringView name = sink.GetTypeMeta().typeName;
            return String{name.data(), name.size()};
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
            _dir = TestTempDirectory("BagiEngineConfigEditorSaveTest");
            std::error_code error;
            std::filesystem::remove_all(_dir, error);
            std::filesystem::create_directories(_dir, error);
            ASSERT_FALSE(error) << _dir.generic_string() << ": " << error.message();
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
        String TemplateText() const {
            const String firstLog = FromStdString(FirstLogPath().generic_string());
            const String engineLog = FromStdString(EngineLogPath().generic_string());
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

        /// Plays the Save button on a fresh copy of the config: stages @p type as the `type` of the first sink, applies it to
        /// the DOM, saves the file and reads it back into @p saved. @p edited receives the document as it was saved.
        bool SaveEditedType(StringView type, XmlDocument& edited, XmlDeserializer& saved) const {
            if (!LoadTemplate(edited)) {
                return false;
            }
            const Vector<pugi::xml_node> sinks = SinkElements(edited);
            if (sinks.empty()) {
                return false;
            }

            StagedChanges staged;
            staged.Set(String{kFirstSinkTypeKey.data(), kFirstSinkTypeKey.size()}, sinks.front().attribute("type"), type);
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

        for (const String& option : offered.options) {
            SCOPED_TRACE(option.c_str());

            XmlDocument edited;
            XmlDeserializer saved;
            ASSERT_TRUE(SaveEditedType(StringView{option.c_str(), option.size()}, edited, saved));

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
        const FieldHint offered = OfferedSinkTypes();
        ASSERT_FALSE(offered.options.empty());

        XmlDocument original;
        ASSERT_TRUE(LoadTemplate(original));
        const Vector<pugi::xml_node> originalSinks = SinkElements(original);
        ASSERT_EQ(originalSinks.size(), 3u);

        for (const String& option : offered.options) {
            SCOPED_TRACE(option.c_str());

            XmlDocument edited;
            XmlDeserializer saved;
            ASSERT_TRUE(SaveEditedType(StringView{option.c_str(), option.size()}, edited, saved));

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

            std::error_code error;
            std::filesystem::remove(EngineLogPath(), error);
            ASSERT_FALSE(error) << error.message();
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
            EXPECT_TRUE(std::filesystem::exists(EngineLogPath(), error));
        }
    }

    /// The shape of LoadTemplate in a DOM that keeps its comments, as a config document does once it is loaded with them
    TEST_F(ConfigEditorSaveTest, AStagedEditOfACommentedDocumentKeepsEveryComment) {
        pugi::xml_document document;
        ASSERT_TRUE(document.load_string(R"(<root>
    <!-- Logger Sinks Configuration -->
    <sinks>
        <!-- Output sink runs first -->
        <sink type="OutputSink" priority="7" />
        <!-- Console sink disabled -->
        <sink type="ConsoleSink" enabled="false" />
        <!-- File sink runs after Output -->
        <sink type="FileSink" priority="100" />
    </sinks>
</root>)",
                                         pugi::parse_full));

        // The keys the editor gives the sinks count the elements only: a comment is no sink[0]
        const XmlNode sinks = XmlNode(document.document_element()).GetChild("sinks");
        Vector<String> keys;
        Vector<pugi::xml_node> elements;
        for (const XmlNode sink : sinks.Children()) {
            keys.push_back(Format("root/sinks/sink[{}]@type", keys.size()));
            elements.push_back(sink.GetPugiNode());
        }
        ASSERT_EQ(keys.size(), 3u);
        EXPECT_EQ(keys[0], kFirstSinkTypeKey);

        StagedChanges staged;
        staged.Set(keys[0], elements[0].attribute("type"), "FileSink");
        staged.ApplyTo(document.document_element());

        EXPECT_STREQ(elements[0].attribute("type").value(), "FileSink");
        size_t comments = 0;
        for (const pugi::xml_node node : document.document_element().child("sinks").children()) {
            comments += node.type() == pugi::node_comment ? 1u : 0u;
        }
        EXPECT_EQ(comments, 3u);
        EXPECT_EQ(document.document_element().first_child().type(), pugi::node_comment);
    }

    TEST_F(ConfigEditorSaveTest, SavingOverTheFileOfAPersonChangesTheEditedLineOnly) {
        const String original = TemplateText();
        {
            std::ofstream out(SavedPath(), std::ios::out | std::ios::binary);  //-NoStd: the test writes its own temporary file byte for byte
            out.write(original.data(), static_cast<std::streamsize>(original.size()));
        }

        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(SavedPath()));
        const Vector<pugi::xml_node> sinks = SinkElements(document);
        ASSERT_FALSE(sinks.empty());
        StagedChanges staged;
        staged.Set(String{kFirstSinkTypeKey.data(), kFirstSinkTypeKey.size()}, sinks.front().attribute("type"), "FileSink");
        staged.ApplyTo(document.GetRoot().GetPugiNode());
        ASSERT_TRUE(document.SaveToFile(SavedPath()));

        std::ifstream in(SavedPath(), std::ios::in | std::ios::binary);  //-NoStd: the test reads its own temporary file byte for byte
        String saved;
        saved.resize(static_cast<size_t>(std::filesystem::file_size(SavedPath())));
        in.read(saved.data(), static_cast<std::streamsize>(saved.size()));

        // The comments, the four-space indent and the blank lines of the file are the ones it had: one line differs
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

    TEST_F(ConfigEditorSaveTest, AValueOutsideTheDropdownIsReportedOnReload) {
        const FieldHint offered = OfferedSinkTypes();
        // A name that is not a class name: it is not offered, and the loader cannot resolve it
        ASSERT_FALSE(Contains(offered.options, "Console"));

        XmlDocument edited;
        XmlDeserializer saved;
        ASSERT_TRUE(SaveEditedType("Console", edited, saved));

        const auto manager = New<LoggerManager>();
        manager->Deserialize(saved);

        ASSERT_TRUE(saved.HasErrors());
        ASSERT_EQ(saved.GetErrors().size(), 1u);
        EXPECT_EQ(saved.GetErrors()[0].path, "sinks[0].type");
        EXPECT_EQ(saved.GetErrors()[0].errorMessage, "Unknown factory type: 'Console'");

        EXPECT_EQ(manager->GetSink<OutputSink>(), nullptr);
        ASSERT_EQ(manager->GetSinks().size(), 1u);
        EXPECT_EQ(TypeNameOf(*manager->GetSinks()[0]), "FileSink");
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

            XmlDeserializer deserializer;
            _widget = New<ConfigEditorWidget>();
            ASSERT_TRUE(_widget->Initialize(deserializer));
            _widget->UseConfigManager(&_manager);
            _widget->SelectConfig(kPanelConfig);
        }

        void TearDown() override {
            _widget.Reset();
            ConfigFieldSchema::DestroyInstance();
            RemoveDirectory(_dir);
        }

        void StageNewValue(StringView value) {
            const XmlNode root = _manager.GetConfig(kPanelConfig);
            _widget->GetStagedChanges().Set(String{kValueKey.data(), kValueKey.size()}, root.GetPugiNode().attribute("value"), value);
        }

        std::filesystem::path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        IntrusivePtr<ConfigEditorWidget> _widget;
    };

    TEST_F(ConfigEditorChangeTest, SavingThroughTheEditorPublishesTheChangeOnceAndTheEditorStaysQuiet) {
        const ChangeCounter changes;
        StageNewValue("two");

        _widget->SaveSelectedConfig();

        EXPECT_EQ(changes.Count(kPanelConfig), 1u);
        EXPECT_NE(ReadFile(_dir / "PanelConfig.xml").find("two"), String::npos);
        EXPECT_TRUE(_widget->GetStagedChanges().IsEmpty());
        EXPECT_FALSE(_widget->ShowsChangedOnDiskNotice()) << "the event of its own save is not news to the panel";
    }

    TEST_F(ConfigEditorChangeTest, AChangeOfTheSelectedConfigDropsTheStagedEditsAndSaysSo) {
        StageNewValue("two");

        _manager.ReloadConfig(kPanelConfig);  // what a file change on disk does, not the panel's own call

        EXPECT_TRUE(_widget->GetStagedChanges().IsEmpty());
        EXPECT_TRUE(_widget->ShowsChangedOnDiskNotice());
    }

    TEST_F(ConfigEditorChangeTest, AChangeOfAnotherConfigKeepsTheStagedEdits) {
        StageNewValue("two");

        EXPECT_FALSE(_widget->DropStagedForChange(kOtherPanelConfig));
        _manager.ReloadConfig(kOtherPanelConfig);

        EXPECT_FALSE(_widget->GetStagedChanges().IsEmpty());
        EXPECT_FALSE(_widget->ShowsChangedOnDiskNotice());
    }

    TEST_F(ConfigEditorChangeTest, AChangeWithNothingStagedSaysNothing) {
        EXPECT_FALSE(_widget->DropStagedForChange(kPanelConfig));
        EXPECT_FALSE(_widget->ShowsChangedOnDiskNotice());
    }

}  // namespace BECore::Tests
