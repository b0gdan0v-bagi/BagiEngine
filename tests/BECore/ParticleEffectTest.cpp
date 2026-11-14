#include <BECore/Particles/ParticleEffect.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
        constexpr float kInfinity = std::numeric_limits<float>::infinity();

        String SaveEffect(const ParticleEffect& effect) {
            XmlSerializer serializer;
            if (serializer.BeginObject("effect")) {
                effect.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

        /// Reads the <effect> element of @p xml into @p out; the errors of the reader go to @p errors when given. Use through ASSERT_NO_FATAL_FAILURE.
        void LoadEffect(StringView xml, ParticleEffect& out, eastl::vector<DeserializeError>* errors = nullptr) {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(xml)) << xml.data();
            ASSERT_TRUE(deserializer.BeginObject("effect")) << xml.data();
            out.Deserialize(deserializer);
            deserializer.EndObject();
            if (errors != nullptr) {
                *errors = deserializer.GetErrors();
            }
        }

        /// Every field away from its default
        ParticleEffect MakeChangedEffect() {
            ParticleEffect effect;
            effect.looping = true;
            effect.rate = 45.5f;
            effect.burstCount = 7;
            effect.maxParticles = 512;
            effect.seed = 987654321u;
            effect.lifetimeMin = 0.25f;
            effect.lifetimeMax = 1.75f;
            effect.shape = ParticleShape::Circle;
            effect.shapeRadius = 12.5f;
            effect.speedMin = 10.0f;
            effect.speedMax = 250.0f;
            effect.direction = 270.0f;
            effect.spread = 45.0f;
            effect.gravityX = -3.5f;
            effect.gravityY = 400.0f;
            effect.size = ParticleCurve(16.0f, 2.0f, EaseType::QuadOut);
            effect.sizeJitter = 0.25f;
            effect.rotation = ParticleCurve(-90.0f, 180.0f, EaseType::SineInOut);
            effect.rotationJitter = 30.0f;
            effect.color.from = Color(255, 200, 50, 255);
            effect.color.to = Color(200, 20, 10, 0);
            effect.color.ease = EaseType::CubicIn;
            effect.spriteId = PoolString::Intern("Spark");
            effect.texturePath = PoolString::Intern("assets/spark.png");
            effect.blend = BlendMode::Add;
            effect.frameClass = ParticleFrameClass::DecorativeLoop;
            return effect;
        }

        void ExpectCurveEq(const ParticleCurve& actual, const ParticleCurve& expected) {
            EXPECT_FLOAT_EQ(actual.from, expected.from);
            EXPECT_FLOAT_EQ(actual.to, expected.to);
            EXPECT_EQ(actual.ease, expected.ease);
        }

        void ExpectColorEq(const Color& actual, const Color& expected) {
            EXPECT_EQ(actual.r, expected.r);
            EXPECT_EQ(actual.g, expected.g);
            EXPECT_EQ(actual.b, expected.b);
            EXPECT_EQ(actual.a, expected.a);
        }

        void ExpectEffectEq(const ParticleEffect& actual, const ParticleEffect& expected) {
            EXPECT_EQ(actual.looping, expected.looping);
            EXPECT_FLOAT_EQ(actual.rate, expected.rate);
            EXPECT_EQ(actual.burstCount, expected.burstCount);
            EXPECT_EQ(actual.maxParticles, expected.maxParticles);
            EXPECT_EQ(actual.seed, expected.seed);
            EXPECT_FLOAT_EQ(actual.lifetimeMin, expected.lifetimeMin);
            EXPECT_FLOAT_EQ(actual.lifetimeMax, expected.lifetimeMax);
            EXPECT_EQ(actual.shape, expected.shape);
            EXPECT_FLOAT_EQ(actual.shapeRadius, expected.shapeRadius);
            EXPECT_FLOAT_EQ(actual.speedMin, expected.speedMin);
            EXPECT_FLOAT_EQ(actual.speedMax, expected.speedMax);
            EXPECT_FLOAT_EQ(actual.direction, expected.direction);
            EXPECT_FLOAT_EQ(actual.spread, expected.spread);
            EXPECT_FLOAT_EQ(actual.gravityX, expected.gravityX);
            EXPECT_FLOAT_EQ(actual.gravityY, expected.gravityY);
            ExpectCurveEq(actual.size, expected.size);
            EXPECT_FLOAT_EQ(actual.sizeJitter, expected.sizeJitter);
            ExpectCurveEq(actual.rotation, expected.rotation);
            EXPECT_FLOAT_EQ(actual.rotationJitter, expected.rotationJitter);
            ExpectColorEq(actual.color.from, expected.color.from);
            ExpectColorEq(actual.color.to, expected.color.to);
            EXPECT_EQ(actual.color.ease, expected.color.ease);
            EXPECT_EQ(actual.spriteId, expected.spriteId);
            EXPECT_EQ(actual.texturePath, expected.texturePath);
            EXPECT_EQ(actual.blend, expected.blend);
            EXPECT_EQ(actual.frameClass, expected.frameClass);
        }

        /// The effect written as the text a scene file carries
        constexpr StringView kSceneText = R"(<root>
  <effect looping="true" rate="45.5" burstCount="7" maxParticles="512" seed="987654321"
          lifetimeMin="0.25" lifetimeMax="1.75" shape="Circle" shapeRadius="12.5"
          speedMin="10" speedMax="250" direction="270" spread="45" gravityX="-3.5" gravityY="400"
          sizeJitter="0.25" rotationJitter="30" spriteId="Spark" texturePath="assets/spark.png"
          blend="Add" frameClass="DecorativeLoop">
    <size from="16" to="2" ease="QuadOut"/>
    <rotation from="-90" to="180" ease="SineInOut"/>
    <color ease="CubicIn">
      <from r="255" g="200" b="50" a="255"/>
      <to r="200" g="20" b="10" a="0"/>
    </color>
  </effect>
</root>)";

    }  // namespace

    TEST(ParticleEffectTest, DefaultsAreAUsableSmallBurst) {
        const ParticleEffect effect;
        EXPECT_FALSE(effect.looping);
        EXPECT_GT(effect.burstCount, 0);
        EXPECT_GE(effect.maxParticles, effect.burstCount);
        EXPECT_LE(effect.lifetimeMin, effect.lifetimeMax);
        EXPECT_LE(effect.speedMin, effect.speedMax);
        EXPECT_EQ(effect.shape, ParticleShape::Point);
        EXPECT_FLOAT_EQ(effect.spread, 360.0f);
        EXPECT_GT(effect.size.from, 0.0f);
        EXPECT_EQ(effect.blend, BlendMode::Alpha);
        EXPECT_EQ(effect.frameClass, ParticleFrameClass::FiniteEffect);
        EXPECT_TRUE(effect.spriteId.Empty());
        EXPECT_TRUE(effect.texturePath.Empty());

        // Nothing for Sanitize to change: the defaults are inside every range
        ExpectEffectEq(effect.Sanitized(), effect);
    }

    TEST(ParticleEffectTest, RoundTripKeepsEveryField) {
        const ParticleEffect original = MakeChangedEffect();
        const String xml = SaveEffect(original);

        ParticleEffect loaded;
        eastl::vector<DeserializeError> errors;
        ASSERT_NO_FATAL_FAILURE(LoadEffect(xml.c_str(), loaded, &errors));
        EXPECT_TRUE(errors.empty());
        ExpectEffectEq(loaded, original);
    }

    TEST(ParticleEffectTest, SceneFileTextLoadsTheSameValues) {
        ParticleEffect loaded;
        eastl::vector<DeserializeError> errors;
        ASSERT_NO_FATAL_FAILURE(LoadEffect(kSceneText, loaded, &errors));
        EXPECT_TRUE(errors.empty());
        ExpectEffectEq(loaded, MakeChangedEffect());
    }

    TEST(ParticleEffectTest, MissingPartsKeepTheDefaults) {
        ParticleEffect loaded;
        ASSERT_NO_FATAL_FAILURE(LoadEffect(R"(<root><effect rate="5"/></root>)", loaded));
        const ParticleEffect defaults;
        EXPECT_FLOAT_EQ(loaded.rate, 5.0f);
        EXPECT_EQ(loaded.burstCount, defaults.burstCount);
        ExpectCurveEq(loaded.size, defaults.size);
        ExpectColorEq(loaded.color.to, defaults.color.to);
    }

    TEST(ParticleEffectTest, BadEnumTextIsReportedAndKeepsTheDefault) {
        ParticleEffect loaded;
        eastl::vector<DeserializeError> errors;
        ASSERT_NO_FATAL_FAILURE(LoadEffect(R"(<root><effect shape="Hexagon" blend="Glow" frameClass="Sometimes">
            <size from="3" to="1" ease="Wobble"/></effect></root>)",
                                           loaded, &errors));
        EXPECT_FALSE(errors.empty());
        EXPECT_EQ(loaded.shape, ParticleShape::Point);
        EXPECT_EQ(loaded.blend, BlendMode::Alpha);
        EXPECT_EQ(loaded.frameClass, ParticleFrameClass::FiniteEffect);
        EXPECT_EQ(loaded.size.ease, EaseType::Linear);
        EXPECT_FLOAT_EQ(loaded.size.from, 3.0f);
    }

    TEST(ParticleEffectTest, SanitizeClampsCountsAndRates) {
        using namespace ParticleLimits;
        ParticleEffect effect;
        effect.rate = -5.0f;
        effect.burstCount = -3;
        effect.maxParticles = std::numeric_limits<int32_t>::max();
        ParticleEffect clamped = effect.Sanitized();
        EXPECT_FLOAT_EQ(clamped.rate, 0.0f);
        EXPECT_EQ(clamped.burstCount, 0);
        EXPECT_EQ(clamped.maxParticles, kMaxParticles);

        effect.rate = 1e9f;
        effect.burstCount = std::numeric_limits<int32_t>::max();
        effect.maxParticles = 0;
        clamped = effect.Sanitized();
        EXPECT_FLOAT_EQ(clamped.rate, kMaxRate);
        EXPECT_EQ(clamped.maxParticles, kMinParticles);
        EXPECT_EQ(clamped.burstCount, kMinParticles);

        // The effect itself keeps what was written
        EXPECT_EQ(effect.maxParticles, 0);
    }

    TEST(ParticleEffectTest, SanitizeOrdersRangesAndKeepsLifetimePositive) {
        using namespace ParticleLimits;
        ParticleEffect effect;
        effect.lifetimeMin = 3.0f;
        effect.lifetimeMax = -1.0f;
        effect.speedMin = 200.0f;
        effect.speedMax = 50.0f;
        effect.Sanitize();
        EXPECT_FLOAT_EQ(effect.lifetimeMin, kMinLifetime);
        EXPECT_FLOAT_EQ(effect.lifetimeMax, 3.0f);
        EXPECT_FLOAT_EQ(effect.speedMin, 50.0f);
        EXPECT_FLOAT_EQ(effect.speedMax, 200.0f);
    }

    TEST(ParticleEffectTest, SanitizeReplacesNotFiniteFloatsWithTheDefault) {
        const ParticleEffect defaults;
        ParticleEffect effect;
        effect.rate = kNaN;
        effect.lifetimeMin = kInfinity;
        effect.lifetimeMax = -kInfinity;
        effect.shapeRadius = kNaN;
        effect.speedMax = kNaN;
        effect.direction = kInfinity;
        effect.spread = kNaN;
        effect.gravityX = kNaN;
        effect.gravityY = -kInfinity;
        effect.size = ParticleCurve(kNaN, kInfinity);
        effect.sizeJitter = kNaN;
        effect.rotation = ParticleCurve(kInfinity, kNaN);
        effect.rotationJitter = kInfinity;
        effect.Sanitize();

        EXPECT_FLOAT_EQ(effect.rate, defaults.rate);
        EXPECT_FLOAT_EQ(effect.lifetimeMin, defaults.lifetimeMin);
        EXPECT_FLOAT_EQ(effect.lifetimeMax, defaults.lifetimeMax);
        EXPECT_FLOAT_EQ(effect.shapeRadius, defaults.shapeRadius);
        EXPECT_FLOAT_EQ(effect.speedMax, defaults.speedMax);
        EXPECT_FLOAT_EQ(effect.direction, defaults.direction);
        EXPECT_FLOAT_EQ(effect.spread, defaults.spread);
        EXPECT_FLOAT_EQ(effect.gravityX, defaults.gravityX);
        EXPECT_FLOAT_EQ(effect.gravityY, defaults.gravityY);
        ExpectCurveEq(effect.size, ParticleCurve(defaults.size.from, defaults.size.to, defaults.size.ease));
        EXPECT_FLOAT_EQ(effect.sizeJitter, defaults.sizeJitter);
        ExpectCurveEq(effect.rotation, defaults.rotation);
        EXPECT_FLOAT_EQ(effect.rotationJitter, defaults.rotationJitter);
    }

    TEST(ParticleEffectTest, SanitizeClampsAnglesSizesAndJitters) {
        using namespace ParticleLimits;
        ParticleEffect effect;
        effect.spread = 720.0f;
        effect.sizeJitter = 5.0f;
        effect.rotationJitter = 1000.0f;
        effect.size = ParticleCurve(-4.0f, 1e12f);
        effect.gravityX = 1e12f;
        effect.Sanitize();
        EXPECT_FLOAT_EQ(effect.spread, kFullCircle);
        EXPECT_FLOAT_EQ(effect.sizeJitter, kMaxJitter);
        EXPECT_FLOAT_EQ(effect.rotationJitter, kMaxRotationJitter);
        EXPECT_FLOAT_EQ(effect.size.from, 0.0f);
        EXPECT_FLOAT_EQ(effect.size.to, kMaxSize);
        EXPECT_FLOAT_EQ(effect.gravityX, kMaxGravity);
    }

    TEST(ParticleEffectTest, SanitizeReplacesAnEnumThatIsNoName) {
        ParticleEffect effect;
        effect.shape = static_cast<ParticleShape>(200);
        effect.blend = static_cast<BlendMode>(77);
        effect.frameClass = static_cast<ParticleFrameClass>(9);
        effect.size.ease = static_cast<EaseType>(250);
        effect.color.ease = static_cast<EaseType>(251);
        effect.rotation.ease = static_cast<EaseType>(252);
        effect.Sanitize();
        EXPECT_EQ(effect.shape, ParticleShape::Point);
        EXPECT_EQ(effect.blend, BlendMode::Alpha);
        EXPECT_EQ(effect.frameClass, ParticleFrameClass::FiniteEffect);
        EXPECT_EQ(effect.size.ease, EaseType::Linear);
        EXPECT_EQ(effect.color.ease, EaseType::Linear);
        EXPECT_EQ(effect.rotation.ease, EaseType::Linear);
    }

}  // namespace BECore::Tests
