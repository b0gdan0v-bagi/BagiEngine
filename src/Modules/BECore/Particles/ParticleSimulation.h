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
        float frameSeed = 0.0f;       ///< in [0, 1): the flipbook's random start is `floor(frameSeed * frameCount)`, so a change of `frames` keeps it valid
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
     * - **Looping emission is a schedule.** Particle `n` (from 1) is born at `n / rate` of the time since `Play()`; an update spawns
     *   every one that is due, spawn time backdated. A one-shot effect fires `burstCount` at the update that follows `Play()`.
     * - **Deterministic.** The numbers of a particle come from `ParticleRandom` by (seed, serial), so the order and the number of
     *   updates do not change the picture. Emitter time is the sum of the `dt` given to `Update` (game time).
     * - **A fixed pool.** At most `maxParticles` live; a particle that does not fit is dropped and counted. Dead ones are removed by a
     *   stable compaction, so spawn order is draw order.
     *
     * The effect is sanitized (`ParticleEffect::Sanitize`) on every call that reads it. Serials, pool growth and what is not there:
     * `Particles/CLAUDE.md`, "The simulation".
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

        [[nodiscard]] size_t GetCapacity() const {
            return _particles.capacity();
        }

        /// The live particles in spawn order
        [[nodiscard]] const Vector<Particle>& GetParticles() const {
            return _particles;
        }

        /// Where @p particle is now (world space); the gravity is read from @p effect
        [[nodiscard]] Vec2 PositionOf(const Particle& particle, const ParticleEffect& effect) const;

        /// The curves of @p effect at @p progress (0 at birth, 1 at the end of the life; clamped), also with `Ease` exact at the ends
        [[nodiscard]] static ParticleLook SampleLook(const ParticleEffect& effect, float progress);

        /**
         * @brief The flipbook frame, in `0..count-1`, of a particle that is @p age seconds old at @p progress (`age / lifetime`) of its life.
         *
         * `count` is `frames`, or `columns * rows` when `frames` is 0 (never more than the grid). `OverLife` shows every frame once over
         * the life, `Loop` advances `fps` frames per second and wraps; `randomStart` shifts either by `floor(frameSeed * count)`.
         * A function of the age only, so the frame is the same at any frame rate. Safe on an unsanitized flipbook.
         */
        [[nodiscard]] static int32_t FrameOf(const ParticleFlipbook& flipbook, float progress, float age, float frameSeed);

        /// Reserve @p vertices (four per particle) and @p indices (six) for @p maxParticles and write the index pattern `0 1 2 0 2 3 ...` once
        static void ReserveGeometry(int32_t maxParticles, Vector<GeometryVertex>& vertices, Vector<int>& indices);

        /// What `BuildQuads` reads besides the particles: everything `PrepareGeometry` settles once for one frame
        struct GeometryFrame {
            ParticleEffect effect;  ///< sanitized
            Rect part;              ///< the part of the texture in pixels; the whole texture when the source is empty
            Vec2 textureSize;
            bool textured = false;
            double time = 0.0;  ///< emitter time of the frame
        };

        /**
         * @brief The serial half of `BuildGeometry`: sanitize @p effect, size @p vertices to four per live particle and make sure
         * @p indices holds the pattern for them.
         *
         * @param source the part of the texture in pixels; empty means the whole texture. A flipbook splits it into its grid
         * @param textureSize the texture's size in pixels; zero in either component draws untextured (the UVs are 0)
         * @return the frame for `BuildQuads`, valid until the next `Update`, `Burst` or `Clear`. Allocates only when the vectors are
         *         smaller than `ReserveGeometry` makes them
         */
        [[nodiscard]] GeometryFrame PrepareGeometry(const ParticleEffect& effect, const Rect& source, Vec2 textureSize, Vector<GeometryVertex>& vertices, Vector<int>& indices) const;

        /**
         * @brief The range half of `BuildGeometry`: write the vertices of the live particles `[begin, end)` to `vertices[4 * begin, 4 * end)`.
         *
         * Touches nothing else (no member, no allocation, no lock), so ranges may run on several threads at once while nothing calls
         * a non-const method; the bytes do not depend on the split. @p vertices is the data of the vector `PrepareGeometry` sized.
         */
        void BuildQuads(const GeometryFrame& frame, size_t begin, size_t end, GeometryVertex* vertices) const;

        /**
         * @brief Fill @p vertices with one rotated square per live particle (four vertices each, corners in the order top left,
         * top right, bottom right, bottom left, straight-alpha color) and make sure @p indices holds the pattern for them:
         * `PrepareGeometry` and one `BuildQuads` over all of them.
         *
         * @param source the part of the texture in pixels; empty means the whole texture. A flipbook splits it into its grid and
         *        each particle gets the UVs of its frame's cell
         * @param textureSize the texture's size in pixels; zero in either component draws untextured (the UVs are 0)
         * @return the number of quads: draw `indices` up to `6 * quads` with `vertices`. Allocates only when the vectors are smaller
         *         than `ReserveGeometry` makes them
         */
        size_t BuildGeometry(const ParticleEffect& effect, const Rect& source, Vec2 textureSize, Vector<GeometryVertex>& vertices, Vector<int>& indices) const;

    private:
        void Spawn(const ParticleEffect& effect, const Rect& area, uint64_t serial, double spawnTime, Color tint);
        void SpawnBurst(const ParticleEffect& effect, const Rect& area, int32_t count, double spawnTime, Color tint);
        void SpawnLoopDue(const ParticleEffect& effect, const Rect& area);
        void RemoveDead();

        Vector<Particle> _particles;
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
