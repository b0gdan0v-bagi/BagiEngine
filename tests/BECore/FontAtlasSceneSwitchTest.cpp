// What a scene switch costs the glyph atlases: showing a scene a second time finds the atlases the first show made.

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/TextTestSupport.h>
#include <Common/UITestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Two scenes that show text in the shipped font: Menu a scene text and a UI text (with an outline), Game a text of another size and a UI text of the
        // same size as the menu's. "Extra" shows glyphs the preload does not hold. "Spelled" asks for the same file as the menu's text, with a path spelled another way.
        constexpr const char* kSwitchScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Menu"><nodes>
    <node name="Title"><components>
      <component type="TransformComponent" x="10" y="20" width="0" height="0"/>
      <component type="TextComponent" text="Menu" fontSize="24"/>
    </components></node>
    <node name="Hud"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"><elements>
        <element type="UIElement" name="Caption" x="10" y="100" width="200" height="40" fontSize="20"><components>
          <component type="UITextComponent" text="Play" textOutline="2,0,0,0,255"/>
        </components></element>
      </elements></component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Game"><nodes>
    <node name="Score"><components>
      <component type="TransformComponent" x="10" y="20" width="0" height="0"/>
      <component type="TextComponent" text="Score 120" fontSize="32"/>
    </components></node>
    <node name="Hud"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"><elements>
        <element type="UIElement" name="Caption" x="10" y="100" width="200" height="40" fontSize="20"><components>
          <component type="UITextComponent" text="Pause"/>
        </components></element>
      </elements></component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Extra"><nodes>
    <node name="Title"><components>
      <component type="TransformComponent" x="10" y="20" width="0" height="0"/>
      <component type="TextComponent" text="Łódź" fontSize="24"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Spelled"><nodes>
    <node name="Title"><components>
      <component type="TransformComponent" x="10" y="20" width="0" height="0"/>
      <component type="TextComponent" text="Menu" fontSize="24" fontPath="assets/fonts/./PT_Sans-Web-Regular.ttf"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        /// What the atlases cost so far: the library's atlas count and the textures the renderer was asked for
        struct AtlasWork {
            size_t atlases = 0;
            uint32_t textures = 0;

            bool operator==(const AtlasWork&) const = default;
        };

    }  // namespace

    class FontAtlasSceneSwitchTest : public UISceneFixture {
    protected:
        void SetUp() override {
            UISceneFixture::SetUp();
            ASSERT_NO_FATAL_FAILURE(LoadScene(kSwitchScenesXml));
        }

        AtlasWork Work() const {
            return {CoreManager::GetFontAtlasLibrary().GetAtlasCount(), _renderer->texturesCreated};
        }

        /// Shows @p scene for one frame
        void Show(const char* scene) {
            _manager->SetActiveScene(PoolString::Intern(scene));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        /// A scene unload and load: the manager is released and the same scenes are read again
        void ReloadScenes() {
            _manager.Reset();
            ASSERT_NO_FATAL_FAILURE(LoadScene(kSwitchScenesXml));
        }
    };

    TEST_F(FontAtlasSceneSwitchTest, ASecondShowOfASceneMakesNoAtlasWork) {
        Show("Menu");
        Show("Game");
        const AtlasWork afterBoth = Work();
        EXPECT_GT(afterBoth.atlases, 0u);

        Show("Menu");
        Show("Game");

        EXPECT_EQ(Work(), afterBoth);
    }

    TEST_F(FontAtlasSceneSwitchTest, ASecondShowAfterTheScenesWereUnloadedAndLoadedMakesNoAtlasWork) {
        Show("Menu");
        Show("Game");
        const AtlasWork afterBoth = Work();

        ASSERT_NO_FATAL_FAILURE(ReloadScenes());
        Show("Menu");
        Show("Game");

        EXPECT_EQ(Work(), afterBoth);
    }

    TEST_F(FontAtlasSceneSwitchTest, GlyphsOutsideThePreloadAreMadeOnTheFirstShowOnly) {
        Show("Menu");
        const AtlasWork afterMenu = Work();

        Show("Extra");
        const AtlasWork afterFirst = Work();
        EXPECT_GT(afterFirst.textures, afterMenu.textures) << "the case needs glyphs the preload lacks";
        EXPECT_EQ(afterFirst.atlases, afterMenu.atlases);

        Show("Menu");
        Show("Extra");

        EXPECT_EQ(Work(), afterFirst);
    }

    // The library keys an atlas by the Font object, and the resource cache keys a font by the text of its path: a file spelled another way is another
    // font and gets atlases of its own, once. What must not happen is a rebuild on every show.
    TEST_F(FontAtlasSceneSwitchTest, AFileSpelledAnotherWayGetsItsOwnAtlasesOnce) {
        Show("Menu");
        const AtlasWork afterMenu = Work();

        Show("Spelled");
        const AtlasWork afterSpelled = Work();
        EXPECT_GT(afterSpelled.atlases, afterMenu.atlases);

        Show("Menu");
        Show("Spelled");

        EXPECT_EQ(Work(), afterSpelled);
    }

    TEST_F(FontAtlasSceneSwitchTest, AFontLoadedAgainAfterTheCacheWasClearedGetsItsAtlasesOnce) {
        Show("Menu");
        CoreManager::GetResourceManager().ClearCache();
        ASSERT_NO_FATAL_FAILURE(ReloadScenes());
        Show("Menu");
        const AtlasWork afterReload = Work();

        ASSERT_NO_FATAL_FAILURE(ReloadScenes());
        Show("Menu");

        EXPECT_EQ(Work(), afterReload);
    }

}  // namespace BECore::Tests
