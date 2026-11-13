#pragma once

#include <BECore/Math/Vec2.h>
#include <BECore/Particles/ParticleEffect.h>
#include <BECore/Renderer/IRenderer.h>

namespace BECore {

    /// One live particle: the values drawn at its birth. Where it is and how it looks is a function of its age (`ParticleSimulation`)
    struct Particle {
        double spawnTime = 0.0;  ///< emitter time of its birth; for a looping emitter backdated to the scheduled moment
        float originX = 0.0f;
        float originY = 0.0f;
        float velocityX = 0.0f;  ///< scene units per second
        float velocityY = 0.0f;
        float lifetime = 1.0f;        ///< seconds
        float sizeScale = 1.0f;       ///< the size jitter, a factor on the size curve
        float rotationOffset = 0.0f;  ///< degrees added to the rotation curve
        Color tint{255, 255, 255, 255};
    };

    /// What the curves of an effect give at one point of a particle's life
    struct ParticleLook {
        float size = 0.0f;      ///< a side of the square, scene units, not negative
        float rotation = 0.0f;  ///< degrees, clockwise
        Color color;
    };

    /**
     * @brief Runs a `ParticleEffect`: the pool, the spawn schedule and the maths. Plain C++, no scene, no renderer, no `CoreManager`.
     *
     * - **A position is a function of time.** A particle stores its spawn time and what was drawn at birth; at emitter time `t`,
     *   `age = t - spawnTime` and the position is `origin + velocity * age + gravity * age^2 / 2`. Size, rotation and color are the
     *   curves of `age / lifetime`. Nothing is integrated, so a lower or an uneven frame rate gives the same picture at the same time.
     * - **Looping emission is a schedule.** Particle number `n` (from 1) of a looping effect is born at `n / rate` of the time since
     *   `Play()`; an update to time `t` spawns every one that is due, with the spawn time backdated, so its age is right after a long
     *   frame. A one-shot effect fires `burstCount` at `Play()` (at the update that follows, when the effect and the area are known)
     *   and stops emitting.
     * - **Deterministic.** The numbers of a particle come from `ParticleRandom` by (seed, serial). A looping particle's serial is its
     *   number, a burst particle's serial counts the burst particles since `Play()` (with a high bit), so the order and the number of updates
     *   do not change the picture. Emitter time is the sum of the `dt` given to `Update` (game time).
     * - **A fixed pool.** At most `maxParticles` live; a new particle that does not fit is dropped and counted. The pool is reserved
     *   once (`Reserve`, or at the first update) and again only when the field grows. Dead particles are removed by a stable
     *   compaction (spawn order is draw order), in an update where one died.
     * - **World space.** A particle keeps the place it was born at.
     *
     * The effect is sanitized (`ParticleEffect::Sanitize`) on every call that reads it. No drag, no collisions, no sub-emitters.
     */
    class ParticleSimulation {
    public:
        /// Reserve room for @p maxParticles (clamped like the effect's field). Allocates only when the pool is smaller
        void Reserve(int32_t maxParticles);

        /**
         * @brief Advance emitter time by @p dt seconds and run the schedule.
         *
         * @p area is the emitter's rect in scene units: `Point` and `Circle` use its center, `Rect` fills it. A @p dt that is not
         * finite or is negative advances nothing (the rest of the update still runs). No allocation once the pool is reserved.
         */
        void Update(const ParticleEffect& effect, const Rect& area, float dt);

        /// Spawn @p count particles now (emitter time) around @p center, whatever the effect's `looping`. @p tint multiplies the color of these particles only
        void Burst(const ParticleEffect& effect, Vec2 center, int32_t count, Color tint = Color(255, 255, 255, 255));

        /// Restart the schedule from the current emitter time. The live particles stay and finish
        void Play();

        /// No new particles (also none that `Play` has not fired yet); the live ones finish
        void Stop();

        /// Stop, remove every live particle and reset the dropped count. Emitter time goes on
        void Clear();

        /// True from `Play` until a one-shot effect has fired, or for as long as a looping one plays
        [[nodiscard]] bool IsEmitting() const {
            return _emitting;
        }

        [[nodiscard]] size_t GetLiveCount() const {
            return _particles.size();
        }

        /// Particles that did not fit in the pool since the last `Clear`
        [[nodiscard]] uint64_t GetDroppedCount() const {
            return _dropped;
        }

        /// Whether the frame loop has to keep running for this emitter: it is emitting or a particle is alive
        [[nodiscard]] bool NeedsFrames() const {
            return _emitting || !_particles.empty();
        }

        /// The sum of the `dt` given to `Update`, seconds
        [[nodiscard]] double GetTime() const {
            return _time;
        }

        /// The room the pool has reserved, in particles
        [[nodiscard]] size_t GetCapacity() const {
            return _particles.capacity();
        }

        /// The live particles in spawn order
        [[nodiscard]] const eastl::vector<Particle>& GetParticles() const {
            return _particles;
        }

        /// Where @p particle is now (world space); the gravity is read from @p effect
        [[nodiscard]] Vec2 PositionOf(const Particle& particle, const ParticleEffect& effect) const;

        /// The curves of @p effect at @p progress (0 at birth, 1 at the end of the life; clamped), also with `Ease` exact at the ends
        [[nodiscard]] static ParticleLook SampleLook(const ParticleEffect& effect, float progress);

        /// Reserve @p vertices (four per particle) and @p indices (six) for @p maxParticles and write the index pattern `0 1 2 0 2 3 ...` once
        static void ReserveGeometry(int32_t maxParticles, eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices);

        /**
         * @brief Fill @p vertices with one rotated square per live particle (four vertices each, corners in the order top left,
         * top right, bottom right, bottom left, straight-alpha color) and make sure @p indices holds the pattern for them.
         *
         * @param source the part of the texture in pixels; empty means the whole texture
         * @param textureSize the texture's size in pixels; zero in either component draws untextured (the UVs are 0)
         * @return the number of quads: draw `indices` up to `6 * quads` with `vertices`. Allocates only when the vectors are smaller
         *         than `ReserveGeometry` makes them
         */
        size_t BuildGeometry(const ParticleEffect& effect, const Rect& source, Vec2 textureSize, eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices) const;

    private:
        void Spawn(const ParticleEffect& effect, const Rect& area, uint64_t serial, double spawnTime, Color tint);
        void SpawnBurst(const ParticleEffect& effect, const Rect& area, int32_t count, double spawnTime, Color tint);
        void SpawnLoopDue(const ParticleEffect& effect, const Rect& area);
        void RemoveDead();

        eastl::vector<Particle> _particles;
        double _time = 0.0;
        double _playStart = 0.0;                                      ///< emitter time of the last `Play`
        double _nextDeath = std::numeric_limits<double>::infinity();  ///< the earliest death of a live particle: no compaction before it
        uint64_t _loopSpawned = 0;                                    ///< looping particles of this play that are due and handled (spawned, dead on arrival or dropped)
        uint64_t _burstSerial = 0;                                    ///< burst particles since the last `Play`
        uint64_t _dropped = 0;
        bool _emitting = false;
        bool _playPending = false;  ///< `Play` was called and the update that fires or starts it has not run
    };

}  // namespace BECore
