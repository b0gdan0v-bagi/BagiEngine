#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Scene/Components/ParticleEmitterComponent.h>
#include <Common/ConfigFilesCheck.h>
#include <Common/ContentFilesCheck.h>
#include <tests/DemoSceneFixture.h>

namespace BECore::Demo::Tests {

    namespace {

        void CollectSpriteIds(const SceneNode& node, Vector<PoolString>& ids) {
            if (auto sprite = node.GetComponent<SpriteRendererComponent>(); sprite && !sprite->_spriteId.Empty()) {
                ids.push_back(sprite->_spriteId);
            }
            if (auto emitter = node.GetComponent<ParticleEmitterComponent>(); emitter && !emitter->_effect.spriteId.Empty()) {
                ids.push_back(emitter->_effect.spriteId);
            }
            for (const auto& child : node.GetChildren()) {
                CollectSpriteIds(*child, ids);
            }
        }

    }  // namespace

    using DemoContentTest = DemoSceneFixture;

    TEST_F(DemoContentTest, TheContentFilesAreAtTheCurrentVersion) {
        BECore::Tests::ExpectContentFilesAtCurrentVersion(DemoRoot() / "config");
    }

    TEST_F(DemoContentTest, TheConfigsAreCleanUnderTheStrictMode) {
        const Vector<BECore::Tests::ConfigReader> readers{
            {"ApplicationConfig", [](IDeserializer& d) { ApplicationConfig{}.Deserialize(d); }},
            {"SceneConfig", [](IDeserializer& d) { New<SceneManager>()->Deserialize(d); }},
            {"TextureLibrary", [](IDeserializer& d) { TextureLibrary{}.Load(d); }},
        };
        BECore::Tests::ExpectConfigFilesClean(DemoRoot() / "config", readers, {});
    }

    TEST_F(DemoContentTest, EverySpriteOfTheSceneIsInTheLibrary) {
        LoadScene();
        Vector<PoolString> ids;
        CollectSpriteIds(GetScene().GetRootNode(), ids);
        ASSERT_FALSE(ids.empty());
        for (const PoolString id : ids) {
            EXPECT_NE(CoreManager::GetTextureLibrary().GetSprite(id), nullptr) << id.CStr();
        }
    }

    TEST_F(DemoContentTest, EveryTextureOfTheLibraryIsAFileOfTheDemo) {
        TextureLibrary library;
        LoadSpriteLibrary(library);
        ASSERT_FALSE(library.GetAll().empty());
        for (const SpriteEntry& entry : library.GetAll()) {
            EXPECT_TRUE(BECore::Tests::IsFile(DemoRoot() / entry.texturePath.ToStringView())) << entry.name.CStr() << ": " << entry.texturePath.CStr();
        }
    }

}  // namespace BECore::Demo::Tests
