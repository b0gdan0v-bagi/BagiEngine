// The descriptions of the component fields (SceneEditor/FieldHelp): which place answers, the two files on disk, and the file the engine ships.

#include "FieldHelpTest.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/IComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Generated/FieldHelpTest.gen.hpp>
#include <Widgets/SceneEditor/FieldHelp.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kProbe = "HelpProbeComponent";
        constexpr StringView kBase = "HelpProbeBase";
        constexpr StringView kEngineFile = "FieldHelpConfig.xml";
        constexpr StringView kGameFile = "GameFieldHelpConfig.xml";
        /// A component of the engine that only some builds have
        constexpr StringView kRmlUiComponent = "RmlUiDocumentComponent";

        String Entry(StringView component, StringView field, StringView text) {
            return Format("<item component=\"{}\" field=\"{}\" text=\"{}\"/>", component, field, text);
        }

        String HelpFile(StringView entries) {
            return Format("<root><entries>{}</entries></root>", entries);
        }

        class FieldHelpTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                MakeDirectory(_engine / "config");
                MakeDirectory(_game / "config");
                _files.AddContentRoot(_engine);
                _files.AddContentRoot(_game);
            }

            [[nodiscard]] Path EngineFile() const {
                return _engine / "config" / kEngineFile;
            }

            [[nodiscard]] Path GameFile() const {
                return _game / "config" / kGameFile;
            }

            /// The table over the layers as they are now
            [[nodiscard]] FieldHelp Load() {
                _files.Initialize();
                FieldHelp help(_files);
                help.Reload();
                return help;
            }

            static void ExpectAnswer(const FieldHelp& help, StringView component, StringView field, StringView text, FieldHelpSource source) {
                const FieldHelpAnswer answer = help.Find(component, field);
                EXPECT_EQ(answer.text, text) << String(component).c_str() << "." << String(field).c_str();
                EXPECT_EQ(answer.source, source) << String(component).c_str() << "." << String(field).c_str();
            }

            TestTempFolder _folder{"FieldHelp"};
            Path _engine{_folder.PathOf("engine")};
            Path _game{_folder.PathOf("game")};
            FileSystem _files;
        };

    }  // namespace

    TEST_F(FieldHelpTest, TheOwnClassWinsOverAParentAndTheGameOverTheEngine) {
        WriteFile(EngineFile(), HelpFile(Entry(kBase, "speed", "engine base") + Entry(kBase, "active", "engine base active") + Entry(kProbe, "active", "engine own active") +
                                         Entry(kProbe, "count", "engine count") + Entry(kProbe, "title", "")));
        WriteFile(GameFile(), HelpFile(Entry(kBase, "active", "game base active") + Entry(kProbe, "count", "game count")));
        const FieldHelp help = Load();

        ExpectAnswer(help, kProbe, "speed", "engine base", FieldHelpSource::Engine);
        ExpectAnswer(help, kProbe, "active", "engine own active", FieldHelpSource::Engine);
        ExpectAnswer(help, kBase, "active", "game base active", FieldHelpSource::Game);
        ExpectAnswer(help, kProbe, "count", "game count", FieldHelpSource::Game);
        ExpectAnswer(help, kProbe, "title", "", FieldHelpSource::None);
        ExpectAnswer(help, kProbe, "no such field", "", FieldHelpSource::None);
    }

    TEST_F(FieldHelpTest, TheTooltipAttributeAnswersOnlyWithoutAnEntry) {
        FieldHelp help = Load();
        ExpectAnswer(help, kProbe, "speed", "How fast it goes", FieldHelpSource::Attribute);
        ExpectAnswer(help, kProbe, "count", "", FieldHelpSource::None);

        help.Set(FieldHelpFile::Engine, kBase, "speed", "from the table");
        ExpectAnswer(help, kProbe, "speed", "from the table", FieldHelpSource::Engine);
    }

    TEST_F(FieldHelpTest, SetAndRemoveAreSavedSortedAndReadBack) {
        const LogRecorder log;
        FieldHelp help = Load();
        const uint64_t loaded = help.GetVersion();

        help.Set(FieldHelpFile::Game, kProbe, "title", "two\nlines of \"text\" <here>");
        help.Set(FieldHelpFile::Game, kProbe, "count", "how many");
        help.Set(FieldHelpFile::Game, kBase, "speed", "the speed");
        const uint64_t changed = help.GetVersion();
        EXPECT_GT(changed, loaded);
        help.Set(FieldHelpFile::Game, kProbe, "count", "how many");
        EXPECT_FALSE(help.Remove(FieldHelpFile::Engine, kProbe, "count"));
        EXPECT_EQ(help.GetVersion(), changed) << "nothing changed";

        EXPECT_FALSE(PathExists(GameFile()));
        ASSERT_TRUE(help.Save(FieldHelpFile::Game));
        EXPECT_FALSE(PathExists(EngineFile())) << "only the file that was asked for is written";
        const String saved = ReadFile(GameFile());
        const size_t base = saved.find("HelpProbeBase");
        const size_t count = saved.find("field=\"count\"");
        const size_t title = saved.find("field=\"title\"");
        EXPECT_LT(base, count) << saved.c_str();
        EXPECT_LT(count, title) << saved.c_str();
        EXPECT_NE(title, String::npos) << saved.c_str();

        // The file the table just made is in the layers at once
        help.Reload();
        EXPECT_GT(help.GetVersion(), changed);
        ExpectAnswer(help, kProbe, "title", "two\nlines of \"text\" <here>", FieldHelpSource::Game);
        ExpectAnswer(help, kProbe, "speed", "the speed", FieldHelpSource::Game);

        const uint64_t reloaded = help.GetVersion();
        EXPECT_TRUE(help.Remove(FieldHelpFile::Game, kProbe, "count"));
        EXPECT_GT(help.GetVersion(), reloaded);
        ASSERT_TRUE(help.Save(FieldHelpFile::Game));
        help.Reload();
        ExpectAnswer(help, kProbe, "count", "", FieldHelpSource::None);
        ExpectAnswer(help, kProbe, "speed", "the speed", FieldHelpSource::Game);
        EXPECT_EQ(log.Count(LogLevel::Warning, {"FieldHelp"}), 0u) << "a missing file is not worth a word";
    }

    TEST_F(FieldHelpTest, AFileThatCannotBeReadIsOneWarningAndIsNotSavedOver) {
        const String broken = "<root><entries><item component=\"HelpProbeComponent\"";
        WriteFile(EngineFile(), HelpFile(Entry(kProbe, "count", "engine count")));
        WriteFile(GameFile(), broken);
        const LogRecorder log;
        FieldHelp help = Load();
        LogEvent::Flush();

        EXPECT_EQ(log.Count(LogLevel::Warning, {"FieldHelp", "GameFieldHelpConfig.xml"}), 1u);
        ExpectAnswer(help, kProbe, "count", "engine count", FieldHelpSource::Engine);
        help.Set(FieldHelpFile::Game, kProbe, "title", "new");
        EXPECT_FALSE(help.Save(FieldHelpFile::Game));
        EXPECT_EQ(ReadFile(GameFile()), broken);

        // Mended on disk but not read again: what the table holds is still not the file's content
        const String mended = HelpFile(Entry(kProbe, "count", "game count"));
        WriteFile(GameFile(), mended);
        EXPECT_FALSE(help.Save(FieldHelpFile::Game));
        EXPECT_EQ(ReadFile(GameFile()), mended);

        // Broken after a good read: a save would drop what the person is writing
        help.Reload();
        WriteFile(GameFile(), broken);
        EXPECT_FALSE(help.Save(FieldHelpFile::Game));
        EXPECT_EQ(ReadFile(GameFile()), broken);
    }

    TEST_F(FieldHelpTest, AContentOverlayGetsTheWrite) {
        const String original = HelpFile(Entry(kProbe, "count", "shipped"));
        WriteFile(EngineFile(), original);
        const Path overlay{_folder.PathOf("overlay")};
        ASSERT_TRUE(_files.SetContentOverlay(overlay));
        FieldHelp help = Load();

        help.Set(FieldHelpFile::Engine, kProbe, "count", "edited");
        ASSERT_TRUE(help.Save(FieldHelpFile::Engine));
        help.Set(FieldHelpFile::Game, kProbe, "title", "the game's");
        ASSERT_TRUE(help.Save(FieldHelpFile::Game));

        EXPECT_EQ(ReadFile(EngineFile()), original);
        EXPECT_FALSE(PathExists(GameFile()));
        EXPECT_TRUE(IsFile(overlay / "config" / kEngineFile));
        EXPECT_TRUE(IsFile(overlay / "config" / kGameFile));
        help.Reload();
        ExpectAnswer(help, kProbe, "count", "edited", FieldHelpSource::Engine);
        ExpectAnswer(help, kProbe, "title", "the game's", FieldHelpSource::Game);
    }

    TEST_F(FieldHelpTest, TheShippedFileDescribesFieldsThatExist) {
        const Path path = Path(__FILE__).Parent().Parent().Parent() / "config" / kEngineFile;
        XmlDeserializer reader;
        ASSERT_TRUE(reader.LoadFromFile(path)) << path.CStr();
        FieldHelpConfig config;
        config.Deserialize(reader);
        ASSERT_FALSE(reader.HasErrors());
        ASSERT_FALSE(config.entries.empty());

        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* const component = registry.Find(IComponent::GetStaticTypeMeta().typeHash);
        ASSERT_NE(component, nullptr);
        FieldHelp help;
        for (const FieldHelpEntry& entry : config.entries) {
            const String name = Format("{}.{}", entry.component.ToStringView(), entry.field.ToStringView());
            EXPECT_FALSE(entry.text.empty()) << name.c_str();
            help.Set(FieldHelpFile::Engine, entry.component.ToStringView(), entry.field.ToStringView(), StringView(entry.text.data(), entry.text.size()));
            const TypeInfo* const type = registry.Find(entry.component.ToStringView());
            if (type == nullptr) {
                EXPECT_EQ(entry.component.ToStringView(), kRmlUiComponent) << name.c_str() << " names no registered class";
                continue;
            }
            EXPECT_TRUE(registry.IsDerivedFrom(*type, *component)) << name.c_str();
            const FieldDesc* const field = FindReflectedField(*type, entry.field.ToStringView());
            if (field == nullptr) {
                ADD_FAILURE() << name.c_str() << " names no field of its class";
                continue;
            }
            EXPECT_FALSE(field->attributes.hidden) << name.c_str() << " is never shown";
        }

        // A class the file describes is described whole, so a new field of it asks for its line
        for (const FieldHelpEntry& entry : config.entries) {
            if (const TypeInfo* const type = registry.Find(entry.component.ToStringView())) {
                for (const FieldDesc& field : type->fields) {
                    EXPECT_TRUE(field.attributes.hidden || help.Find(type->Name(), field.name).source == FieldHelpSource::Engine)
                        << entry.component.ToStringView().data() << "." << String(field.name).c_str() << " has no description";
                }
            }
        }
    }

}  // namespace BECore::Tests
