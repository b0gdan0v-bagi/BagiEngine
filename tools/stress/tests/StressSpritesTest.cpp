#include "StressSceneFixture.h"

#include <Stress/Components/StressSpritesComponent.h>
#include <gtest/gtest.h>

/**
 * Coverage of StressSpritesComponent on the shipped scene file, drawn on a RecordingRenderer: one draw call per sprite, the field grows and
 * shrinks from the tail, the sprites spin, a game that is not active shows nothing, and the draw order follows the runs of atlases.
 */
namespace BECore::Stress::Tests {

    using BECore::Tests::DrawCall;

    namespace {

        constexpr uint32_t kCount = 2000;

        size_t TextureSwitches(const eastl::vector<DrawCall>& calls) {
            size_t switches = 0;
            for (size_t i = 1; i < calls.size(); ++i) {
                switches += calls[i].texture != calls[i - 1].texture;
            }
            return switches;
        }

    }  // namespace

    class StressSpritesTest : public StressSceneFixture {
    protected:
        void StartSprites(const eastl::vector<eastl::string>& arguments = {}) {
            StartGame(arguments);
            ASSERT_EQ(_manager->GetActiveScene()->GetName(), "StressSprites"_intern);
        }

        SceneNode& Field() const {
            return StressSceneFixture::Field("StressSprites");
        }

        StressSpritesComponent& Component() const {
            auto component = Field().GetComponent<StressSpritesComponent>();
            EXPECT_TRUE(component);
            return *component;
        }
    };

    TEST_F(StressSpritesTest, OneDrawCallPerSprite) {
        StartSprites();
        SetCount(1000);
        Update();
        Draw();

        EXPECT_EQ(Field().GetChildren().size(), 1000u);
        EXPECT_EQ(_renderer->calls.size(), 1000u);
    }

    TEST_F(StressSpritesTest, TheCountOfTheCommandLineIsWhatTheFirstFrameShows) {
        StartSprites({"--stress-count=64"});
        Update();
        Draw();

        EXPECT_EQ(_renderer->calls.size(), 64u);
    }

    TEST_F(StressSpritesTest, TheAtlasesAreMadeOnceAndSharedBySprites) {
        StartSprites();
        SetCount(300);
        Update();
        EXPECT_EQ(_renderer->texturesCreated, kDefaultAtlases);
        SetCount(900);
        Update();
        SetCount(100);
        Update();
        EXPECT_EQ(_renderer->texturesCreated, kDefaultAtlases);
    }

    TEST_F(StressSpritesTest, GrowAppendsAndShrinkRemovesTheTailKeepingTheFirstNodes) {
        StartSprites();
        SetCount(100);
        Update();
        eastl::vector<const SceneNode*> first;
        for (size_t i = 0; i < 10; ++i) {
            first.push_back(Field().GetChildren()[i].Get());
        }

        SetCount(400);
        Update();
        ASSERT_EQ(Field().GetChildren().size(), 400u);
        EXPECT_EQ(Component().GetSpriteCount(), 400u);
        for (size_t i = 0; i < first.size(); ++i) {
            EXPECT_EQ(Field().GetChildren()[i].Get(), first[i]) << i;
        }

        SetCount(50);
        Update();
        ASSERT_EQ(Field().GetChildren().size(), 50u);
        for (size_t i = 0; i < first.size(); ++i) {
            EXPECT_EQ(Field().GetChildren()[i].Get(), first[i]) << i;
        }
        Draw();
        EXPECT_EQ(_renderer->calls.size(), 50u);
    }

    TEST_F(StressSpritesTest, KeptSpritesKeepTheirPlace) {
        StartSprites();
        SetCount(100);
        Update();
        const auto transform = Field().GetChildren()[7]->GetComponent<TransformComponent>();
        const float x = transform->_x;
        const float y = transform->_y;
        const float width = transform->_width;
        const float height = transform->_height;

        SetCount(600);
        Update();

        EXPECT_EQ(Field().GetChildren()[7]->GetComponent<TransformComponent>().Get(), transform.Get());
        EXPECT_EQ(transform->_x, x);
        EXPECT_EQ(transform->_y, y);
        EXPECT_EQ(transform->_width, width);
        EXPECT_EQ(transform->_height, height);
    }

