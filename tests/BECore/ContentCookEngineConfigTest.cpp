// The engine's own `config/` folder through the cook: every file is cooked and reads equal, or is copied for a reason written down here. A new
// engine config file that is neither fails the test, so nobody ships a file that the cooked release path silently reads another way.

#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Assert/AssertHandlers.h>
#include <BECore/Cook/ContentVerify.h>
#include <BECore/Logger/LoggerManager.h>
#include <Common/CookTestSupport.h>
#include <Common/Printers.h>
#include <algorithm>

namespace BECore::Tests {

    namespace {

        struct ListedConfig {
            StringView stem;
            StringView reason;
        };

        /// Files a cook copies: no reflected type reads them, so there is no cooker
        constexpr Array<ListedConfig, 5> kCopied = {{
            {"TextConfig", "read by hand: the font and text settings of the text system (entry config-xmlnode-by-hand)"},
            {"UIConfig", "read by hand: the UI environment (entry config-xmlnode-by-hand)"},
            {"WidgetsConfig", "the editor widgets start their runtime in Initialize (ImGui, the user's preferences folder, a hardware monitor thread), which a cook would run; Game mode never reads "
                              "it and a build without the editor cannot read its types"},
            {"FileChangesPanel", "an editor-only panel asset read from XML so that its hot reload works (`WidgetsConfig` is the precedent); a pure `PanelAsset` has no cooker"},
            {"SDLWindowConfig", "the path in ApplicationConfig names the .xml, and the window config is read by SDLWindowConfig::Deserialize from that file"},
        }};

        /// Files whose widget types only the application executable registers, so the executable of this test cannot read them: `<exe> --cook-verify`
        /// checks them (the ctest entry of each executable that has the widgets)
        constexpr Array<ListedConfig, 1> kApplicationOnly = {{
            {"GameWidgetsConfig", "a WidgetList of widgets that only the application executable registers"},
        }};

        /// Cookers that no file of the engine's `config/` feeds: a game's file or a library the engine ships none of
        constexpr Array<StringView, 2> kWithoutShippedFile = {"GamesConfig", "TextureLibrary"};

        Path EngineConfig() {
            return Path(__FILE__).Parent().Parent().Parent() / "config";
        }

        template <typename T>
        bool IsListed(const T& list, StringView stem) {
            for (const auto& entry : list) {
                if constexpr (std::is_same_v<std::decay_t<decltype(entry)>, StringView>) {
                    if (entry == stem) {
                        return true;
                    }
                } else if (entry.stem == stem) {
                    return true;
                }
            }
            return false;
        }

        template <typename T>
        IntrusivePtr<T> ReadAs(ConfigManager& configs, StringView stem) {
            const auto reader = configs.CreateDeserializer(stem);
            EXPECT_NE(reader, nullptr) << String(stem).c_str();
            auto object = New<T>();
            if (reader) {
                const ValueLinkScope scope(stem);
                object->Deserialize(*reader);
                reader->FinishLoad();
                EXPECT_FALSE(reader->HasErrors());
            }
            return object;
        }

    }  // namespace

    /// The engine's `config/` copied into layer 0 of a tree of its own, without the files of `kApplicationOnly`, cooked with the registry of the program
    class ContentCookEngineConfigTest : public ::testing::Test {
    protected:
        void SetUp() override {
            String error;
            for (const Path& file : ListContentFiles(EngineConfig(), error)) {
                const String stem(PathStem(file.View()));
                _shipped.push_back(stem);
                if (!IsListed(kApplicationOnly, stem)) {
                    _tree.Write(0, file.View(), ReadFile(EngineConfig() / file.View()));
                }
            }
            ASSERT_TRUE(error.empty()) << error.c_str();
            _cook = CookContent(_tree.Sources(), _tree.Out(), ContentCookRegistry::GetInstance());
        }

