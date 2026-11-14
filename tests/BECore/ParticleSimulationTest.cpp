#include <BECore/Particles/ParticleRandom.h>
#include <BECore/Particles/ParticleSimulation.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kStep = 1.0f / 32.0f;  // a power of two: the sums of steps are exact
        constexpr float kTolerance = 1e-3f;
        constexpr Color kWhite{255, 255, 255, 255};
        const Rect kPointArea{100.0f, 200.0f, 0.0f, 0.0f};

        /// Every range collapsed to one value, so the position of a particle is a closed form: velocity (100, 0), gravity (0, 50)
        ParticleEffect MakeFixedEffect() {
            ParticleEffect effect;
            effect.looping = false;
            effect.burstCount = 4;
            effect.maxParticles = 64;
            effect.lifetimeMin = 2.0f;
            effect.lifetimeMax = 2.0f;
            effect.speedMin = 100.0f;
            effect.speedMax = 100.0f;
            effect.direction = 0.0f;
            effect.spread = 0.0f;
            effect.gravityX = 0.0f;
            effect.gravityY = 50.0f;
            effect.size = ParticleCurve(10.0f, 10.0f);
            effect.color.from = kWhite;
            effect.color.to = kWhite;
            return effect;
        }

        /// Random lifetimes, speeds, angles, shape and jitters: nothing is a closed form, so only a rule of the simulation can make two runs agree
        ParticleEffect MakeRandomEffect() {
            ParticleEffect effect;
            effect.looping = true;
            effect.rate = 10.0f;
            effect.maxParticles = 128;
            effect.seed = 77;
            effect.lifetimeMin = 0.5f;
            effect.lifetimeMax = 1.5f;
            effect.speedMin = 20.0f;
            effect.speedMax = 80.0f;
            effect.direction = 270.0f;
            effect.spread = 90.0f;
            effect.gravityY = 100.0f;
            effect.shape = ParticleShape::Circle;
            effect.shapeRadius = 5.0f;
            effect.sizeJitter = 0.5f;
            effect.rotationJitter = 45.0f;
            return effect;
        }

        void Step(ParticleSimulation& sim, const ParticleEffect& effect, const Rect& area, float dt, int count) {
            for (int i = 0; i < count; ++i) {
                sim.Update(effect, area, dt);
            }
        }

        void ExpectNear(Vec2 actual, float x, float y, float tolerance = kTolerance) {
            EXPECT_NEAR(actual.x, x, tolerance);
            EXPECT_NEAR(actual.y, y, tolerance);
        }

        void ExpectColorEq(Color actual, Color expected) {
            EXPECT_EQ(actual.r, expected.r);
            EXPECT_EQ(actual.g, expected.g);
            EXPECT_EQ(actual.b, expected.b);
            EXPECT_EQ(actual.a, expected.a);
        }

        static_assert(ParticleRandom::SplitMix64(0) == 0xE220A8397B1DCDAFull);
        static_assert(ParticleRandom::Hash(1, 0, ParticleStream::Lifetime) == 0xE5DD105A383644F7ull);
        static_assert(ParticleRandom::Hash(1, 1, ParticleStream::Angle) == 0x4B6D18C1EF2A27F8ull);
        static_assert(ParticleRandom::Hash(42, 7, ParticleStream::Speed) == 0x5BC7E8AE4FD36BB0ull);
        static_assert(ParticleRandom::Hash(0xFFFFFFFFu, (1ull << 63) | 3, ParticleStream::ShapeB) == 0xF11DEA949C23A47Cull);

    }  // namespace

    TEST(ParticleSimulationTest, CompileTimeChecks) {}

    TEST(ParticleSimulationTest, TheHashIsPinnedAndTheStreamsDiffer) {
        // The top 24 bits of the pinned hashes over 2^24 (the same numbers from an independent implementation)
        EXPECT_FLOAT_EQ(ParticleRandom::Unit(1, 0, ParticleStream::Lifetime), 0.8979043960571289f);
        EXPECT_FLOAT_EQ(ParticleRandom::Unit(1, 1, ParticleStream::Angle), 0.2946333885192871f);
        EXPECT_FLOAT_EQ(ParticleRandom::Unit(42, 7, ParticleStream::Speed), 0.3585190773010254f);
        EXPECT_NE(ParticleRandom::Hash(5, 3, ParticleStream::Lifetime), ParticleRandom::Hash(5, 3, ParticleStream::Speed));
        EXPECT_NE(ParticleRandom::Hash(5, 3, ParticleStream::Lifetime), ParticleRandom::Hash(5, 4, ParticleStream::Lifetime));
        EXPECT_NE(ParticleRandom::Hash(5, 3, ParticleStream::Lifetime), ParticleRandom::Hash(6, 3, ParticleStream::Lifetime));

        // A unit number is in [0, 1) and spreads over it
        float lowest = 1.0f;
        float highest = 0.0f;
        for (uint64_t serial = 0; serial < 2000; ++serial) {
            const float value = ParticleRandom::Unit(9, serial, ParticleStream::Angle);
            ASSERT_GE(value, 0.0f);
            ASSERT_LT(value, 1.0f);
            lowest = std::min(lowest, value);
            highest = std::max(highest, value);
        }
        EXPECT_LT(lowest, 0.01f);
        EXPECT_GT(highest, 0.99f);
    }

    TEST(ParticleSimulationTest, APositionIsTheClosedFormOfItsAge) {
        const ParticleEffect effect = MakeFixedEffect();
        ParticleSimulation sim;
        sim.Play();
        EXPECT_TRUE(sim.IsEmitting());
        EXPECT_EQ(sim.GetLiveCount(), 0u);  // the burst is fired by the first update, which knows the area

        sim.Update(effect, kPointArea, 0.0f);
        ASSERT_EQ(sim.GetLiveCount(), 4u);
        for (const Particle& particle : sim.GetParticles()) {
            ExpectNear(sim.PositionOf(particle, effect), 100.0f, 200.0f);
        }

        for (int step = 1; step <= 32; ++step) {
            sim.Update(effect, kPointArea, kStep);
            const float age = static_cast<float>(step) * kStep;
            for (const Particle& particle : sim.GetParticles()) {
                // origin + velocity * age + gravity * age^2 / 2
                ExpectNear(sim.PositionOf(particle, effect), 100.0f + 100.0f * age, 200.0f + 25.0f * age * age);
            }
        }
        EXPECT_DOUBLE_EQ(sim.GetTime(), 1.0);
        ASSERT_EQ(sim.GetLiveCount(), 4u);
        ExpectNear(sim.PositionOf(sim.GetParticles()[0], effect), 200.0f, 225.0f);
    }

    TEST(ParticleSimulationTest, TheSameGameTimeGivesTheSamePictureAtAnyFrameRate) {
        const ParticleEffect effect = MakeRandomEffect();
        const Rect area{0.0f, 0.0f, 40.0f, 20.0f};

        ParticleSimulation fine;
        fine.Play();
        Step(fine, effect, area, 1.0f / 60.0f, 45);
        ParticleSimulation coarse;
        coarse.Play();
        Step(coarse, effect, area, 0.25f, 3);

        EXPECT_NEAR(fine.GetTime(), 0.75, 1e-6);
        EXPECT_DOUBLE_EQ(coarse.GetTime(), 0.75);
        ASSERT_GT(fine.GetLiveCount(), 3u);
        ASSERT_EQ(fine.GetLiveCount(), coarse.GetLiveCount());
        for (size_t i = 0; i < fine.GetLiveCount(); ++i) {
            const Particle& a = fine.GetParticles()[i];
            const Particle& b = coarse.GetParticles()[i];
            EXPECT_NEAR(a.spawnTime, b.spawnTime, 1e-6) << i;
            ExpectNear(fine.PositionOf(a, effect), coarse.PositionOf(b, effect).x, coarse.PositionOf(b, effect).y);
            EXPECT_FLOAT_EQ(a.sizeScale, b.sizeScale);
        }

        // The same picture as geometry
        Vector<GeometryVertex> fineVertices;
        Vector<GeometryVertex> coarseVertices;
        Vector<int> indices;
        const size_t quads = fine.BuildGeometry(effect, Rect{}, Vec2{}, fineVertices, indices);
        ASSERT_EQ(coarse.BuildGeometry(effect, Rect{}, Vec2{}, coarseVertices, indices), quads);
        for (size_t i = 0; i < quads * 4; ++i) {
            EXPECT_NEAR(fineVertices[i].x, coarseVertices[i].x, kTolerance) << i;
            EXPECT_NEAR(fineVertices[i].y, coarseVertices[i].y, kTolerance) << i;
        }
    }

    TEST(ParticleSimulationTest, TheSameSeedGivesTheSamePictureAndAnotherSeedAnother) {
        ParticleEffect effect = MakeRandomEffect();
        ParticleSimulation first;
        ParticleSimulation second;
        first.Play();
        second.Play();
        Step(first, effect, kPointArea, kStep, 20);
        Step(second, effect, kPointArea, kStep, 20);
        ASSERT_EQ(first.GetLiveCount(), second.GetLiveCount());
        ASSERT_GT(first.GetLiveCount(), 0u);
        for (size_t i = 0; i < first.GetLiveCount(); ++i) {
            const Vec2 a = first.PositionOf(first.GetParticles()[i], effect);
            const Vec2 b = second.PositionOf(second.GetParticles()[i], effect);
            EXPECT_EQ(a.x, b.x);
            EXPECT_EQ(a.y, b.y);
        }

        effect.seed = 78;
        ParticleSimulation other;
        other.Play();
        Step(other, effect, kPointArea, kStep, 20);
        ASSERT_EQ(other.GetLiveCount(), first.GetLiveCount());
        bool anyDifferent = false;
        for (size_t i = 0; i < other.GetLiveCount(); ++i) {
            anyDifferent |= other.GetParticles()[i].velocityX != first.GetParticles()[i].velocityX;
        }
        EXPECT_TRUE(anyDifferent);
    }

    TEST(ParticleSimulationTest, ALoopingEmitterSpawnsTheRateTimesTheTime) {
        ParticleEffect effect = MakeFixedEffect();
        effect.looping = true;
        effect.rate = 10.0f;
        effect.lifetimeMin = 10.0f;
        effect.lifetimeMax = 10.0f;

        ParticleSimulation steps;
        steps.Play();
        Step(steps, effect, kPointArea, kStep, 32);  // one second
        EXPECT_EQ(steps.GetLiveCount(), 10u);
        EXPECT_EQ(steps.GetDroppedCount(), 0u);

        // Long frames lose nothing: the number due is a function of the time, and each is born at its scheduled moment
        ParticleSimulation frames;
        frames.Play();
        const size_t expected[] = {2, 5, 7, 10};
        for (const size_t count : expected) {
            frames.Update(effect, kPointArea, 0.25f);
            EXPECT_EQ(frames.GetLiveCount(), count);
        }
        EXPECT_NEAR(frames.GetParticles()[0].spawnTime, 0.1, 1e-9);
        EXPECT_NEAR(frames.GetParticles()[1].spawnTime, 0.2, 1e-9);
        EXPECT_NEAR(frames.GetParticles()[9].spawnTime, 1.0, 1e-9);
    }

    TEST(ParticleSimulationTest, ALoopingEmitterPlaysUntilStopped) {
        ParticleEffect effect = MakeFixedEffect();
        effect.looping = true;
        effect.rate = 8.0f;
        effect.lifetimeMin = 0.5f;
        effect.lifetimeMax = 0.5f;
        ParticleSimulation sim;
        sim.Play();
        Step(sim, effect, kPointArea, kStep, 64);
        EXPECT_TRUE(sim.IsEmitting());
        EXPECT_TRUE(sim.NeedsFrames());
        EXPECT_GT(sim.GetLiveCount(), 0u);

        sim.Stop();
        EXPECT_FALSE(sim.IsEmitting());
        EXPECT_TRUE(sim.NeedsFrames());  // the live ones finish
        const size_t live = sim.GetLiveCount();
        Step(sim, effect, kPointArea, kStep, 1);
        EXPECT_LE(sim.GetLiveCount(), live);
        Step(sim, effect, kPointArea, kStep, 32);
        EXPECT_EQ(sim.GetLiveCount(), 0u);
        EXPECT_FALSE(sim.NeedsFrames());
    }

    TEST(ParticleSimulationTest, AOneShotEndsWhenTheLastParticleDied) {
        ParticleEffect effect = MakeFixedEffect();
        effect.lifetimeMin = 1.0f;
        effect.lifetimeMax = 1.0f;
        effect.burstCount = 5;

        ParticleSimulation sim;
        EXPECT_FALSE(sim.NeedsFrames());
        sim.Play();
        EXPECT_TRUE(sim.NeedsFrames());  // from Play, before any update
        sim.Update(effect, kPointArea, 0.0f);
        EXPECT_EQ(sim.GetLiveCount(), 5u);
        EXPECT_FALSE(sim.IsEmitting());  // fired
        EXPECT_TRUE(sim.NeedsFrames());

        Step(sim, effect, kPointArea, kStep, 31);
        EXPECT_EQ(sim.GetLiveCount(), 5u);
        EXPECT_TRUE(sim.NeedsFrames());
        Step(sim, effect, kPointArea, kStep, 1);  // time 1.0: the lifetime is over
        EXPECT_EQ(sim.GetLiveCount(), 0u);
        EXPECT_FALSE(sim.NeedsFrames());
        EXPECT_EQ(sim.GetDroppedCount(), 0u);
    }

    TEST(ParticleSimulationTest, AOneShotThatArrivesInALongFrameIsNotLate) {
        ParticleEffect effect = MakeFixedEffect();
        effect.lifetimeMin = 1.0f;
        effect.lifetimeMax = 1.0f;
        effect.burstCount = 3;
        ParticleSimulation sim;
        sim.Play();
        sim.Update(effect, kPointArea, 0.5f);  // fired at the moment of Play, half a second ago
        ASSERT_EQ(sim.GetLiveCount(), 3u);
        EXPECT_NEAR(sim.GetParticles()[0].spawnTime, 0.0, 1e-9);
        ExpectNear(sim.PositionOf(sim.GetParticles()[0], effect), 150.0f, 200.0f + 25.0f * 0.25f);

        // A frame longer than the life: dead before anyone saw it, and not counted as dropped
        ParticleSimulation late;
        late.Play();
        late.Update(effect, kPointArea, 5.0f);
        EXPECT_EQ(late.GetLiveCount(), 0u);
        EXPECT_FALSE(late.NeedsFrames());
        EXPECT_EQ(late.GetDroppedCount(), 0u);
    }

    TEST(ParticleSimulationTest, PlayAgainReplaysTheSamePictureFromItsOwnStart) {
        ParticleEffect random = MakeRandomEffect();
        random.looping = false;
        random.burstCount = 6;
        ParticleSimulation sim;
        sim.Play();
        Step(sim, random, kPointArea, kStep, 4);
        const Particle first = sim.GetParticles()[2];
        sim.Clear();

        Step(sim, random, kPointArea, kStep, 8);  // time goes on while nothing plays
        sim.Play();
        sim.Update(random, kPointArea, 0.0f);
        ASSERT_EQ(sim.GetLiveCount(), 6u);
        const Particle& again = sim.GetParticles()[2];
        EXPECT_EQ(again.velocityX, first.velocityX);
        EXPECT_EQ(again.velocityY, first.velocityY);
        EXPECT_EQ(again.originX, first.originX);
        EXPECT_EQ(again.lifetime, first.lifetime);
    }

    TEST(ParticleSimulationTest, TheMaxParticlesHoldsAndTheRestIsCounted) {
        ParticleEffect effect = MakeFixedEffect();
        effect.maxParticles = 20;
        ParticleSimulation burst;
        burst.Burst(effect, Vec2(0.0f, 0.0f), 50, kWhite);
        EXPECT_EQ(burst.GetLiveCount(), 20u);
        EXPECT_EQ(burst.GetDroppedCount(), 30u);

        effect.looping = true;
        effect.rate = 1000.0f;
        effect.maxParticles = 10;
        effect.lifetimeMin = 10.0f;
        effect.lifetimeMax = 10.0f;
        ParticleSimulation loop;
        loop.Play();
        Step(loop, effect, kPointArea, 0.25f, 4);  // 1000 are due in a second
        EXPECT_EQ(loop.GetLiveCount(), 10u);
        EXPECT_EQ(loop.GetDroppedCount(), 990u);

        loop.Clear();
        EXPECT_EQ(loop.GetLiveCount(), 0u);
        EXPECT_EQ(loop.GetDroppedCount(), 0u);
    }

    TEST(ParticleSimulationTest, TheCurvesAreExactAtTheEnds) {
        ParticleEffect effect;
        effect.size = ParticleCurve(10.0f, 2.0f, EaseType::QuadIn);
        effect.rotation = ParticleCurve(0.0f, 90.0f, EaseType::Linear);
        effect.color.from = Color(255, 0, 0, 255);
        effect.color.to = Color(0, 0, 255, 0);
        effect.color.ease = EaseType::Linear;

        const ParticleLook start = ParticleSimulation::SampleLook(effect, 0.0f);
        EXPECT_EQ(start.size, 10.0f);
        EXPECT_EQ(start.rotation, 0.0f);
        ExpectColorEq(start.color, Color(255, 0, 0, 255));

        const ParticleLook middle = ParticleSimulation::SampleLook(effect, 0.5f);
        EXPECT_FLOAT_EQ(middle.size, 8.0f);  // QuadIn(0.5) = 0.25
        EXPECT_FLOAT_EQ(middle.rotation, 45.0f);
        ExpectColorEq(middle.color, Color(128, 0, 128, 128));

        const ParticleLook end = ParticleSimulation::SampleLook(effect, 1.0f);
        EXPECT_EQ(end.size, 2.0f);
        EXPECT_EQ(end.rotation, 90.0f);
        ExpectColorEq(end.color, Color(0, 0, 255, 0));

        // An overshooting ease never gives a negative size
        effect.size = ParticleCurve(1.0f, 0.0f, EaseType::BackOut);
        for (int i = 0; i <= 20; ++i) {
            EXPECT_GE(ParticleSimulation::SampleLook(effect, static_cast<float>(i) / 20.0f).size, 0.0f);
        }
    }

    TEST(ParticleSimulationTest, TheGeometryIsOneRotatedSquarePerParticle) {
        ParticleEffect effect = MakeFixedEffect();
        effect.burstCount = 2;
        effect.gravityY = 0.0f;
        effect.speedMin = 0.0f;
        effect.speedMax = 0.0f;
        effect.rotation = ParticleCurve(90.0f, 90.0f);
        ParticleSimulation sim;
        sim.Play();
        sim.Update(effect, kPointArea, 0.0f);

        Vector<GeometryVertex> vertices;
        Vector<int> indices;
        ParticleSimulation::ReserveGeometry(effect.maxParticles, vertices, indices);
        const size_t quads = sim.BuildGeometry(effect, Rect{}, Vec2{}, vertices, indices);
        ASSERT_EQ(quads, 2u);
        ASSERT_EQ(vertices.size(), 8u);
        ASSERT_GE(indices.size(), 12u);
        const int expectedIndices[] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
        for (size_t i = 0; i < 12; ++i) {
            EXPECT_EQ(indices[i], expectedIndices[i]) << i;
        }

        // Clockwise by 90 degrees about (100, 200): the top left corner (-5, -5) goes to (5, -5)
        EXPECT_NEAR(vertices[0].x, 105.0f, kTolerance);
        EXPECT_NEAR(vertices[0].y, 195.0f, kTolerance);
        EXPECT_NEAR(vertices[1].x, 105.0f, kTolerance);
        EXPECT_NEAR(vertices[1].y, 205.0f, kTolerance);
        EXPECT_NEAR(vertices[2].x, 95.0f, kTolerance);
        EXPECT_NEAR(vertices[2].y, 205.0f, kTolerance);
        ExpectColorEq(vertices[0].color, kWhite);
        EXPECT_EQ(vertices[0].u, 0.0f);  // untextured
        EXPECT_EQ(vertices[0].v, 0.0f);

        // No live particle, no quad
        sim.Clear();
        EXPECT_EQ(sim.BuildGeometry(effect, Rect{}, Vec2{}, vertices, indices), 0u);
        EXPECT_TRUE(vertices.empty());
    }

    TEST(ParticleSimulationTest, TheUVsAreTheSourceRectOverTheTexture) {
        ParticleEffect effect = MakeFixedEffect();
        ParticleSimulation sim;
        sim.Play();
        sim.Update(effect, kPointArea, 0.0f);
        Vector<GeometryVertex> vertices;
        Vector<int> indices;

        sim.BuildGeometry(effect, Rect(16.0f, 8.0f, 16.0f, 16.0f), Vec2(64.0f, 32.0f), vertices, indices);
        EXPECT_FLOAT_EQ(vertices[0].u, 0.25f);
        EXPECT_FLOAT_EQ(vertices[0].v, 0.25f);
        EXPECT_FLOAT_EQ(vertices[1].u, 0.5f);
        EXPECT_FLOAT_EQ(vertices[1].v, 0.25f);
        EXPECT_FLOAT_EQ(vertices[2].u, 0.5f);
        EXPECT_FLOAT_EQ(vertices[2].v, 0.75f);
        EXPECT_FLOAT_EQ(vertices[3].u, 0.25f);
        EXPECT_FLOAT_EQ(vertices[3].v, 0.75f);

        // An empty source is the whole texture
        sim.BuildGeometry(effect, Rect{}, Vec2(64.0f, 32.0f), vertices, indices);
        EXPECT_FLOAT_EQ(vertices[0].u, 0.0f);
        EXPECT_FLOAT_EQ(vertices[2].u, 1.0f);
        EXPECT_FLOAT_EQ(vertices[2].v, 1.0f);
    }

    TEST(ParticleSimulationTest, TheColorFollowsTheCurveOfTheAge) {
        ParticleEffect effect = MakeFixedEffect();
        effect.color.from = Color(255, 255, 255, 255);
        effect.color.to = Color(255, 255, 255, 0);
        ParticleSimulation sim;
        sim.Play();
        sim.Update(effect, kPointArea, 0.0f);
        Vector<GeometryVertex> vertices;
        Vector<int> indices;
        sim.BuildGeometry(effect, Rect{}, Vec2{}, vertices, indices);
        EXPECT_EQ(vertices[0].color.a, 255);
        Step(sim, effect, kPointArea, kStep, 32);  // age 1 s of a 2 s life
        sim.BuildGeometry(effect, Rect{}, Vec2{}, vertices, indices);
        EXPECT_EQ(vertices[0].color.a, 128);
    }

    TEST(ParticleSimulationTest, ABurstTintTouchesOnlyItsOwnParticles) {
        const ParticleEffect effect = MakeFixedEffect();
        ParticleSimulation sim;
        const Color red(255, 0, 0, 255);
        sim.Burst(effect, Vec2(10.0f, 20.0f), 3, red);
        sim.Burst(effect, Vec2(10.0f, 20.0f), 2);
        ASSERT_EQ(sim.GetLiveCount(), 5u);
        for (size_t i = 0; i < 3; ++i) {
            ExpectColorEq(sim.GetParticles()[i].tint, red);
        }
        for (size_t i = 3; i < 5; ++i) {
            ExpectColorEq(sim.GetParticles()[i].tint, kWhite);
        }

        Vector<GeometryVertex> vertices;
        Vector<int> indices;
        sim.BuildGeometry(effect, Rect{}, Vec2{}, vertices, indices);
        ExpectColorEq(vertices[0].color, red);         // the first particle: white times red
        ExpectColorEq(vertices[3 * 4].color, kWhite);  // the fourth is not tinted
        ExpectNear(sim.PositionOf(sim.GetParticles()[0], effect), 10.0f, 20.0f);
    }

    TEST(ParticleSimulationTest, TheShapeDecidesWhereParticlesAreBorn) {
        ParticleEffect effect = MakeFixedEffect();
        effect.speedMin = 0.0f;
        effect.speedMax = 0.0f;
        effect.gravityY = 0.0f;
        effect.burstCount = 64;
        const Rect area{10.0f, 20.0f, 40.0f, 10.0f};

        effect.shape = ParticleShape::Rect;
        ParticleSimulation rectSim;
        rectSim.Play();
        rectSim.Update(effect, area, 0.0f);
        ASSERT_EQ(rectSim.GetLiveCount(), 64u);
        float minX = 1e9f;
        float maxX = -1e9f;
        for (const Particle& particle : rectSim.GetParticles()) {
            EXPECT_GE(particle.originX, 10.0f);
            EXPECT_LE(particle.originX, 50.0f);
            EXPECT_GE(particle.originY, 20.0f);
            EXPECT_LE(particle.originY, 30.0f);
            minX = std::min(minX, particle.originX);
            maxX = std::max(maxX, particle.originX);
        }
        EXPECT_GT(maxX - minX, 20.0f);  // spread over the width, not at one point

        effect.shape = ParticleShape::Circle;
        effect.shapeRadius = 8.0f;
        ParticleSimulation circleSim;
        circleSim.Play();
        circleSim.Update(effect, area, 0.0f);
        for (const Particle& particle : circleSim.GetParticles()) {
            const float dx = particle.originX - 30.0f;  // the center of the area
            const float dy = particle.originY - 25.0f;
            EXPECT_LE(std::sqrt(dx * dx + dy * dy), 8.0f + kTolerance);
        }

        effect.shape = ParticleShape::Point;
        ParticleSimulation pointSim;
        pointSim.Play();
        pointSim.Update(effect, area, 0.0f);
        for (const Particle& particle : pointSim.GetParticles()) {
            EXPECT_EQ(particle.originX, 30.0f);
            EXPECT_EQ(particle.originY, 25.0f);
        }
    }

    TEST(ParticleSimulationTest, ALiveParticleKeepsWhereItWasBorn) {
        const ParticleEffect effect = MakeFixedEffect();
        ParticleSimulation sim;
        sim.Play();
        sim.Update(effect, kPointArea, 0.0f);
        const Particle born = sim.GetParticles()[0];
        sim.Update(effect, Rect(500.0f, 500.0f, 0.0f, 0.0f), kStep);  // the emitter moved away
        EXPECT_EQ(sim.GetParticles()[0].originX, born.originX);
        EXPECT_EQ(sim.GetParticles()[0].originY, born.originY);
    }

    TEST(ParticleSimulationTest, ADtThatIsNotFiniteOrIsNegativeAdvancesNothing) {
        const ParticleEffect effect = MakeFixedEffect();
        ParticleSimulation sim;
        sim.Play();
        sim.Update(effect, kPointArea, kStep);
        const double time = sim.GetTime();
        sim.Update(effect, kPointArea, -1.0f);
        sim.Update(effect, kPointArea, std::numeric_limits<float>::quiet_NaN());
        sim.Update(effect, kPointArea, std::numeric_limits<float>::infinity());
        sim.Update(effect, kPointArea, 0.0f);
        EXPECT_EQ(sim.GetTime(), time);
        EXPECT_EQ(sim.GetLiveCount(), 4u);
    }

    TEST(ParticleSimulationTest, NothingAllocatesOnceThePoolAndTheVectorsAreReserved) {
        ParticleEffect effect = MakeRandomEffect();
        effect.rate = 1000.0f;
        effect.maxParticles = 64;
        effect.lifetimeMin = 30.0f;
        effect.lifetimeMax = 30.0f;

        ParticleSimulation sim;
        Vector<GeometryVertex> vertices;
        Vector<int> indices;
        sim.Reserve(effect.maxParticles);
        ParticleSimulation::ReserveGeometry(effect.maxParticles, vertices, indices);
        sim.Play();
        Step(sim, effect, kPointArea, 0.25f, 1);  // the pool is full from here on
        ASSERT_EQ(sim.GetLiveCount(), 64u);

        const size_t poolCapacity = sim.GetCapacity();
        const size_t vertexCapacity = vertices.capacity();
        const size_t indexCapacity = indices.capacity();
#if BE_PROFILER_ALLOC
        const uint64_t before = AllocTracker::GetThreadCounters().count;
#endif
        for (int frame = 0; frame < 1000; ++frame) {
            sim.Update(effect, kPointArea, 1.0f / 60.0f);
            ASSERT_EQ(sim.BuildGeometry(effect, Rect{}, Vec2(64.0f, 64.0f), vertices, indices), 64u);
        }
#if BE_PROFILER_ALLOC
        EXPECT_EQ(AllocTracker::GetThreadCounters().count, before);
#endif
        EXPECT_EQ(sim.GetCapacity(), poolCapacity);
        EXPECT_EQ(vertices.capacity(), vertexCapacity);
        EXPECT_EQ(indices.capacity(), indexCapacity);
        EXPECT_GT(sim.GetDroppedCount(), 0u);

        // With no live particle too
        sim.Clear();
#if BE_PROFILER_ALLOC
        const uint64_t emptyBefore = AllocTracker::GetThreadCounters().count;
#endif
        for (int frame = 0; frame < 100; ++frame) {
            sim.Update(effect, kPointArea, 1.0f / 60.0f);
            EXPECT_EQ(sim.BuildGeometry(effect, Rect{}, Vec2{}, vertices, indices), 0u);
        }
#if BE_PROFILER_ALLOC
        EXPECT_EQ(AllocTracker::GetThreadCounters().count, emptyBefore);
#endif
        EXPECT_EQ(sim.GetCapacity(), poolCapacity);
    }

    TEST(ParticleSimulationTest, TheCapacityGrowsOnlyWhenTheFieldGrows) {
        ParticleEffect effect = MakeFixedEffect();
        effect.maxParticles = 32;
        ParticleSimulation sim;
        sim.Update(effect, kPointArea, kStep);
        const size_t capacity = sim.GetCapacity();
        EXPECT_GE(capacity, 32u);
        effect.maxParticles = 16;
        sim.Update(effect, kPointArea, kStep);
        EXPECT_EQ(sim.GetCapacity(), capacity);
        effect.maxParticles = 128;
        sim.Update(effect, kPointArea, kStep);
        EXPECT_GE(sim.GetCapacity(), 128u);
    }

    TEST(ParticleSimulationTest, HostileEffectsStayBounded) {
        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
        constexpr float kInfinity = std::numeric_limits<float>::infinity();

        ParticleEffect rate = MakeRandomEffect();
        rate.rate = 1e9f;
        rate.maxParticles = std::numeric_limits<int32_t>::max();
        rate.lifetimeMin = 1e9f;
        rate.lifetimeMax = 1e9f;
        ParticleSimulation rateSim;
        rateSim.Play();
        Step(rateSim, rate, kPointArea, 0.25f, 8);
        EXPECT_LE(rateSim.GetLiveCount(), static_cast<size_t>(ParticleLimits::kMaxParticles));
        EXPECT_LE(rateSim.GetCapacity(), static_cast<size_t>(ParticleLimits::kMaxParticles));

        ParticleEffect none = MakeRandomEffect();
        none.maxParticles = 0;
        none.rate = 500.0f;
        ParticleSimulation noneSim;
        noneSim.Play();
        Step(noneSim, none, kPointArea, 0.25f, 4);
        EXPECT_LE(noneSim.GetLiveCount(), static_cast<size_t>(ParticleLimits::kMinParticles));

        ParticleEffect nan = MakeRandomEffect();
        nan.rate = kNaN;
        nan.lifetimeMin = kNaN;
        nan.lifetimeMax = kNaN;
        nan.speedMin = kNaN;
        nan.speedMax = kInfinity;
        nan.direction = kNaN;
        nan.spread = kInfinity;
        nan.gravityX = kNaN;
        nan.shapeRadius = kNaN;
        nan.size = ParticleCurve(kNaN, kInfinity);
        nan.sizeJitter = kNaN;
        nan.burstCount = -5;
        ParticleSimulation nanSim;
        nanSim.Play();
        Vector<GeometryVertex> vertices;
        Vector<int> indices;
        for (int i = 0; i < 40; ++i) {
            nanSim.Update(nan, Rect(kNaN, kInfinity, kNaN, 5.0f), kStep);
        }
        nanSim.Burst(nan, Vec2(kNaN, kNaN), 10);
        const size_t quads = nanSim.BuildGeometry(nan, Rect{}, Vec2{}, vertices, indices);
        for (size_t i = 0; i < quads * 4; ++i) {
            ASSERT_TRUE(std::isfinite(vertices[i].x)) << i;
            ASSERT_TRUE(std::isfinite(vertices[i].y)) << i;
        }

        // Huge steps of time do not loop for long or overflow the schedule
        ParticleEffect looping = MakeRandomEffect();
        looping.rate = 10000.0f;
        ParticleSimulation hugeSim;
        hugeSim.Play();
        hugeSim.Update(looping, kPointArea, 1e9f);
        hugeSim.Update(looping, kPointArea, 3e38f);
        hugeSim.Update(looping, kPointArea, 3e38f);
        EXPECT_LE(hugeSim.GetLiveCount(), static_cast<size_t>(looping.maxParticles));

        // A burst of a hostile count is counted, not looped
        ParticleSimulation burstSim;
        burstSim.Burst(MakeFixedEffect(), Vec2(0.0f, 0.0f), std::numeric_limits<int32_t>::max());
        EXPECT_EQ(burstSim.GetLiveCount(), 64u);
        EXPECT_EQ(burstSim.GetDroppedCount(), static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) - 64u);
    }

}  // namespace BECore::Tests