    TEST_F(StressSpritesTest, CountZeroDrawsNothingAndComesBack) {
        StartSprites();
        SetCount(40);
        Update();
        SetCount(0);
        Update();
        Draw();
        EXPECT_TRUE(Field().GetChildren().empty());
        EXPECT_TRUE(_renderer->calls.empty());

        SetCount(40);
        Update();
        Draw();
        EXPECT_EQ(_renderer->calls.size(), 40u);
    }

    TEST_F(StressSpritesTest, TheSpritesSpin) {
        StartSprites();
        SetCount(20);
        Update();  // builds the nodes and turns them for the first time
        auto transform = [&](size_t i) { return Field().GetChildren()[i]->GetComponent<TransformComponent>(); };
        std::vector<float> before;
        for (size_t i = 0; i < 20; ++i) {
            before.push_back(transform(i)->_rotation);
        }

        Update();

        for (size_t i = 0; i < 20; ++i) {
            EXPECT_NE(transform(i)->_rotation, before[i]) << i;
            EXPECT_GE(transform(i)->_rotation, 0.0f);
            EXPECT_LT(transform(i)->_rotation, 360.0f);
        }
    }

    TEST_F(StressSpritesTest, TheRotationStaysInOneTurn) {
        StartSprites();
        SetCount(30);
        for (int frame = 0; frame < 400; ++frame) {
            Update();
        }
        for (const auto& node : Field().GetChildren()) {
            const float rotation = node->GetComponent<TransformComponent>()->_rotation;
            EXPECT_GE(rotation, 0.0f);
            EXPECT_LT(rotation, 360.0f);
        }
    }

    TEST_F(StressSpritesTest, AGameThatIsNotActiveShowsNothing) {
        StartSprites();
        SetCount(100);
        GameManager::GetInstance().Shutdown();  // no active game now
        Update();
        Draw();

        EXPECT_TRUE(Field().GetChildren().empty());
        EXPECT_TRUE(_renderer->calls.empty());
        EXPECT_EQ(_renderer->texturesCreated, 0u);
    }

    TEST_F(StressSpritesTest, AnotherActiveSceneDoesNotBuildTheField) {
        StartSprites();
        SetCount(100);
        _manager->SetActiveScene("StressEntities"_intern);
        Update();
        EXPECT_TRUE(Field().GetChildren().empty());
    }

    TEST_F(StressSpritesTest, EveryBlendModeIsDrawn) {
        StartSprites();
        SetCount(kCount);
        Update();
        Draw();
        bool seen[3] = {};
        for (const DrawCall& call : _renderer->calls) {
            seen[static_cast<size_t>(call.blend)] = true;
        }
        EXPECT_TRUE(seen[0]);
        EXPECT_TRUE(seen[1]);
        EXPECT_TRUE(seen[2]);
    }

    TEST_F(StressSpritesTest, TextureSwitchesFollowTheRuns) {
        StartSprites();
        SetCount(kCount);
        Update();
        Draw();

        ASSERT_EQ(_renderer->calls.size(), kCount);
        EXPECT_LE(TextureSwitches(_renderer->calls), size_t{kDefaultAtlases});
    }

    TEST_F(StressSpritesTest, InterleaveSwitchesTheTextureOnEverySprite) {
        StartSprites({"--stress-interleave"});
        SetCount(kCount);
        Update();
        Draw();

        ASSERT_EQ(_renderer->calls.size(), kCount);
        EXPECT_EQ(TextureSwitches(_renderer->calls), size_t{kCount - 1});
    }

    TEST_F(StressSpritesTest, ChangingTheCountKeepsTheRunsOfTheNewCount) {
        StartSprites();
        SetCount(100);
        Update();
        SetCount(kCount);
        Update();
        Draw();
        EXPECT_LE(TextureSwitches(_renderer->calls), size_t{kDefaultAtlases});
    }

}  // namespace BECore::Stress::Tests