        bool IsShipped(StringView stem) const {
            return std::find(_shipped.begin(), _shipped.end(), String(stem)) != _shipped.end();
        }

        CookTestTree _tree;
        Vector<String> _shipped;
        CookReport _cook;
    };

    TEST_F(ContentCookEngineConfigTest, EveryEngineConfigIsCookedAndEqualOrCopiedForAListedReason) {
        ASSERT_TRUE(_cook.Ok()) << _cook.ToText().c_str();
        const CookReport verified = VerifyCooked(_tree.Sources(), _tree.Out(), ContentCookRegistry::GetInstance());
        EXPECT_TRUE(verified.Ok()) << verified.ToText().c_str();

        for (const CookRow& row : verified.rows) {
            const String stem(Path(StringView(row.path)).Stem().View());
            if (row.outcome == CookOutcome::Copied) {
                EXPECT_TRUE(IsListed(kCopied, stem)) << row.path.c_str() << " is copied but no reader cooks it and no reason is listed here: register a cooker or list it";
            } else {
                EXPECT_FALSE(IsListed(kCopied, stem)) << row.path.c_str() << " is listed as copied but the cook cooks it: remove the entry";
            }
        }
        for (const ListedConfig& listed : kCopied) {
            EXPECT_TRUE(IsShipped(listed.stem)) << listed.stem.data() << " is listed as copied and the engine ships no such file: remove the entry";
        }
        EXPECT_GE(verified.Count(CookOutcome::Cooked), 6u);
    }

    TEST_F(ContentCookEngineConfigTest, TheApplicationExecutableFilesAndTheRegistryAgree) {
        for (const ListedConfig& listed : kApplicationOnly) {
            const bool registered = ContentCookRegistry::GetInstance().Find(listed.stem) != nullptr;
            EXPECT_TRUE(registered) << listed.stem.data();
            EXPECT_TRUE(IsShipped(listed.stem)) << listed.stem.data() << " is not shipped";
        }
        for (const ContentCooker& cooker : ContentCookRegistry::GetInstance().All()) {
            EXPECT_TRUE(IsShipped(cooker.stem) || IsListed(kWithoutShippedFile, cooker.stem)) << cooker.stem.data() << " is registered and nothing feeds it: a stale registrar";
        }
    }

    TEST_F(ContentCookEngineConfigTest, TheApplicationConfigOfTheBinReadsStrictlyToTheSameConfig) {
        ASSERT_TRUE(_cook.Ok()) << _cook.ToText().c_str();
        SourceContent source(_tree);
        CookedContent cooked(_tree.Out(), CookTestTree::kLayers);
        ASSERT_TRUE(cooked.Configs().IsBinaryConfig("ApplicationConfig"));
        const auto fromXml = source.Configs().CreateDeserializer("ApplicationConfig");
        const auto fromBin = cooked.Configs().CreateDeserializer("ApplicationConfig");
        ASSERT_NE(fromXml, nullptr);
        ASSERT_NE(fromBin, nullptr);

        const ApplicationConfigLoad expected = LoadApplicationConfig(*fromXml);
        const ApplicationConfigLoad actual = LoadApplicationConfig(*fromBin);
        EXPECT_TRUE(expected.errors.empty());
        EXPECT_TRUE(actual.errors.empty());
        EXPECT_EQ(actual.config._type, expected.config._type);
        EXPECT_EQ(actual.config._renderer, expected.config._renderer);
        EXPECT_EQ(actual.config._path, expected.config._path);
        EXPECT_EQ(actual.config._mode, expected.config._mode);
        EXPECT_EQ(actual.config._control, expected.config._control);
        EXPECT_EQ(actual.config._controlPort, expected.config._controlPort);
        EXPECT_EQ(actual.config._manualClock, expected.config._manualClock);
        EXPECT_EQ(actual.config._headless, expected.config._headless);
        EXPECT_EQ(actual.config._taskFrameBudgetMs, expected.config._taskFrameBudgetMs);
        EXPECT_EQ(actual.config._fileWatch, expected.config._fileWatch);
        EXPECT_FALSE(expected.config._type.empty()) << "the file says something, so the comparison is not of two defaults";
    }

