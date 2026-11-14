#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Components/SpriteFit.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // A tall atlas cell (116.67 x 190.5), the case that started SpriteFit
        constexpr float kCellX = 583.33f;
        constexpr float kCellWidth = 116.67f;
        constexpr float kCellHeight = 190.5f;
        constexpr float kSquare = 80.0f;
        // The width of the cell fitted by height into a square: 116.67 * 80 / 190.5
        constexpr float kFittedWidth = 48.99f;
        constexpr float kTolerance = 0.01f;

        constexpr uint32_t kTextureWidth = 20;
        constexpr uint32_t kTextureHeight = 40;

        // Output 800 x 600. "Plain" has no view size (one unit is one pixel). "Fit" shows 400 x 200 units, so its scale is 2 and its
        // view is moved 100 pixels down.
        constexpr const char* kSpriteScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Plain"><nodes>
    <node name="Stretch"><components>
      <component type="TransformComponent" x="10" y="20" width="80" height="80"/>
      <component type="SpriteRendererComponent"><srcRect x="583.33" y="0" w="116.67" h="190.5"/></component>
    </components></node>
    <node name="Contain"><components>
      <component type="TransformComponent" x="10" y="20" width="80" height="80"/>
      <component type="SpriteRendererComponent" fit="Contain"><srcRect x="583.33" y="0" w="116.67" h="190.5"/></component>
    </components></node>
    <node name="StretchWhole"><components>
      <component type="TransformComponent" x="100" y="100" width="80" height="80"/>
      <component type="SpriteRendererComponent"/>
    </components></node>
    <node name="ContainWhole"><components>
      <component type="TransformComponent" x="100" y="100" width="80" height="80"/>
      <component type="SpriteRendererComponent" fit="Contain"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Fit" viewWidth="400" viewHeight="200"><nodes>
    <node name="Contain"><components>
      <component type="TransformComponent" x="20" y="10" width="100" height="60"/>
      <component type="SpriteRendererComponent" fit="Contain"><srcRect x="0" y="0" w="50" h="50"/></component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        ::testing::AssertionResult RectNear(const Rect& actual, float x, float y, float w, float h) {
            if (std::abs(actual.x - x) <= kTolerance && std::abs(actual.y - y) <= kTolerance && std::abs(actual.w - w) <= kTolerance && std::abs(actual.h - h) <= kTolerance) {
                return ::testing::AssertionSuccess();
            }
            return ::testing::AssertionFailure() << "rect (" << actual.x << ", " << actual.y << ", " << actual.w << ", " << actual.h << ") is not (" << x << ", " << y << ", " << w << ", " << h << ")";
        }

        static_assert(EnumUtils<SpriteFit>::Count() == 2);
        static_assert(EnumUtils<SpriteFit>::FromString("Contain") == SpriteFit::Contain);

    }  // namespace

    TEST(SpriteFitTest, CompileTimeChecks) {
        // The static_asserts at file scope are the checks
        SUCCEED();
    }

    TEST(SpriteFitTest, StretchReturnsTheRect) {
        const Rect fitted = FitSpriteRect(kCellWidth, kCellHeight, Rect{10.0f, 20.0f, 200.0f, 80.0f}, SpriteFit::Stretch);

        EXPECT_TRUE(RectNear(fitted, 10.0f, 20.0f, 200.0f, 80.0f));
    }

    TEST(SpriteFitTest, ContainATallSourceFitsByHeight) {
        const Rect fitted = FitSpriteRect(kCellWidth, kCellHeight, Rect{0.0f, 0.0f, kSquare, kSquare}, SpriteFit::Contain);

        EXPECT_TRUE(RectNear(fitted, (kSquare - kFittedWidth) * 0.5f, 0.0f, kFittedWidth, kSquare));
        EXPECT_NEAR(fitted.w / fitted.h, kCellWidth / kCellHeight, 1e-5f);
    }

    TEST(SpriteFitTest, ContainAWideSourceFitsByWidth) {
        const Rect fitted = FitSpriteRect(200.0f, 100.0f, Rect{0.0f, 0.0f, kSquare, kSquare}, SpriteFit::Contain);

        EXPECT_TRUE(RectNear(fitted, 0.0f, 20.0f, 80.0f, 40.0f));
    }

    TEST(SpriteFitTest, ContainTheSameAspectGivesTheRect) {
        EXPECT_TRUE(RectNear(FitSpriteRect(50.0f, 100.0f, Rect{5.0f, 6.0f, 20.0f, 40.0f}, SpriteFit::Contain), 5.0f, 6.0f, 20.0f, 40.0f));
        EXPECT_TRUE(RectNear(FitSpriteRect(10.0f, 10.0f, Rect{0.0f, 0.0f, kSquare, kSquare}, SpriteFit::Contain), 0.0f, 0.0f, kSquare, kSquare));
    }

    TEST(SpriteFitTest, ContainScalesASmallSourceUp) {
        const Rect fitted = FitSpriteRect(10.0f, 5.0f, Rect{0.0f, 0.0f, kSquare, kSquare}, SpriteFit::Contain);

        EXPECT_TRUE(RectNear(fitted, 0.0f, 20.0f, 80.0f, 40.0f));
    }

    TEST(SpriteFitTest, ContainInANonSquareDestination) {
        // A tall source in a wide rect is fitted by height and centered on x
        EXPECT_TRUE(RectNear(FitSpriteRect(50.0f, 100.0f, Rect{0.0f, 0.0f, 200.0f, 80.0f}, SpriteFit::Contain), 80.0f, 0.0f, 40.0f, 80.0f));
        // A wide source (aspect 2) in the same rect (aspect 2.5) is fitted by height too: the rect is wider than the source
        EXPECT_TRUE(RectNear(FitSpriteRect(100.0f, 50.0f, Rect{0.0f, 0.0f, 200.0f, 80.0f}, SpriteFit::Contain), 20.0f, 0.0f, 160.0f, 80.0f));
        // A very wide source in a tall rect is fitted by width and centered on y
        EXPECT_TRUE(RectNear(FitSpriteRect(300.0f, 100.0f, Rect{0.0f, 0.0f, 60.0f, 200.0f}, SpriteFit::Contain), 0.0f, 90.0f, 60.0f, 20.0f));
    }

    TEST(SpriteFitTest, ContainInADestinationAwayFromTheOrigin) {
        const Rect fitted = FitSpriteRect(kCellWidth, kCellHeight, Rect{100.0f, 50.0f, kSquare, kSquare}, SpriteFit::Contain);

        EXPECT_TRUE(RectNear(fitted, 100.0f + (kSquare - kFittedWidth) * 0.5f, 50.0f, kFittedWidth, kSquare));
    }

    TEST(SpriteFitTest, ASizeThatIsNotPositiveReturnsTheRect) {
        const Rect dst{3.0f, 4.0f, 80.0f, 60.0f};

        EXPECT_TRUE(RectNear(FitSpriteRect(0.0f, 100.0f, dst, SpriteFit::Contain), 3.0f, 4.0f, 80.0f, 60.0f));
        EXPECT_TRUE(RectNear(FitSpriteRect(100.0f, 0.0f, dst, SpriteFit::Contain), 3.0f, 4.0f, 80.0f, 60.0f));
        EXPECT_TRUE(RectNear(FitSpriteRect(-10.0f, 100.0f, dst, SpriteFit::Contain), 3.0f, 4.0f, 80.0f, 60.0f));
        EXPECT_TRUE(RectNear(FitSpriteRect(100.0f, -10.0f, dst, SpriteFit::Contain), 3.0f, 4.0f, 80.0f, 60.0f));
        EXPECT_TRUE(RectNear(FitSpriteRect(50.0f, 100.0f, Rect{3.0f, 4.0f, 0.0f, 60.0f}, SpriteFit::Contain), 3.0f, 4.0f, 0.0f, 60.0f));
        EXPECT_TRUE(RectNear(FitSpriteRect(50.0f, 100.0f, Rect{3.0f, 4.0f, 80.0f, 0.0f}, SpriteFit::Contain), 3.0f, 4.0f, 80.0f, 0.0f));
        EXPECT_TRUE(RectNear(FitSpriteRect(50.0f, 100.0f, Rect{3.0f, 4.0f, -80.0f, 60.0f}, SpriteFit::Contain), 3.0f, 4.0f, -80.0f, 60.0f));
    }

    class SpriteRendererComponentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kSpriteScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        IntrusivePtr<SpriteRendererComponent> Sprite(const char* sceneName, const char* nodeName) const {
            Scene& scene = *_manager->GetSceneByName(PoolString::Intern(sceneName));
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    return node->GetComponent<SpriteRendererComponent>();
                }
            }
            return {};
        }

        /// Gives a sprite a texture of the given size made by the renderer, as a test has no other way to make one draw
        void GiveTexture(const IntrusivePtr<SpriteRendererComponent>& sprite, uint32_t width, uint32_t height) {
            const Vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 255);
            IntrusivePtr<ITexture> texture = _renderer->CreateTexture(pixels.data(), width, height);
            ASSERT_TRUE(texture);
            sprite->SetTexture(texture);
        }

        void Draw(const char* sceneName) {
            _manager->SetActiveScene(PoolString::Intern(sceneName));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(SpriteRendererComponentTest, TheFitComesFromTheSceneConfig) {
        ASSERT_TRUE(Sprite("Plain", "Contain"));
        EXPECT_EQ(Sprite("Plain", "Contain")->_fit, SpriteFit::Contain);
        EXPECT_EQ(Sprite("Fit", "Contain")->_fit, SpriteFit::Contain);
    }

    TEST_F(SpriteRendererComponentTest, TheFitDefaultsToStretch) {
        ASSERT_TRUE(Sprite("Plain", "Stretch"));
        EXPECT_EQ(Sprite("Plain", "Stretch")->_fit, SpriteFit::Stretch);
        EXPECT_EQ(Sprite("Plain", "StretchWhole")->_fit, SpriteFit::Stretch);
    }

    TEST_F(SpriteRendererComponentTest, ASpriteWithoutATextureDrawsNothing) {
        Draw("Plain");

        EXPECT_TRUE(_renderer->calls.empty());
    }

    TEST_F(SpriteRendererComponentTest, StretchCoversTheWholeTransform) {
        ASSERT_NO_FATAL_FAILURE(GiveTexture(Sprite("Plain", "Stretch"), kTextureWidth, kTextureHeight));

        Draw("Plain");

        ASSERT_EQ(_renderer->calls.size(), 1u);
        const DrawCall& call = _renderer->calls[0];
        EXPECT_EQ(call.kind, DrawKind::Texture);
        EXPECT_TRUE(RectNear(call.rect, 10.0f, 20.0f, kSquare, kSquare));
        ASSERT_TRUE(call.source.has_value());
        EXPECT_TRUE(RectNear(*call.source, kCellX, 0.0f, kCellWidth, kCellHeight));
    }

    TEST_F(SpriteRendererComponentTest, ContainCentersTheCellInTheTransform) {
        ASSERT_NO_FATAL_FAILURE(GiveTexture(Sprite("Plain", "Contain"), kTextureWidth, kTextureHeight));

        Draw("Plain");

        ASSERT_EQ(_renderer->calls.size(), 1u);
        const DrawCall& call = _renderer->calls[0];
        EXPECT_EQ(call.kind, DrawKind::Texture);
        EXPECT_TRUE(RectNear(call.rect, 10.0f + (kSquare - kFittedWidth) * 0.5f, 20.0f, kFittedWidth, kSquare));
        // The source rect is not touched by the fit, and the aspect is the one of the cell, not of the 20 x 40 texture
        ASSERT_TRUE(call.source.has_value());
        EXPECT_TRUE(RectNear(*call.source, kCellX, 0.0f, kCellWidth, kCellHeight));
    }

    TEST_F(SpriteRendererComponentTest, AnEmptySourceStretchesTheWholeTexture) {
        ASSERT_NO_FATAL_FAILURE(GiveTexture(Sprite("Plain", "StretchWhole"), kTextureWidth, kTextureHeight));

        Draw("Plain");

        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_TRUE(RectNear(_renderer->calls[0].rect, 100.0f, 100.0f, kSquare, kSquare));
        EXPECT_FALSE(_renderer->calls[0].source.has_value());
    }

    TEST_F(SpriteRendererComponentTest, AnEmptySourceTakesTheAspectOfTheTexture) {
        ASSERT_NO_FATAL_FAILURE(GiveTexture(Sprite("Plain", "ContainWhole"), kTextureWidth, kTextureHeight));

        Draw("Plain");

        // 20 x 40 in 80 x 80 is 40 x 80, 20 units from each side
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_TRUE(RectNear(_renderer->calls[0].rect, 120.0f, 100.0f, 40.0f, 80.0f));
        EXPECT_FALSE(_renderer->calls[0].source.has_value());
    }

    TEST_F(SpriteRendererComponentTest, TheFitIsComputedInSceneUnits) {
        ASSERT_NO_FATAL_FAILURE(GiveTexture(Sprite("Fit", "Contain"), kTextureWidth, kTextureHeight));

        Draw("Fit");

        // 50 x 50 in 100 x 60 at (20, 10) is 60 x 60 at (40, 10) in scene units
        const Rect fitted = FitSpriteRect(50.0f, 50.0f, Rect{20.0f, 10.0f, 100.0f, 60.0f}, SpriteFit::Contain);
        EXPECT_TRUE(RectNear(fitted, 40.0f, 10.0f, 60.0f, 60.0f));

        const ViewTransform view = ViewTransform::Fit(400.0f, 200.0f, _renderer->width, _renderer->height);
        const Rect expected = view.ToOutput(fitted.x, fitted.y, fitted.w, fitted.h);
        ASSERT_EQ(_renderer->calls.size(), 1u);
        EXPECT_TRUE(RectNear(_renderer->calls[0].rect, expected.x, expected.y, expected.w, expected.h));
        // The scale is 2 and the view is moved down by 100 pixels
        EXPECT_TRUE(RectNear(_renderer->calls[0].rect, 80.0f, 120.0f, 120.0f, 120.0f));
    }

    TEST_F(SpriteRendererComponentTest, ReloadingTheTextureDropsTheOneFromSetTexture) {
        auto sprite = Sprite("Plain", "Contain");
        ASSERT_NO_FATAL_FAILURE(GiveTexture(sprite, kTextureWidth, kTextureHeight));
        Draw("Plain");
        ASSERT_EQ(_renderer->calls.size(), 1u);

        sprite->ReloadTexture();
        Draw("Plain");

        EXPECT_TRUE(_renderer->calls.empty());
    }

}  // namespace BECore::Tests
