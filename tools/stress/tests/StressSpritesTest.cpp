#include <BECore/Scene/Components/ParticleEmitterComponent.h>
#include <BECore/Text/TextComponent.h>
#include <Common/AllocCheck.h>
#include <Stress/Components/StressLifeComponent.h>
#include <Stress/Components/StressShapeComponent.h>
#include <Stress/Components/StressSpritesComponent.h>
#include <Stress/SpriteField.h>
#include <Stress/StressNames.h>
#include <gtest/gtest.h>
#include <tests/StressSceneFixture.h>

namespace BECore::Stress::Tests {

    using BECore::Tests::DrawCall;

    namespace {

        constexpr uint32_t kCount = 2000;
        constexpr float kShiftTolerance = 0.01f;  // positions of a few thousand units in float

        size_t TextureSwitches(const Vector<DrawCall>& calls) {
            size_t switches = 0;
            for (size_t i = 1; i < calls.size(); ++i) {
                switches += calls[i].texture != calls[i - 1].texture;
            }
            return switches;
        }

    }  // namespace

    class StressSpritesTest : public StressSceneFixture {
    protected:
        void StartSprites(const Vector<String>& arguments = {}) {
            StartGame(arguments);
            ASSERT_EQ(_manager->GetActiveScene()->GetName(), StressNames::sprites);
        }

        SceneNode& Field() const {
            return StressSceneFixture::Field("StressSprites");
        }

        StressSpritesComponent& Component() const {
            auto component = Field().GetComponent<StressSpritesComponent>();
            EXPECT_TRUE(component);
            return *component;
        }

        Vec2 PositionOf(size_t i) const {
            const auto transform = Field().GetChildren()[i]->GetComponent<TransformComponent>();
            return Vec2(transform->_x, transform->_y);
        }

        Vector<IntrusivePtr<SceneNode>> Nodes() const {
            Vector<IntrusivePtr<SceneNode>> nodes;
            for (const auto& node : Field().GetChildren()) {
                nodes.push_back(node);
            }
            return nodes;
        }

        size_t Updaters() const {
            return GetScene("StressSprites").GetUpdaterCount(SceneUpdateKind::Frame);
        }

        /// The age of the life of the sprite node @p i; negative when it has none
        float AgeOf(size_t i) const {
            const SceneNode& node = *Field().GetChildren()[i];
            if (node.GetChildren().size() != 1) {
                return -1.0f;
            }
            const auto life = node.GetChildren()[0]->GetComponent<StressLifeComponent>();
            return life ? life->GetAge() : -1.0f;
        }

        /// How many nodes of the field have a component of type @p T
        template <typename T>
        size_t CountWith() const {
            size_t count = 0;
            for (const auto& node : Field().GetChildren()) {
                count += node->GetComponent<T>() ? 1 : 0;
            }
            return count;
        }

        /// Runs enough frames for every list of a frame to reach its size, then expects further frames to allocate nothing
        void ExpectSteadyFramesToAllocateNothing() {
            constexpr int kWarmup = 96;  // three seconds of frames: the pool of an emitter is full by then
            constexpr int kMeasured = 64;
            // Without an output size nothing is culled: the recorder gets the storage of a call for every sprite, however many a later frame draws
            const float width = _renderer->width;
            _renderer->width = 0.0f;
            Update();
            Draw();
            _renderer->width = width;
            for (int frame = 0; frame < kWarmup; ++frame) {
                Update();
                Draw();
            }
            BECore::Tests::ExpectNoAllocations([&] {
                for (int frame = 0; frame < kMeasured; ++frame) {
                    Update();
                    Draw();
                }
            });
        }