    TEST_F(ContentCookEngineConfigTest, TheLoggerAndAssertConfigsOfTheBinGiveTheSameSinksAndHandlersInTheSameOrder) {
        ASSERT_TRUE(_cook.Ok()) << _cook.ToText().c_str();
        SourceContent source(_tree);
        CookedContent cooked(_tree.Out(), CookTestTree::kLayers);

        const auto sinksFromXml = ReadAs<LoggerManager>(source.Configs(), "LoggerConfig");
        const auto sinksFromBin = ReadAs<LoggerManager>(cooked.Configs(), "LoggerConfig");
        ASSERT_FALSE(sinksFromXml->GetSinks().empty());
        ASSERT_EQ(sinksFromBin->GetSinks().size(), sinksFromXml->GetSinks().size());
        for (size_t i = 0; i < sinksFromXml->GetSinks().size(); ++i) {
            EXPECT_EQ(sinksFromBin->GetSinks()[i]->GetTypeMeta().typeName, sinksFromXml->GetSinks()[i]->GetTypeMeta().typeName) << "sink " << i;
        }

        const auto handlersFromXml = ReadAs<AssertHandlerManager>(source.Configs(), "AssertHandlersConfig");
        const auto handlersFromBin = ReadAs<AssertHandlerManager>(cooked.Configs(), "AssertHandlersConfig");
        ASSERT_FALSE(handlersFromXml->GetHandlers().empty());
        ASSERT_EQ(handlersFromBin->GetHandlers().size(), handlersFromXml->GetHandlers().size());
        for (size_t i = 0; i < handlersFromXml->GetHandlers().size(); ++i) {
            EXPECT_EQ(handlersFromBin->GetHandlers()[i]->GetTypeMeta().typeName, handlersFromXml->GetHandlers()[i]->GetTypeMeta().typeName) << "handler " << i;
        }
    }

    TEST_F(ContentCookEngineConfigTest, TheTextureLibraryAndAGamesConfigOfAGameCookAndVerifyEqual) {
        _tree.Write(1, "TextureLibrary.xml", R"(<root><sprites>
  <sprite name="Plain" texturePath="assets/a.png"><srcRect x="1" y="2" w="3" h="4"/></sprite>
  <sprite name="Packed" texturePath="assets/atlas.png"><srcRect x="9" y="9" w="4" h="4"/>
    <atlas name="ui"><source texturePath="assets/d.png"><rect x="3" y="3" w="4" h="4"/></source><pack padding="2" sizeMode="Pot" maxSide="1024"/></atlas></sprite>
  <sprite texturePath="assets/nameless.png"/>
</sprites></root>)");
        _tree.Write(1, "GamesConfig.xml", R"(<root><defaultGame name="Chess"/></root>)");

        const CookReport cooked = CookContent(_tree.Sources(), _tree.Out(), ContentCookRegistry::GetInstance());
        ASSERT_TRUE(cooked.Ok()) << cooked.ToText().c_str();
        const CookReport verified = VerifyCooked(_tree.Sources(), _tree.Out(), ContentCookRegistry::GetInstance());
        EXPECT_TRUE(verified.Ok()) << verified.ToText().c_str();
        ASSERT_NE(FindRow(verified, 1, "TextureLibrary.xml"), nullptr);
        EXPECT_EQ(FindRow(verified, 1, "TextureLibrary.xml")->outcome, CookOutcome::Cooked);
        ASSERT_NE(FindRow(verified, 1, "GamesConfig.xml"), nullptr);
        EXPECT_EQ(FindRow(verified, 1, "GamesConfig.xml")->outcome, CookOutcome::Cooked) << "the executable of this test links the Games module";
    }

}  // namespace BECore::Tests
