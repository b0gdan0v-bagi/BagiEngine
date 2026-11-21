#include <BECore/Particles/ParticleSimulation.h>
#include <TaskSystem/ParallelFor.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/ThreadPool.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kFrameStep = 1.0f / 60.0f;
        constexpr Rect kArea{100.0f, 200.0f, 0.0f, 0.0f};
        constexpr Rect kSource{0.0f, 0.0f, 256.0f, 256.0f};
        constexpr Vec2 kTextureSize{256.0f, 256.0f};
        constexpr double kMicrosPerSecond = 1e6;

        /// Long-lived particles of a looping effect fill the pool in a second and then stay, so a frame always builds `maxParticles` of them
        constexpr float kLifetime = 100.0f;
        constexpr int32_t kFlipbookAxis = 4;
        constexpr float kFlipbookFps = 24.0f;

        /// A little over a second of 1/60 s steps: the rate is twice the pool, so the pool is full by then
        constexpr size_t kFillSteps = 70;
        constexpr size_t kBenchWarmupFrames = 40;
        constexpr size_t kBenchFrames = 400;
        constexpr size_t kSmokeFrames = 3;
        constexpr size_t kSmokeEmitters = 3;
        constexpr size_t kSmokeParticles = 700;

        /// The chunk the component uses (`ParticleEmitterComponent.cpp`, `kMinChunkParticles`); the sweep below shows what others would cost
        constexpr size_t kMinChunk = 128;

        ParticleEffect MakeBenchEffect(int32_t particles, uint32_t seed) {
            ParticleEffect effect;
            effect.looping = true;
            effect.rate = std::min(static_cast<float>(particles) * 2.0f, ParticleLimits::kMaxRate);
            effect.maxParticles = particles;
            effect.seed = seed;
            effect.lifetimeMin = kLifetime;
            effect.lifetimeMax = kLifetime;
            effect.speedMin = 20.0f;
            effect.speedMax = 80.0f;
            effect.spread = ParticleLimits::kFullCircle;
            effect.gravityY = 100.0f;
            effect.shape = ParticleShape::Circle;
            effect.shapeRadius = 5.0f;
            effect.sizeJitter = 0.5f;
            effect.rotationJitter = 45.0f;
            effect.size = ParticleCurve(4.0f, 12.0f, EaseType::QuadOut);
            effect.rotation = ParticleCurve(0.0f, 270.0f);
            effect.color.from = Color(255, 200, 50, 255);
            effect.color.to = Color(20, 40, 255, 0);
            effect.flipbook.columns = kFlipbookAxis;
            effect.flipbook.rows = kFlipbookAxis;
            effect.flipbook.mode = ParticleFlipbookMode::Loop;
            effect.flipbook.fps = kFlipbookFps;
            effect.flipbook.randomStart = true;
            return effect;
        }

        /// @p emitters pools of @p particles each, played and stepped until every pool is full
        struct Scene {
            Scene(size_t emitters, int32_t particles) {
                for (size_t k = 0; k < emitters; ++k) {
                    Emitter& emitter = _emitters.emplace_back();
                    emitter.effect = MakeBenchEffect(particles, static_cast<uint32_t>(k + 1));
                    emitter.simulation.Reserve(particles);
                    ParticleSimulation::ReserveGeometry(particles, emitter.vertices, emitter.indices);
                    emitter.simulation.Play();
                }
                for (size_t step = 0; step < kFillSteps; ++step) {
                    Step();
                }
            }

            void Step() {
                for (Emitter& emitter : _emitters) {
                    emitter.simulation.Update(emitter.effect, kArea, kFrameStep);
                }
            }

            void BuildSerial() {
                for (Emitter& emitter : _emitters) {
                    const auto frame = emitter.simulation.PrepareGeometry(emitter.effect, kSource, kTextureSize, emitter.vertices, emitter.indices);
                    emitter.simulation.BuildQuads(frame, 0, emitter.simulation.GetLiveCount(), emitter.vertices.data());
                }
            }

            void BuildParallel(ThreadPool& pool, size_t minChunk) {
                for (Emitter& emitter : _emitters) {
                    const auto frame = emitter.simulation.PrepareGeometry(emitter.effect, kSource, kTextureSize, emitter.vertices, emitter.indices);
                    const ParticleSimulation& simulation = emitter.simulation;
                    GeometryVertex* vertices = emitter.vertices.data();
                    ParallelFor(pool, simulation.GetLiveCount(), minChunk, [&](size_t begin, size_t end) { simulation.BuildQuads(frame, begin, end, vertices); });
                }
            }

            struct Emitter {
                ParticleEffect effect;
                ParticleSimulation simulation;
                Vector<GeometryVertex> vertices;
                Vector<int> indices;
            };
            Vector<Emitter> _emitters;
        };

        template <typename TBuild>
        double MedianMicros(Scene& scene, size_t frames, TBuild&& build) {
            Vector<double> micros;
            micros.reserve(frames);
            for (size_t frame = 0; frame < kBenchWarmupFrames + frames; ++frame) {
                scene.Step();
                const auto start = std::chrono::steady_clock::now();
                build();
                const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
                if (frame >= kBenchWarmupFrames) {
                    micros.push_back(elapsed.count() * kMicrosPerSecond);
                }
            }
            std::nth_element(micros.begin(), micros.begin() + static_cast<std::ptrdiff_t>(micros.size() / 2), micros.end());
            return micros[micros.size() / 2];
        }

    }  // namespace

    /// Both ways build the bytes the same; a small grid so that it costs nothing in an everyday run
    TEST(ParticleParallelBenchTest, SerialAndParallelBuildTheSameVertices) {
        ThreadPool pool(TaskManager::kMaxWorkerThreads);
        Scene serial(kSmokeEmitters, kSmokeParticles);
        Scene parallel(kSmokeEmitters, kSmokeParticles);
        for (size_t frame = 0; frame < kSmokeFrames; ++frame) {
            serial.Step();
            parallel.Step();
            serial.BuildSerial();
            parallel.BuildParallel(pool, kMinChunk / 8);
            for (size_t k = 0; k < kSmokeEmitters; ++k) {
                const auto& expected = serial._emitters[k];
                const auto& actual = parallel._emitters[k];
                ASSERT_EQ(expected.simulation.GetLiveCount(), kSmokeParticles);
                ASSERT_EQ(expected.vertices.size(), actual.vertices.size());
                EXPECT_EQ(std::memcmp(expected.vertices.data(), actual.vertices.data(), expected.vertices.size() * sizeof(GeometryVertex)), 0) << k;
            }
        }
    }

    /**
     * Run in Release (`macos-release`): `BagiEngineTests --gtest_also_run_disabled_tests --gtest_filter=ParticleParallelBenchTest.DISABLED_*`.
     * Prints the geometry of all emitters of a frame, serial against `ParallelFor` over four workers (what `TaskManager` creates), and a
     * sweep of the smallest chunk. It fixed `kDefaultParallelMinParticles` and `kMinChunkParticles` of `ParticleEmitterComponent.cpp`.
     *
     * Apple M4 Pro (14 cores, other loops running), the median of 400 frames, 4 workers, chunk 128, microseconds, serial / parallel:
     * 1 x 256 5.2 / 7.5 and 64 x 256 316 / 421 (forking loses), 1 x 512 10.2 / 9.3 and 64 x 512 642 / 659 (break even);
     * 1 x 1024 20 / 11.6, 8 x 1024 159 / 123, 64 x 1024 1264 / 900; 1 x 4096 81 / 29, 8 x 4096 631 / 238, 64 x 4096 5144 / 2040.
     * The chunk sweep at 8 x 1024: 64 -> 108, 128 -> 111, 256 -> 121, 512 -> 129, 1024 -> 158 (no split); at 1 x 4096 it stays
     * within 29 to 33. So parallel starts to win clearly at 1024 (the threshold) and 128 is the largest chunk within noise of the
     * best. A serial particle costs about 20 ns.
     */
    TEST(ParticleParallelBenchTest, DISABLED_PrintsTheFrameCost) {
        ThreadPool pool(TaskManager::kMaxWorkerThreads);
        for (const size_t emitters : {1, 8, 64}) {
            for (const int32_t particles : {64, 256, 512, 1024, 4096}) {
                Scene serialScene(emitters, particles);
                const double serialMicros = MedianMicros(serialScene, kBenchFrames, [&] { serialScene.BuildSerial(); });
                Scene parallelScene(emitters, particles);
                const double parallelMicros = MedianMicros(parallelScene, kBenchFrames, [&] { parallelScene.BuildParallel(pool, kMinChunk); });
                std::printf("ParticleParallel emitters=%zu particles=%d serial_us=%.2f parallel_us=%.2f\n", emitters, particles, serialMicros, parallelMicros);
            }
        }
        for (const size_t minChunk : {64, 128, 256, 512, 1024}) {
            for (const auto& [emitters, particles] : {Pair<size_t, int32_t>{1, 4096}, {8, 1024}}) {
                Scene scene(emitters, particles);
                const double micros = MedianMicros(scene, kBenchFrames, [&] { scene.BuildParallel(pool, minChunk); });
                std::printf("ParticleParallelChunk emitters=%zu particles=%d min_chunk=%zu parallel_us=%.2f\n", emitters, particles, minChunk, micros);
            }
        }
    }

}  // namespace BECore::Tests
