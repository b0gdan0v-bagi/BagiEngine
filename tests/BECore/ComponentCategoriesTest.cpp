#include <BECore/File/File.h>
#include <BECore/File/TextReader.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameControlTest.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Games/GameEditorTools.h>
#include <Games/GameManager.h>
#include <Widgets/EditorLayout/ComponentCategories.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// The engine folders that declare components; the registry also holds the components of the tests, which need no category
        constexpr StringView kEngineSourceFolders[] = {"src/Modules/BECore", "src/Modules/RmlUiHost"};
        constexpr StringView kClassMarker = "BE_CLASS(";
        constexpr size_t kMinEngineComponents = 10;

        ClassMeta MetaOfCategory(StringView category) {
            return ClassMeta{1, "CategoryProbe", {}, AssetPurpose::Common, category};
        }

        /// The names the `BE_CLASS` markers of the headers under @p folder declare (a marker in a comment gives a name no type has)
        bool CollectClassNames(StringView folder, Set<String>& names) {
            const Path root = CoreManager::GetFileSystem().ResolvePath(folder);
            if (root.Empty()) {
                return false;
            }
            bool readable = true;
            const auto walked = WalkDirectory(root, WalkMode::Tree, [&](StringView relativePath, FileKind kind) {
                const Path entry(relativePath);
                if (kind != FileKind::File || entry.Extension() != ".h") {
                    return true;
                }
                const auto text = ReadTextFile(root / entry);
                if (!text) {
                    readable = false;
                    return false;
                }
                for (size_t at = text->find(kClassMarker.data()); at != String::npos; at = text->find(kClassMarker.data(), at + 1)) {
                    const size_t start = at + kClassMarker.size();
                    const size_t end = text->find_first_of(",)", start);
                    if (end != String::npos) {
                        names.insert(text->substr(start, end - start));
                    }
                }
                return true;
            });
            return walked.has_value() && readable;
        }

        class ComponentCategoriesTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }
        };

    }  // namespace

    TEST_F(ComponentCategoriesTest, EveryBuiltInCategoryHasAColour) {
        for (const StringView name : kBuiltInComponentCategories) {
            EXPECT_TRUE(CategoryColor(MetaOfCategory(name)).has_value()) << name.data();
        }
    }

    TEST_F(ComponentCategoriesTest, NoCategoryAndAnUnknownOneAreNeutralAndOnlyTheUnknownOneIsReportedOnce) {
        static bool reported = false;
        if (reported) {
            GTEST_SKIP() << "a name is reported once per process";
        }
        reported = true;
        LogRecorder log;

        EXPECT_FALSE(CategoryColor(MetaOfCategory({})).has_value());
        EXPECT_FALSE(CategoryColor(MetaOfCategory("no-such-category")).has_value());
        EXPECT_FALSE(CategoryColor(MetaOfCategory("no-such-category")).has_value());
        LogEvent::Flush();

        EXPECT_EQ(log.Count(LogLevel::Warning, {"names the category"}), 1u);
    }

    TEST_F(ComponentCategoriesTest, AGamesCategoryHasItsColourWhileTheGameIsActive) {
        GameManager& games = GameManager::GetInstance();
        games.Initialize();
        games.SwitchTo("FakePlain"_intern);
        for (const IntrusivePtr<IGame>& game : games.GetAll()) {
            if (auto* fake = game->Cast<FakeControllableGame>()) {
                fake->editorTools = [](GameEditorTools& tools) { tools.AddCategory({"combat", Color(200, 40, 40, 255)}); };
            }
        }
        const ClassMeta combat = MetaOfCategory("combat");

        games.SwitchTo("FakeControllable"_intern);
        const Optional<Color> declared = CategoryColor(combat);
        games.SwitchTo("FakePlain"_intern);
        const Optional<Color> afterTheGame = CategoryColor(combat);
        games.Shutdown();

        ASSERT_TRUE(declared.has_value());
        EXPECT_EQ(declared->r, 200);
        EXPECT_EQ(declared->g, 40);
        EXPECT_FALSE(afterTheGame.has_value());
    }

    TEST_F(ComponentCategoriesTest, EveryComponentOfTheEngineButTheTransformNamesABuiltInCategory) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo& component = Require(registry.Find("IComponent"));
        Set<String> names;
        for (const StringView folder : kEngineSourceFolders) {
            ASSERT_TRUE(CollectClassNames(folder, names)) << "cannot read " << folder.data();
        }

        size_t components = 0;
        for (const String& name : names) {
            const TypeInfo* info = registry.Find(name);
            if (info == nullptr || info == &component || !registry.IsDerivedFrom(*info, component)) {
                continue;
            }
            ++components;
            if (name == "TransformComponent") {
                EXPECT_TRUE(info->meta.category.empty());
                continue;
            }
            EXPECT_TRUE(CategoryColor(info->meta).has_value()) << name.c_str() << " names '" << info->meta.category.data() << "'";
        }
        EXPECT_GE(components, kMinEngineComponents) << "the scan found too few components";
    }

    TEST_F(ComponentCategoriesTest, ANodeHasTheCategoryOfItsFirstComponentThatNamesOne) {
        auto scene = New<Scene>();
        scene->Initialize();
        auto node = scene->CreateNode("Categories"_intern, nullptr);
        EXPECT_EQ(CategoryOfNode(*node), nullptr);
        node->AddComponent(New<TransformComponent>());
        EXPECT_EQ(CategoryOfNode(*node), nullptr);
        EXPECT_FALSE(CategoryColorOfNode(*node).has_value());

        node->AddComponent(New<SpriteRendererComponent>());
        node->AddComponent(New<ClickableComponent>());

        EXPECT_EQ(Require(CategoryOfNode(*node)).category, "render");
        EXPECT_TRUE(CategoryColorOfNode(*node).has_value());
    }

}  // namespace BECore::Tests
