#include <BECore/Animation/TweenManager.h>
#include <BECore/Scene/Components/ParticleEmitterComponent.h>
#include <Common/UITestSupport.h>
#include <tests/DemoSceneFixture.h>

namespace BECore::Demo::Tests {

    namespace {

        constexpr int kSecond = 32;  // frames of kFrame

    }  // namespace

    class DemoControlsTest : public DemoSceneFixture {
    protected:
        void Run(int frames) {
            for (int i = 0; i < frames; ++i) {
                Update();
            }
        }

        StringView Variable(PoolString name) const {
            return CoreManager::GetUIVars().Get(name);
        }
    };

    TEST_F(DemoControlsTest, TheAnimateToggleStopsAndStartsTheTweens) {
        LoadScene();
        Update();
        ASSERT_GT(CoreManager::GetTweens().GetActiveCount(), 0u);

        ASSERT_NO_FATAL_FAILURE(Click("Animate"));
        Update();
        EXPECT_EQ(Variable(DemoNames::animateVar), DemoNames::off);
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);

        ASSERT_NO_FATAL_FAILURE(Click("Animate"));
        Update();
        EXPECT_EQ(Variable(DemoNames::animateVar), DemoNames::on);
        EXPECT_GT(CoreManager::GetTweens().GetActiveCount(), 0u);
    }

    TEST_F(DemoControlsTest, TheSliderSetsTheRateOfTheFountainAndTheLabelShowsIt) {
        LoadScene();
        Update();
        auto& fountain = Component<ParticleEmitterComponent>("Fountain");

        ASSERT_NO_FATAL_FAILURE(Click("Emission", 1.0f));
        EXPECT_EQ(fountain._effect.rate, 120.0f);
        EXPECT_EQ(Variable(DemoNames::emissionVar), "120");
        Draw();
        EXPECT_EQ(BECore::Tests::ShownText(Element("EmissionLabel")), "Emission: 120 / s");

        // A rate edited by hand (the inspector) reaches the panel too
        fountain._effect.rate = 25.0f;
        Update();
        EXPECT_EQ(Variable(DemoNames::emissionVar), "25");
    }

    TEST_F(DemoControlsTest, ALoweredRateKeepsTheFountainEmitting) {
        LoadScene();
        auto& fountain = Component<ParticleEmitterComponent>("Fountain");
        Run(8 * kSecond);

        ASSERT_NO_FATAL_FAILURE(Click("Emission", 0.2f));
        ASSERT_LT(fountain._effect.rate, 20.0f);
        ASSERT_GT(fountain._effect.rate, 0.0f);
        Run(2 * kSecond);  // longer than a particle lives: every one that is alive was born at the new rate
        EXPECT_GT(fountain.GetLiveCount(), 0u);
    }

    TEST_F(DemoControlsTest, TheBurstButtonFiresTheBurstEmitter) {
        LoadScene();
        auto& sparks = Component<ParticleEmitterComponent>("Sparks");
        Run(2 * kSecond);  // the burst of the start has died
        ASSERT_EQ(sparks.GetLiveCount(), 0u);

        ASSERT_NO_FATAL_FAILURE(Click("Burst"));
        EXPECT_EQ(sparks.GetLiveCount(), static_cast<size_t>(sparks._effect.burstCount));
    }

    TEST_F(DemoControlsTest, ThePanelFitsItsTextAndPassesTheValidator) {
        LoadScene();
        CoreManager::GetUIVars().Set(DemoNames::emissionVar, 120);  // the longest label
        Draw();
        const UICanvasLayout& layout = Panel().GetLayout();
        EXPECT_TRUE(layout.Validate({}).empty());
        EXPECT_EQ(BECore::Tests::CutTexts(Panel().GetRoot(), layout.GetResolved().transform.scale), "");
    }

}  // namespace BECore::Demo::Tests
