#include <BECore/Config/ContentVersion.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneLoader.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        pugi::xml_document Parse(const std::string& text) {
            pugi::xml_document doc;
            const pugi::xml_parse_result result = doc.load_string(text.c_str());
            EXPECT_TRUE(result) << result.description();
            return doc;
        }

        /// A `<node ... contentVersion=V/>` root, the attribute left out when @p version is empty
        pugi::xml_document NodeWith(const std::string& version) {
            return Parse(version.empty() ? "<node name=\"A\"/>" : "<node name=\"A\" contentVersion=\"" + version + "\"/>");
        }

        std::string Message(const pugi::xml_document& doc) {
            const auto result = CheckContentVersion(doc.document_element(), "config/nodes/A.xml");
            return result ? std::string{} : std::string{result.error().c_str()};
        }

        std::string SceneConfigText(const std::string& attribute) {
            return "<SceneManager" + attribute + "><scenes><scene type=\"Scene\" name=\"Main\"><nodes/></scene></scenes></SceneManager>";
        }

        NodeFileLoader NoNodeFiles() {
            return [](StringView, pugi::xml_document&, String& error) {
                error = "no node files in this test";
                return false;
            };
        }

    }  // namespace

    TEST(ContentVersionTest, NoAttributeIsVersionOne) {
        const pugi::xml_document doc = NodeWith("");
        ASSERT_TRUE(GetContentVersion(doc.document_element()));
        EXPECT_EQ(*GetContentVersion(doc.document_element()), kUnversionedContentVersion);
        EXPECT_EQ(kUnversionedContentVersion, 1u);
    }

    TEST(ContentVersionTest, ReadsTheNumber) {
        const pugi::xml_document doc = NodeWith("7");
        ASSERT_TRUE(GetContentVersion(doc.document_element()));
        EXPECT_EQ(*GetContentVersion(doc.document_element()), 7u);
    }

    TEST(ContentVersionTest, ExplicitZeroIsOlderThanAnything) {
        const pugi::xml_document doc = NodeWith("0");
        ASSERT_TRUE(GetContentVersion(doc.document_element()));
        EXPECT_EQ(*GetContentVersion(doc.document_element()), 0u);
        EXPECT_NE(Message(doc).find("content version 0"), std::string::npos);
    }

    TEST(ContentVersionTest, MalformedValueIsAnErrorOfTheFile) {
        for (const char* value : {"", "x", "2x", "-1", "1.5", " 2", "99999999999"}) {
            const pugi::xml_document doc = Parse(std::string("<node name=\"A\" contentVersion=\"") + value + "\"/>");
            EXPECT_FALSE(GetContentVersion(doc.document_element())) << "'" << value << "'";
            const std::string message = Message(doc);
            EXPECT_TRUE(message.starts_with("config/nodes/A.xml: contentVersion '")) << message;
            EXPECT_NE(message.find("is not a whole number"), std::string::npos) << message;
        }
    }

    TEST(ContentVersionTest, CurrentVersionPasses) {
        const pugi::xml_document stamped = NodeWith(std::to_string(kContentVersion));
        EXPECT_TRUE(CheckContentVersion(stamped.document_element(), "x"));
        const pugi::xml_document bare = NodeWith("");
        EXPECT_EQ(CheckContentVersion(bare.document_element(), "x").has_value(), kContentVersion == kUnversionedContentVersion);
    }

    TEST(ContentVersionTest, OlderFileSaysTheVersionsAndTheCommand) {
        ASSERT_GT(kContentVersion, 0u);
        const pugi::xml_document doc = NodeWith(std::to_string(kContentVersion - 1));
        EXPECT_EQ(
            Message(doc),
            Format("config/nodes/A.xml: content version {}, this engine reads {}. Run: python3 engine/tools/migrate/migrate.py <your game folder>", kContentVersion - 1, kContentVersion).c_str());
    }

    TEST(ContentVersionTest, NewerFileSaysToUpdateTheEngine) {
        const pugi::xml_document doc = NodeWith(std::to_string(kContentVersion + 1));
        EXPECT_EQ(Message(doc), Format("config/nodes/A.xml: content version {} is newer than this engine reads ({}): update the engine", kContentVersion + 1, kContentVersion).c_str());
    }

    TEST(ContentVersionTest, TheContentRoots) {
        EXPECT_TRUE(IsContentRoot("SceneManager"));
        EXPECT_TRUE(IsContentRoot("node"));
        EXPECT_FALSE(IsContentRoot("UIConfig"));
        EXPECT_FALSE(IsContentRoot("TextureLibrary"));
        EXPECT_FALSE(IsContentRoot(""));
    }

    /// The stamp on the root of a SceneConfig is no field of any reflected class: the scene loads as without it
    class ContentVersionSceneTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        static bool HasMain(const std::string& text, bool checked) {
            pugi::xml_document doc = Parse(text);
            const XmlNode root{doc.document_element()};
            auto manager = checked ? LoadCheckedSceneManager(root, NoNodeFiles()) : LoadSceneManager(root, NoNodeFiles());
            return manager->GetSceneByName(PoolString::Intern("Main")) != nullptr;
        }
    };

    TEST_F(ContentVersionSceneTest, AStampedSceneConfigLoadsLikeAnUnstampedOne) {
        const std::string stamp = " contentVersion=\"" + std::to_string(kContentVersion) + "\"";
        EXPECT_TRUE(HasMain(SceneConfigText(stamp), false));
        EXPECT_TRUE(HasMain(SceneConfigText(stamp), true));
        EXPECT_TRUE(HasMain(SceneConfigText(""), false));
    }

    TEST_F(ContentVersionSceneTest, TheCheckedLoaderRefusesAnOlderAndANewerSceneConfig) {
        ASSERT_GT(kContentVersion, 0u);
        EXPECT_FALSE(HasMain(SceneConfigText(" contentVersion=\"" + std::to_string(kContentVersion - 1) + "\""), true));
        EXPECT_FALSE(HasMain(SceneConfigText(" contentVersion=\"" + std::to_string(kContentVersion + 1) + "\""), true));
        EXPECT_FALSE(HasMain(SceneConfigText(" contentVersion=\"x\""), true));
    }

    TEST_F(ContentVersionSceneTest, TheUncheckedLoaderReadsAnySceneConfig) {
        EXPECT_TRUE(HasMain(SceneConfigText(" contentVersion=\"0\""), false));
        EXPECT_TRUE(HasMain(SceneConfigText(" contentVersion=\"999\""), false));
    }

    TEST_F(ContentVersionSceneTest, ABareSceneConfigIsVersionOne) {
        EXPECT_EQ(HasMain(SceneConfigText(""), true), kContentVersion == kUnversionedContentVersion);
    }

}  // namespace BECore::Tests
