#include <BECore/GameManager/CoreManager.h>
#include <BECore/Particles/ParticleEffect.h>
#include <BECore/Reflection/IPropertyVisitor.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/Components/ParticleEmitterComponent.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr double kFrame = 1.0 / 32.0;  // a power of two, so the sums of frames are exact
        constexpr int kFramesToOutliveTheEffect = 20;
        constexpr size_t kVerticesPerQuad = 4;
        constexpr float kTolerance = 0.001f;
        constexpr uint32_t kTextureSize = 16;
        constexpr size_t kBytesPerPixel = 4;
        constexpr const char* kSparkSprite = "ParticleTestSpark";

        // Scene "A" is the active one. "OneShot" fires 8 particles of half a second at the first update. "Glow" is the same with Add. "Manual" does not play on its own,
        // stands still (speed 0) with a square of 10 and bursts on the action "Pop". "Loop" emits 32 a second once played. "Spark" draws a sprite of the library, "Unknown" one that is not there.
        // Scene "B" is not active and has the same "Manual".
        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="A"><nodes>
    <node name="OneShot"><components>
      <component type="TransformComponent" x="96" y="48" width="8" height="4"/>
      <component type="ParticleEmitterComponent"><effect burstCount="8" maxParticles="32" lifetimeMin="0.5" lifetimeMax="0.5"/></component>
    </components></node>
    <node name="Glow"><components>
      <component type="TransformComponent" x="0" y="0" width="0" height="0"/>
      <component type="ParticleEmitterComponent"><effect burstCount="3" blend="Add" lifetimeMin="0.5" lifetimeMax="0.5"/></component>
    </components></node>
    <node name="Manual"><components>
      <component type="TransformComponent" x="96" y="48" width="8" height="4"/>
      <component type="ParticleEmitterComponent" playOnStart="false" burstAction="Pop">
        <effect burstCount="5" maxParticles="16" lifetimeMin="1" lifetimeMax="1" speedMin="0" speedMax="0"><size from="10" to="10"/></effect>
      </component>
    </components></node>
    <node name="Loop"><components>
      <component type="TransformComponent" x="0" y="0" width="0" height="0"/>
      <component type="ParticleEmitterComponent" playOnStart="false"><effect looping="true" rate="32" maxParticles="64" lifetimeMin="1" lifetimeMax="1"/></component>
    </components></node>
    <node name="Spark"><components>
      <component type="TransformComponent" x="0" y="0" width="0" height="0"/>
      <component type="ParticleEmitterComponent" playOnStart="false"><effect burstCount="2" spriteId="ParticleTestSpark" speedMin="0" speedMax="0"/></component>
    </components></node>
    <node name="Unknown"><components>
      <component type="TransformComponent" x="0" y="0" width="0" height="0"/>
      <component type="ParticleEmitterComponent" playOnStart="false"><effect burstCount="2" spriteId="NoSuchSprite"/></component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="B"><nodes>
    <node name="Manual"><components>
      <component type="TransformComponent" x="96" y="48" width="8" height="4"/>
      <component type="ParticleEmitterComponent" burstAction="Pop"><effect burstCount="5"/></component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        /// Remembers the name of every field and group an inspector would show
        class NameRecorder : public IPropertyVisitor {
        public:
            bool Visit(eastl::string_view name, bool&) override {
                return Note(name);
            }
            bool Visit(eastl::string_view name, int8_t&) override {
                return Note(name);
            }
            bool Visit(eastl::string_view name, uint8_t&) override {
                return Note(name);
            }
            bool Visit(eastl::string_view name, int16_t&) override {
                return Note(name);
            }
            bool Visit(eastl::string_view name, uint16_t&) override {
                return Note(name);
            }
            bool Visit(eastl::string_view name, int32_t&) override {
                return Note(name);
            }
            bool Visit(eastl::string_view name, uint32_t&) override {
                return Note(name);
            }
            bool Visit(eastl::string_view name, int64_t&) override {
                return Note(name);
            }
            bool Visit(eastl::string_view name, uint64_t&) override {
                return Note(name);
            }
            bool Visit(eastl::string_view name, float&) override {
                return Note(name);
            }
            bool Visit(eastl::string_view name, double&) override {
                return Note(name);
            }
            bool Visit(eastl::string_view name, PoolString&) override {
                return Note(name);
            }
            bool Visit(eastl::string_view name, eastl::string&) override {
                return Note(name);
            }
            bool VisitEnum(eastl::string_view name, const eastl::string_view*, size_t, size_t&) override {
                return Note(name);
            }
            bool BeginCompound(eastl::string_view name) override {
                Note(name);
                return true;
            }

            [[nodiscard]] bool Has(eastl::string_view name) const {
                return eastl::find(_names.begin(), _names.end(), name) != _names.end();
            }

        private:
            bool Note(eastl::string_view name) {
                _names.emplace_back(name.data(), name.size());
                return false;
            }

            eastl::vector<eastl::string> _names;
        };

        void ExpectColor(const Color& actual, const Color& expected) {
            EXPECT_EQ(actual.r, expected.r);
            EXPECT_EQ(actual.g, expected.g);
            EXPECT_EQ(actual.b, expected.b);
            EXPECT_EQ(actual.a, expected.a);
        }

        IntrusivePtr<SceneNode> FindNodeByName(Scene& scene, const char* name) {
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(name)) {
                    return node;
                }
            }
            return {};
        }

    }  // namespace

    class ParticleEmitterComponentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            SpriteEntry spark;
            spark.name = PoolString::Intern(kSparkSprite);
            spark.srcRect = Rect(8.0f, 0.0f, 8.0f, 8.0f);  // the top right quarter of a 16 by 16 texture
            CoreManager::GetTextureLibrary().AddOrUpdate(spark);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetTextureLibrary().Remove(PoolString::Intern(kSparkSprite));
            CoreManager::GetRendererManager().SetRenderer({});
        }

        IntrusivePtr<ParticleEmitterComponent> Emitter(const char* node, const char* scene = "A") {
            auto found = FindNodeByName(*_manager->GetSceneByName(PoolString::Intern(scene)), node);
            return found ? found->GetComponent<ParticleEmitterComponent>() : IntrusivePtr<ParticleEmitterComponent>{};
        }

        /// One frame of @p seconds of game time through the scene manager
        void Frame(double seconds = kFrame) {
            CoreManager::GetTime().Advance(seconds);
            _manager->UpdateAll();
        }

        void Frames(int count) {
            for (int i = 0; i < count; ++i) {
                Frame();
            }
        }

        /// The geometry draw calls of one draw pass of the active scene
        eastl::vector<DrawCall> Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
            eastl::vector<DrawCall> geometry;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Geometry) {
                    geometry.push_back(call);
                }
            }
            return geometry;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(ParticleEmitterComponentTest, ASceneFileCreatesTheEmitterWithItsEffect) {
        auto emitter = Emitter("OneShot");
        ASSERT_TRUE(emitter);
        EXPECT_TRUE(emitter->_playOnStart);
        EXPECT_EQ(emitter->_effect.burstCount, 8);
        EXPECT_EQ(emitter->_effect.maxParticles, 32);
        EXPECT_FLOAT_EQ(emitter->_effect.lifetimeMax, 0.5f);
        EXPECT_EQ(Emitter("Glow")->_effect.blend, BlendMode::Add);
        EXPECT_EQ(Emitter("Manual")->_burstAction, "Pop"_intern);
        EXPECT_FALSE(Emitter("Manual")->_playOnStart);
    }

    TEST_F(ParticleEmitterComponentTest, AOneShotFiresAtTheFirstUpdateAndEnds) {
        auto emitter = Emitter("OneShot");
        EXPECT_EQ(emitter->GetLiveCount(), 0u);
        EXPECT_TRUE(emitter->NeedsFrames()) << "playing, the schedule has not fired yet";

        Frame();
        EXPECT_EQ(emitter->GetLiveCount(), 8u);
        EXPECT_FALSE(emitter->IsEmitting());
        EXPECT_TRUE(emitter->NeedsFrames());

        Frames(kFramesToOutliveTheEffect);
        EXPECT_EQ(emitter->GetLiveCount(), 0u);
        EXPECT_FALSE(emitter->NeedsFrames());
    }

    TEST_F(ParticleEmitterComponentTest, ALoopingEmitterSpawnsByGameTime) {
        auto emitter = Emitter("Loop");
        emitter->Play();
        Frames(16);  // half a second at 32 a second
        EXPECT_EQ(emitter->GetLiveCount(), 16u);
        EXPECT_TRUE(emitter->NeedsFrames());
    }

    TEST_F(ParticleEmitterComponentTest, AHeldFrameMovesNothing) {
        auto emitter = Emitter("OneShot");
        Frames(2);
        const auto before = Draw();
        ASSERT_EQ(before.size(), 2u) << "OneShot and Glow";
        const double time = emitter->GetSimulation().GetTime();

        Frame(0.0);
        Frame(0.0);
        EXPECT_DOUBLE_EQ(emitter->GetSimulation().GetTime(), time);
        const auto after = Draw();
        ASSERT_EQ(after.size(), before.size());
        for (size_t call = 0; call < before.size(); ++call) {
            ASSERT_EQ(after[call].vertices.size(), before[call].vertices.size());
            for (size_t i = 0; i < before[call].vertices.size(); ++i) {
                EXPECT_FLOAT_EQ(after[call].vertices[i].x, before[call].vertices[i].x);
                EXPECT_FLOAT_EQ(after[call].vertices[i].y, before[call].vertices[i].y);
            }
        }
    }

    TEST_F(ParticleEmitterComponentTest, EachEmitterDrawsOneBatchOfFourVerticesPerParticleUnderItsBlendMode) {
        EXPECT_TRUE(Draw().empty()) << "nothing is alive before the first update";

        Frame();
        const auto calls = Draw();
        ASSERT_EQ(calls.size(), 2u) << "OneShot (8 particles) and Glow (3); the others have none";
        // The calls follow the scene order
        EXPECT_EQ(calls[0].vertices.size(), 8 * kVerticesPerQuad);
        EXPECT_EQ(calls[0].indices.size(), 8u * 6u);
        EXPECT_EQ(calls[0].blend, BlendMode::Alpha);
        EXPECT_EQ(calls[1].vertices.size(), 3 * kVerticesPerQuad);
        EXPECT_EQ(calls[1].blend, BlendMode::Add);
        EXPECT_EQ(calls[0].texture, nullptr);
        EXPECT_EQ(_renderer->GetBlendMode(), BlendMode::Alpha) << "the blend mode does not leak out of the draw";

        Frames(kFramesToOutliveTheEffect);
        for (const DrawCall& call : Draw()) {
            EXPECT_NE(call.vertices.size(), 0u);
        }
        EXPECT_EQ(Emitter("OneShot")->GetLiveCount(), 0u);
    }

    TEST_F(ParticleEmitterComponentTest, ABurstPlacesParticlesAroundThePointWithTheTint) {
        auto emitter = Emitter("Manual");
        emitter->Burst(200.0f, 120.0f, 5, Color(255, 0, 0, 255));
        EXPECT_EQ(emitter->GetLiveCount(), 5u);

        const auto calls = Draw();
        ASSERT_EQ(calls.size(), 1u);
        EXPECT_NEAR(calls[0].rect.x, 195.0f, kTolerance);
        EXPECT_NEAR(calls[0].rect.y, 115.0f, kTolerance);
        EXPECT_NEAR(calls[0].rect.w, 10.0f, kTolerance);
        EXPECT_NEAR(calls[0].rect.h, 10.0f, kTolerance);
        ExpectColor(calls[0].vertices[0].color, Color(255, 0, 0, 255));
    }

    TEST_F(ParticleEmitterComponentTest, ABurstWithoutACountUsesTheEffectsBurstCount) {
        auto emitter = Emitter("Manual");
        emitter->Burst(0.0f, 0.0f);
        EXPECT_EQ(emitter->GetLiveCount(), 5u);
        emitter->Burst(0.0f, 0.0f, 2);
        EXPECT_EQ(emitter->GetLiveCount(), 7u);
        EXPECT_TRUE(emitter->NeedsFrames());
    }

    TEST_F(ParticleEmitterComponentTest, TheBurstActionBurstsAtTheNodesCenterAndAnotherActionDoesNot) {
        auto emitter = Emitter("Manual");
        InputEvents::ActionEvent::Emit("Other"_intern);
        EXPECT_EQ(emitter->GetLiveCount(), 0u);

        InputEvents::ActionEvent::Emit("Pop"_intern);
        EXPECT_EQ(emitter->GetLiveCount(), 5u);
        const auto calls = Draw();
        ASSERT_EQ(calls.size(), 1u);
        EXPECT_NEAR(calls[0].rect.x + calls[0].rect.w * 0.5f, 100.0f, kTolerance) << "the center of the rect (96, 48, 8, 4)";
        EXPECT_NEAR(calls[0].rect.y + calls[0].rect.h * 0.5f, 50.0f, kTolerance);
    }

    TEST_F(ParticleEmitterComponentTest, AnInactiveSceneNeitherBurstsNorUpdatesNorDraws) {
        auto inactive = Emitter("Manual", "B");
        ASSERT_TRUE(inactive);
        InputEvents::ActionEvent::Emit("Pop"_intern);
        Frames(2);
        EXPECT_EQ(inactive->GetLiveCount(), 0u);
        EXPECT_DOUBLE_EQ(inactive->GetSimulation().GetTime(), 0.0);

        EXPECT_EQ(Emitter("Manual")->GetLiveCount(), 5u) << "scene A took the action";
        EXPECT_EQ(Draw().size(), 3u) << "OneShot, Glow and Manual of scene A, nothing of B";
    }

    TEST_F(ParticleEmitterComponentTest, ASpriteOfTheLibraryGivesTexturedGeometryWithItsUVs) {
        auto emitter = Emitter("Spark");
        const eastl::vector<uint8_t> pixels(static_cast<size_t>(kTextureSize) * kTextureSize * kBytesPerPixel, 255);
        auto texture = _renderer->CreateTexture(pixels.data(), kTextureSize, kTextureSize);
        emitter->SetTexture(texture);
        Frame();
        emitter->Burst(0.0f, 0.0f);

        const auto calls = Draw();
        const DrawCall* spark = nullptr;
        for (const DrawCall& call : calls) {
            if (call.texture != nullptr) {
                spark = &call;
            }
        }
        ASSERT_NE(spark, nullptr);
        EXPECT_EQ(spark->texture, texture.Get());
        ASSERT_EQ(spark->vertices.size(), 2 * kVerticesPerQuad);
        // The top right quarter: u from 0.5 to 1, v from 0 to 0.5; the corners go top left, top right, bottom right, bottom left
        EXPECT_FLOAT_EQ(spark->vertices[0].u, 0.5f);
        EXPECT_FLOAT_EQ(spark->vertices[0].v, 0.0f);
        EXPECT_FLOAT_EQ(spark->vertices[2].u, 1.0f);
        EXPECT_FLOAT_EQ(spark->vertices[2].v, 0.5f);
    }

    TEST_F(ParticleEmitterComponentTest, AnUnknownSpriteFallsBackToPlainSquares) {
        auto emitter = Emitter("Unknown");
        emitter->Burst(0.0f, 0.0f);

        const auto calls = Draw();
        ASSERT_EQ(calls.size(), 1u);
        EXPECT_EQ(calls[0].texture, nullptr);
        EXPECT_EQ(calls[0].vertices.size(), 2 * kVerticesPerQuad);
        EXPECT_FLOAT_EQ(calls[0].vertices[0].u, 0.0f);
        EXPECT_FLOAT_EQ(calls[0].vertices[0].v, 0.0f);
    }

    TEST_F(ParticleEmitterComponentTest, AnEditedEffectTakesEffectAtTheNextUpdateWithoutARestart) {
        auto emitter = Emitter("Manual");
        emitter->_effect.maxParticles = 4;
        emitter->Burst(0.0f, 0.0f, 10);
        EXPECT_EQ(emitter->GetLiveCount(), 4u);
        EXPECT_EQ(emitter->GetSimulation().GetDroppedCount(), 6u);

        const size_t capacity = emitter->GetSimulation().GetCapacity();
        emitter->_effect.maxParticles = 64;
        Frame();
        EXPECT_GE(emitter->GetSimulation().GetCapacity(), 64u) << "the pool grows with the field";
        EXPECT_GE(emitter->GetSimulation().GetCapacity(), capacity);
        emitter->Burst(0.0f, 0.0f, 10);
        EXPECT_EQ(emitter->GetLiveCount(), 14u);

        emitter->_effect.size = ParticleCurve(20.0f, 20.0f);
        const auto calls = Draw();
        ASSERT_EQ(calls.size(), 3u) << "OneShot and Glow are still alive, Manual is the last";
        EXPECT_NEAR(calls.back().rect.w, 20.0f, kTolerance) << "the curves are read at draw time";
    }

    TEST_F(ParticleEmitterComponentTest, StopAndClearEndTheEmission) {
        auto emitter = Emitter("Loop");
        emitter->Play();
        Frames(4);
        EXPECT_GT(emitter->GetLiveCount(), 0u);
        emitter->Stop();
        EXPECT_FALSE(emitter->IsEmitting());
        const size_t live = emitter->GetLiveCount();
        Frames(2);
        EXPECT_EQ(emitter->GetLiveCount(), live) << "none are added, the others have a second to live";

        emitter->Clear();
        EXPECT_EQ(emitter->GetLiveCount(), 0u);
        EXPECT_FALSE(emitter->NeedsFrames());
    }

    TEST_F(ParticleEmitterComponentTest, ThePropertyVisitorSeesTheEffectsFieldsNestedHelpersIncluded) {
        auto emitter = Emitter("Manual");
        NameRecorder recorder;
        emitter->AcceptPropertyVisitor(recorder);
        for (const char* name : {"effect",   "playOnStart", "burstAction", "looping", "rate",     "burstCount", "maxParticles", "seed", "shape", "direction", "spread",
                                 "gravityY", "size",        "rotation",    "color",   "spriteId", "blend",      "frameClass",   "from", "to",    "ease"}) {
            EXPECT_TRUE(recorder.Has(name)) << name;
        }
    }

    TEST_F(ParticleEmitterComponentTest, AReflectedRoundTripKeepsTheEffect) {
        auto source = Emitter("Manual");
        source->_playOnStart = false;
        source->_effect.looping = true;
        source->_effect.rate = 12.5f;
        source->_effect.blend = BlendMode::Add;
        source->_effect.shape = ParticleShape::Rect;
        source->_effect.size = ParticleCurve(30.0f, 1.0f, EaseType::QuadOut);
        source->_effect.color.to = Color(1, 2, 3, 4);
        source->_effect.spriteId = PoolString::Intern(kSparkSprite);

        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("component"));
        source->Serialize(serializer);
        serializer.EndObject();
        const eastl::string text = serializer.SaveToString();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(text)) << text.c_str();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        auto copy = New<ParticleEmitterComponent>();
        copy->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_FALSE(deserializer.HasErrors());

        EXPECT_FALSE(copy->_playOnStart);
        EXPECT_EQ(copy->_burstAction, "Pop"_intern);
        EXPECT_TRUE(copy->_effect.looping);
        EXPECT_FLOAT_EQ(copy->_effect.rate, 12.5f);
        EXPECT_EQ(copy->_effect.blend, BlendMode::Add);
        EXPECT_EQ(copy->_effect.shape, ParticleShape::Rect);
        EXPECT_FLOAT_EQ(copy->_effect.size.from, 30.0f);
        EXPECT_EQ(copy->_effect.size.ease, EaseType::QuadOut);
        ExpectColor(copy->_effect.color.to, Color(1, 2, 3, 4));
        EXPECT_EQ(copy->_effect.spriteId, PoolString::Intern(kSparkSprite));
    }

}  // namespace BECore::Tests
