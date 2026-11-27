#include <BECore/Input/UIInput.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/Components/ParticleEmitterComponent.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/UIInputEvents.h>
#include <tests/DemoSceneFixture.h>

namespace BECore::Demo::Tests {

    namespace {

        constexpr const char* kPad = "Pad";
        constexpr const char* kBurst = "Burst";
        constexpr int kFramesAfterAClick = 2;

        // The pad is the 200 x 200 rect at (540, 260) of the example; the scene's view is the output, so a scene unit is a pixel
        constexpr float kPadCenterX = 640.0f;
        constexpr float kPadCenterY = 360.0f;
        constexpr float kPadCornerX = 550.0f;  // inside the rect, outside the ellipse inscribed in it
        constexpr float kPadCornerY = 270.0f;
        constexpr float kSkyX = 200.0f;
        constexpr float kSkyY = 600.0f;

        /// The guide page reads the example from the source tree, as the pictures of the page do
        Path ExampleScene() {
            return Path(BagiContentRoots::kEngine) / "docs" / "guide" / "examples" / "input-actions" / "config" / "SceneConfig.xml";
        }

    }  // namespace

    class DemoGuideActionTest : public DemoSceneFixture {
    protected:
        void SetUp() override {
            ASSERT_NO_FATAL_FAILURE(LoadScene(ExampleScene()));
            Draw();  // a target takes input once it has drawn
        }

        /// A press and a release at a scene point, through UIInput as a mouse's, then a few frames
        void ClickAt(float x, float y) {
            BECore::Tests::PressLeftAt(x, y);
            CoreManager::GetUIInput().DispatchMouseButtonUp(BECore::Tests::Button(x, y, false));
            for (int i = 0; i < kFramesAfterAClick; ++i) {
                Update();
            }
        }

        const ParticleEmitterComponent& Emitter() const {
            return Component<ParticleEmitterComponent>(kBurst);
        }
    };

    TEST_F(DemoGuideActionTest, TheExampleHoldsItsPadAndItsEmitterAndNothingBurstsBeforeAClick) {
        const ClickableComponent& pad = BECore::Tests::Require(Node(kPad).GetComponent<ClickableComponent>());
        Update();

        EXPECT_EQ(pad._actionName, Emitter()._burstAction);
        EXPECT_EQ(pad._actionName, "Pop"_intern);
        EXPECT_EQ(Emitter().GetLiveCount(), 0u);
    }

    TEST_F(DemoGuideActionTest, APressOnThePadBurstsTheEmitterNamedByTheSameAction) {
        ClickAt(kPadCenterX, kPadCenterY);

        EXPECT_GT(Emitter().GetLiveCount(), 0u);
    }

    TEST_F(DemoGuideActionTest, APressBesideThePadOrInItsRectsCornerDoesNot) {
        ClickAt(kPadCornerX, kPadCornerY);
        ClickAt(kSkyX, kSkyY);

        EXPECT_EQ(Emitter().GetLiveCount(), 0u);
    }

}  // namespace BECore::Demo::Tests