        /// Where sprite @p i of @p count lies in the field of a world of @p world views
        Vec2 PlaceInWorld(uint32_t i, uint32_t count, uint32_t world) const {
            const Scene& scene = GetScene("StressSprites");
            const FieldView field{scene.GetViewWidth() * world, scene.GetViewHeight() * world};
            const SpritePlacement placement = Place(i, count, StressGame::GetActive()->GetSettings(), field);
            return Vec2(placement.x, placement.y);
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
        Vector<const SceneNode*> first;
        for (size_t i = 0; i < 10; ++i) {
            first.push_back(Field().GetChildren()[i].Get());
        }

        SetCount(400);
        Update();
        ASSERT_EQ(Field().GetChildren().size(), 400u);
        EXPECT_EQ(Component().GetObjectCount(), 400u);
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

    TEST_F(StressSpritesTest, TheSizeFlagGivesEverySpriteThatSide) {
        StartSprites({"--stress-sprite-size=300"});
        SetCount(50);
        Update();
        SetCount(80);
        Update();

        ASSERT_EQ(Field().GetChildren().size(), 80u);
        for (const auto& node : Field().GetChildren()) {
            const auto transform = node->GetComponent<TransformComponent>();
            EXPECT_EQ(transform->_width, 300.0f);
            EXPECT_EQ(transform->_height, 300.0f);
        }
    }

    TEST_F(StressSpritesTest, AWorldSpreadsTheSpritesBeyondTheViewAndTheCameraMovesThemAllAlike) {
        StartSprites({"--stress-world=4"});
        SetCount(200);
        Update();
        Vector<Vec2> before;
        bool beyondTheView = false;
        for (size_t i = 0; i < 200; ++i) {
            before.push_back(PositionOf(i));
            beyondTheView |= before[i].x > GetScene("StressSprites").GetViewWidth() && before[i].y > GetScene("StressSprites").GetViewHeight();
        }
        EXPECT_TRUE(beyondTheView);

        Update();

        const Vec2 shift = PositionOf(0) - before[0];
        EXPECT_LT(shift.x, 0.0f) << "the camera goes right, so the sprites go left";
        EXPECT_LT(shift.y, 0.0f);
        for (size_t i = 1; i < 200; ++i) {
            ASSERT_NEAR(PositionOf(i).x - before[i].x, shift.x, kShiftTolerance) << i;
            ASSERT_NEAR(PositionOf(i).y - before[i].y, shift.y, kShiftTolerance) << i;
        }
    }

    TEST_F(StressSpritesTest, ANewSpriteOfAWorldIsShiftedInTheFrameThatMakesIt) {
        StartSprites({"--stress-world=4"});
        SetCount(10);
        for (int frame = 0; frame < 20; ++frame) {
            Update();
        }
        SetCount(30);
        Update();

        const Vec2 camera = PlaceInWorld(0, 30, 4) - PositionOf(0);
        EXPECT_GT(camera.x, 0.0f);
        for (uint32_t i = 10; i < 30; ++i) {
            ASSERT_NEAR(PositionOf(i).x, PlaceInWorld(i, 30, 4).x - camera.x, kShiftTolerance) << i;
            ASSERT_NEAR(PositionOf(i).y, PlaceInWorld(i, 30, 4).y - camera.y, kShiftTolerance) << i;
        }
    }

    TEST_F(StressSpritesTest, AWorldOfOneViewLeavesThePositionsAlone) {
        StartSprites({"--stress-world=1"});
        SetCount(50);
        for (int frame = 0; frame < 20; ++frame) {
            Update();
        }
        for (uint32_t i = 0; i < 50; ++i) {
            EXPECT_EQ(PositionOf(i), PlaceInWorld(i, 50, 1)) << i;
        }
    }

    TEST_F(StressSpritesTest, OnlyTheSpritesNearTheOutputOfAWorldAreDrawn) {
        constexpr uint32_t kWorldCount = 8000;
        constexpr uint32_t kVisible = kWorldCount / 16;  // a view is a sixteenth of a world of 4 views per side
        constexpr int kToTheMiddle = 192;                // six seconds of frames: the camera is away from the edges of the field
        StartSprites({"--stress-world=4"});
        _renderer->width = GetScene("StressSprites").GetViewWidth();
        _renderer->height = GetScene("StressSprites").GetViewHeight();
        SetCount(kWorldCount);
        for (int frame = 0; frame < kToTheMiddle; ++frame) {
            Update();
        }
        Draw();

        EXPECT_GT(_renderer->calls.size(), size_t{kVisible * 3 / 4});
        EXPECT_LT(_renderer->calls.size(), size_t{kVisible * 5 / 4});
        for (const DrawCall& call : _renderer->calls) {
            ASSERT_GE(call.rect.x + call.rect.w, -kSpriteMaxSize);
            ASSERT_LE(call.rect.x, _renderer->width + kSpriteMaxSize);
            ASSERT_GE(call.rect.y + call.rect.h, -kSpriteMaxSize);
            ASSERT_LE(call.rect.y, _renderer->height + kSpriteMaxSize);
        }
        size_t centred = 0;
        for (uint32_t i = 0; i < kWorldCount; ++i) {
            const Vec2 position = PositionOf(i);
            centred += position.x >= 0.0f && position.x <= _renderer->width - kSpriteMaxSize && position.y >= 0.0f && position.y <= _renderer->height - kSpriteMaxSize;
        }
        EXPECT_GE(_renderer->calls.size(), centred) << "no sprite on screen is left out";
    }

    TEST_F(StressSpritesTest, AWorldOfOneViewDrawsEverySprite) {
        StartSprites();
        _renderer->width = GetScene("StressSprites").GetViewWidth();
        _renderer->height = GetScene("StressSprites").GetViewHeight();
        SetCount(kCount);
        Update();
        Draw();

        EXPECT_EQ(_renderer->calls.size(), kCount);
    }

    TEST_F(StressSpritesTest, ChurnReplacesTheOldestSpritesWithNewOnesAtTheTail) {
        StartSprites({"--stress-churn=10"});
        SetCount(100);
        Update();
        const Vector<IntrusivePtr<SceneNode>> before = Nodes();  // held, so that a new node cannot have the address of an old one

        Update();
        Draw();

        const auto& after = Field().GetChildren();
        ASSERT_EQ(after.size(), 100u);
        for (size_t i = 0; i < 90; ++i) {
            ASSERT_EQ(after[i].Get(), before[i + 10].Get()) << i;
        }
        for (size_t i = 90; i < 100; ++i) {
            for (const auto& old : before) {
                ASSERT_NE(after[i].Get(), old.Get()) << i;
            }
        }
        EXPECT_EQ(Component().GetObjectCount(), 100u);
        EXPECT_EQ(_renderer->calls.size(), 100u);
    }

    TEST_F(StressSpritesTest, ASpriteOfAChurnIsTheNextOfTheFieldAndKeepsItsPlace) {
        StartSprites({"--stress-churn=10"});
        SetCount(100);
        Update();  // builds sprites 0..99 and replaces the first 10
        Update();
        Update();

        for (uint32_t i = 0; i < 100; ++i) {
            ASSERT_EQ(PositionOf(i), PlaceInWorld(30 + i, 100, 1)) << i;
        }
    }

    TEST_F(StressSpritesTest, ASpriteLivesOnItsChildNodeUntilItIsReplaced) {
        StartSprites({"--stress-churn=10"});
        SetCount(40);
        for (int frame = 0; frame < 5; ++frame) {
            Update();
        }

        EXPECT_GT(AgeOf(0), AgeOf(39)) << "the oldest sprite is at the head";
        EXPECT_EQ(AgeOf(39), 0.0f) << "a new life is first updated in the next pass";
    }

    TEST_F(StressSpritesTest, WithoutChurnASpriteHasNoChild) {
        StartSprites();
        SetCount(20);
        Update();

        for (const auto& node : Field().GetChildren()) {
            ASSERT_TRUE(node->GetChildren().empty());
        }
    }

    TEST_F(StressSpritesTest, AChurnAboveTheCountReplacesEverySprite) {
        StartSprites({"--stress-churn=1000"});
        SetCount(30);
        Update();
        const Vector<IntrusivePtr<SceneNode>> before = Nodes();

        Update();
        Draw();

        ASSERT_EQ(Field().GetChildren().size(), 30u);
        for (const auto& node : Field().GetChildren()) {
            for (const auto& old : before) {
                ASSERT_NE(node.Get(), old.Get());
            }
        }
        EXPECT_EQ(_renderer->calls.size(), 30u);
    }

    TEST_F(StressSpritesTest, ChurnFollowsTheCountDownToNothingAndBack) {
        StartSprites({"--stress-churn=7"});
        SetCount(0);
        Update();
        const size_t withoutSprites = Updaters();

        for (const uint32_t count : {50u, 200u, 3u, 0u, 20u}) {
            SetCount(count);
            Update();
            Update();
            Draw();
            ASSERT_EQ(Field().GetChildren().size(), count);
            ASSERT_EQ(_renderer->calls.size(), count);
            ASSERT_EQ(Updaters(), withoutSprites + count) << "one life per sprite";
        }
    }

    TEST_F(StressSpritesTest, AChurnFrameRebuildsTheDrawListOnce) {
        StartSprites({"--stress-churn=5"});
        SetCount(50);
        Update();
        Draw();
        const uint32_t builds = GetScene("StressSprites").GetDrawListBuilds();

        for (int frame = 0; frame < 6; ++frame) {
            Update();
            Draw();
        }

        EXPECT_EQ(GetScene("StressSprites").GetDrawListBuilds(), builds + 6);
    }

    TEST_F(StressSpritesTest, ASteadyChurnFrameAllocatesNothing) {
        constexpr uint32_t kChurned = 400;
        StartSprites({"--stress-churn=50"});
        SetCount(kChurned);

        ExpectSteadyFramesToAllocateNothing();
    }

    TEST_F(StressSpritesTest, ASteadyFrameOfAWorldAllocatesNothing) {
        StartSprites({"--stress-world=4"});
        _renderer->width = GetScene("StressSprites").GetViewWidth();
        _renderer->height = GetScene("StressSprites").GetViewHeight();
        SetCount(kCount);

        ExpectSteadyFramesToAllocateNothing();
    }

    TEST_F(StressSpritesTest, WithoutTheMixEveryObjectIsASpriteOnTheLayerOfTheField) {
        StartSprites();
        SetCount(200);
        Update();

        EXPECT_EQ(CountWith<SpriteRendererComponent>(), 200u);
        for (const auto& node : Field().GetChildren()) {
            ASSERT_EQ(node->GetLayer(), 0);
        }
    }

    TEST_F(StressSpritesTest, AMixHasEveryKindOnEveryLayer) {
        StartSprites({"--stress-mix"});
        SetCount(kCount);
        Update();

        const size_t sprites = CountWith<SpriteRendererComponent>();
        const size_t texts = CountWith<TextComponent>();
        const size_t shapes = CountWith<StressShapeComponent>();
        const size_t emitters = CountWith<ParticleEmitterComponent>();
        EXPECT_EQ(sprites + texts + shapes + emitters, kCount);
        EXPECT_GT(sprites, kCount / 2);
        EXPECT_GT(texts, 0u);
        EXPECT_GT(shapes, 0u);
        EXPECT_GT(emitters, 0u);
        bool layers[kMixLayers] = {};
        for (const auto& node : Field().GetChildren()) {
            layers[node->GetLayer()] = true;
        }
        for (const bool used : layers) {
            EXPECT_TRUE(used);
        }
    }

    TEST_F(StressSpritesTest, EveryKindOfAMixDraws) {
        constexpr uint32_t kMixed = 320;
        constexpr size_t kQuadVertices = 4;
        StartSprites({"--stress-mix"});
        SetCount(kMixed);
        for (int frame = 0; frame < 8; ++frame) {
            Update();
        }
        Draw();

        size_t spriteDraws = 0;
        size_t glyphs = 0;
        size_t shapes = 0;
        size_t particles = 0;
        for (const DrawCall& call : _renderer->calls) {
            ASSERT_FALSE(call.clip.has_value()) << "a text fits its box";
            if (call.kind == BECore::Tests::DrawKind::Texture) {
                ++glyphs;  // the sprites spin, so theirs are geometry
            } else if (call.texture == nullptr) {
                ASSERT_EQ(call.vertices.size(), StressShapeComponent::kCorners + 1);
                ++shapes;
            } else if (call.vertices.size() > kQuadVertices) {
                ++particles;
            } else {
                ++spriteDraws;
            }
        }
        EXPECT_EQ(spriteDraws, CountWith<SpriteRendererComponent>());
        EXPECT_EQ(shapes, CountWith<StressShapeComponent>());
        EXPECT_EQ(particles, CountWith<ParticleEmitterComponent>());
        EXPECT_GE(glyphs, CountWith<TextComponent>());
    }

    TEST_F(StressSpritesTest, AMixBreaksTheBatchesOfThePlainField) {
        constexpr uint32_t kMixed = 320;
        const auto submissions = [this](const Vector<String>& arguments) {
            StartSprites(arguments);
            _renderer->SetBatchingEnabled(true);
            SetCount(kMixed);
            for (int frame = 0; frame < 8; ++frame) {
                Update();
            }
            Draw();
            const size_t calls = _renderer->calls.size();
            TearDown();  // the next game of the comparison starts from nothing
            return calls;
        };
        const size_t plain = submissions({});
        const size_t mixed = submissions({"--stress-mix"});

        EXPECT_GT(mixed, plain * 8);
    }

    TEST_F(StressSpritesTest, AMixWithChurnKeepsTheCountAndLeavesNothingBehind) {
        StartSprites({"--stress-mix", "--stress-churn=40"});
        SetCount(0);
        Update();
        const size_t withoutObjects = Updaters();

        for (const uint32_t count : {320u, 100u, 400u}) {
            SetCount(count);
            for (int frame = 0; frame < 12; ++frame) {
                Update();
                Draw();
            }
            ASSERT_EQ(Field().GetChildren().size(), count);
            // A life for every object and the update of every emitter
            ASSERT_EQ(Updaters(), withoutObjects + count + CountWith<ParticleEmitterComponent>());
        }

        SetCount(0);
        Update();
        Draw();
        EXPECT_TRUE(Field().GetChildren().empty());
        EXPECT_TRUE(_renderer->calls.empty());
        EXPECT_EQ(Updaters(), withoutObjects);
    }

    TEST_F(StressSpritesTest, ASteadyMixedFrameAllocatesNothing) {
        StartSprites({"--stress-mix"});
        SetCount(400);

        ExpectSteadyFramesToAllocateNothing();
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
        Vector<float> before;
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
        _manager->SetActiveScene(StressNames::entities);
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
