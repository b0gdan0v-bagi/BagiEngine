#include "ParticleSimulation.h"

#include <BECore/Animation/Easing.h>
#include <BECore/Animation/Lerp.h>
#include <BECore/Particles/ParticleRandom.h>
#include <cmath>
#include <numbers>

namespace BECore {

    namespace {

        constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0f;
        constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
        constexpr float kHalf = 0.5f;
        constexpr int kQuadVertices = 4;
        constexpr int kQuadIndices = 6;
        constexpr uint64_t kBurstSerialBit = 1ull << 63;  ///< burst particles draw from serials a looping particle number never reaches
        constexpr double kMaxScheduleCount = 1e15;        ///< the largest number of looping particles one play counts: far past anything a double of seconds reaches
        constexpr Color kWhite{255, 255, 255, 255};

        float Unit(const ParticleEffect& effect, uint64_t serial, ParticleStream stream) {
            return ParticleRandom::Unit(effect.seed, serial, stream);
        }

        /// A value drawn in -1..1
        float Signed(const ParticleEffect& effect, uint64_t serial, ParticleStream stream) {
            return 2.0f * Unit(effect, serial, stream) - 1.0f;
        }

        float FiniteOrZero(float value) {
            return std::isfinite(value) ? value : 0.0f;
        }

        Rect SanitizeArea(const Rect& area) {
            return {FiniteOrZero(area.x), FiniteOrZero(area.y), FiniteOrZero(area.w), FiniteOrZero(area.h)};
        }

        unsigned char MultiplyChannel(unsigned char a, unsigned char b) {
            constexpr int kMax = 255;
            return static_cast<unsigned char>((a * b + kMax / 2) / kMax);
        }

        Color Multiply(Color a, Color b) {
            return {MultiplyChannel(a.r, b.r), MultiplyChannel(a.g, b.g), MultiplyChannel(a.b, b.b), MultiplyChannel(a.a, b.a)};
        }

        Vec2 PositionAt(const Particle& particle, double time, float gravityX, float gravityY) {
            const float age = static_cast<float>(time - particle.spawnTime);
            const float halfAgeSquared = kHalf * age * age;
            return {particle.originX + particle.velocityX * age + gravityX * halfAgeSquared, particle.originY + particle.velocityY * age + gravityY * halfAgeSquared};
        }

        void WriteIndexPattern(eastl::vector<int>& indices, size_t quads) {
            size_t quad = indices.size() / kQuadIndices;
            indices.resize(quads * kQuadIndices);
            for (; quad < quads; ++quad) {
                const int base = static_cast<int>(quad) * kQuadVertices;
                int* out = indices.data() + quad * kQuadIndices;
                out[0] = base;
                out[1] = base + 1;
                out[2] = base + 2;
                out[3] = base;
                out[4] = base + 2;
                out[5] = base + 3;
            }
        }

    }  // namespace

    void ParticleSimulation::Reserve(int32_t maxParticles) {
        const auto wanted = static_cast<size_t>(std::clamp(maxParticles, ParticleLimits::kMinParticles, ParticleLimits::kMaxParticles));
        if (_particles.capacity() < wanted) {
            _particles.reserve(wanted);
        }
    }

    void ParticleSimulation::Update(const ParticleEffect& effect, const Rect& area, float dt) {
        const ParticleEffect clean = effect.Sanitized();
        const Rect safeArea = SanitizeArea(area);
        Reserve(clean.maxParticles);

        if (std::isfinite(dt) && dt > 0.0f) {
            _time += static_cast<double>(dt);
        }
        if (_time >= _nextDeath) {
            RemoveDead();
        }

        if (_playPending) {
            _playPending = false;
            if (!clean.looping) {
                SpawnBurst(clean, safeArea, clean.burstCount, _playStart, kWhite);
                _emitting = false;
            }
        }
        if (_emitting && clean.looping) {
            SpawnLoopDue(clean, safeArea);
        }
    }

    void ParticleSimulation::Burst(const ParticleEffect& effect, Vec2 center, int32_t count, Color tint) {
        const ParticleEffect clean = effect.Sanitized();
        Reserve(clean.maxParticles);
        const Rect point{FiniteOrZero(center.x), FiniteOrZero(center.y), 0.0f, 0.0f};
        SpawnBurst(clean, point, count, _time, tint);
    }

    void ParticleSimulation::Play() {
        _playStart = _time;
        _loopSpawned = 0;
        _burstSerial = 0;
        _emitting = true;
        _playPending = true;
    }

    void ParticleSimulation::Stop() {
        _emitting = false;
        _playPending = false;
    }

    void ParticleSimulation::Clear() {
        Stop();
        _particles.clear();
        _nextDeath = std::numeric_limits<double>::infinity();
        _dropped = 0;
    }

    Vec2 ParticleSimulation::PositionOf(const Particle& particle, const ParticleEffect& effect) const {
        const ParticleEffect clean = effect.Sanitized();
        return PositionAt(particle, _time, clean.gravityX, clean.gravityY);
    }

    ParticleLook ParticleSimulation::SampleLook(const ParticleEffect& effect, float progress) {
        ParticleLook look;
        look.size = std::max(0.0f, Lerp(effect.size.from, effect.size.to, Ease(effect.size.ease, progress)));
        look.rotation = Lerp(effect.rotation.from, effect.rotation.to, Ease(effect.rotation.ease, progress));
        look.color = Lerp(effect.color.from, effect.color.to, Ease(effect.color.ease, progress));
        return look;
    }

    void ParticleSimulation::ReserveGeometry(int32_t maxParticles, eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices) {
        const auto quads = static_cast<size_t>(std::clamp(maxParticles, ParticleLimits::kMinParticles, ParticleLimits::kMaxParticles));
        vertices.reserve(quads * kQuadVertices);
        indices.reserve(quads * kQuadIndices);
        WriteIndexPattern(indices, quads);
    }

    size_t ParticleSimulation::BuildGeometry(const ParticleEffect& effect, const Rect& source, Vec2 textureSize, eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices) const {
        const ParticleEffect clean = effect.Sanitized();
        const size_t quads = _particles.size();
        vertices.resize(quads * kQuadVertices);
        if (indices.size() < quads * kQuadIndices) {
            WriteIndexPattern(indices, quads);
        }

        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
        if (textureSize.x > 0.0f && textureSize.y > 0.0f) {
            const Rect part = source.IsEmpty() ? Rect(0.0f, 0.0f, textureSize.x, textureSize.y) : source;
            u0 = part.x / textureSize.x;
            v0 = part.y / textureSize.y;
            u1 = (part.x + part.w) / textureSize.x;
            v1 = (part.y + part.h) / textureSize.y;
        }

        for (size_t i = 0; i < quads; ++i) {
            const Particle& particle = _particles[i];
            const float age = static_cast<float>(_time - particle.spawnTime);
            const ParticleLook look = SampleLook(clean, age / particle.lifetime);
            const Vec2 center = PositionAt(particle, _time, clean.gravityX, clean.gravityY);
            const float half = kHalf * look.size * particle.sizeScale;
            const float angle = (look.rotation + particle.rotationOffset) * kDegreesToRadians;
            const float cosA = std::cos(angle);
            const float sinA = std::sin(angle);
            const Color color = Multiply(look.color, particle.tint);

            // The corners of the unrotated square, clockwise from the top left, turned about the center
            const float cornerX[kQuadVertices] = {-half, half, half, -half};
            const float cornerY[kQuadVertices] = {-half, -half, half, half};
            const float cornerU[kQuadVertices] = {u0, u1, u1, u0};
            const float cornerV[kQuadVertices] = {v0, v0, v1, v1};
            GeometryVertex* out = vertices.data() + i * kQuadVertices;
            for (int corner = 0; corner < kQuadVertices; ++corner) {
                out[corner].x = center.x + cornerX[corner] * cosA - cornerY[corner] * sinA;
                out[corner].y = center.y + cornerX[corner] * sinA + cornerY[corner] * cosA;
                out[corner].color = color;
                out[corner].u = cornerU[corner];
                out[corner].v = cornerV[corner];
            }
        }
        return quads;
    }

    void ParticleSimulation::Spawn(const ParticleEffect& effect, const Rect& area, uint64_t serial, double spawnTime, Color tint) {
        Particle particle;
        particle.spawnTime = spawnTime;
        particle.lifetime = Lerp(effect.lifetimeMin, effect.lifetimeMax, Unit(effect, serial, ParticleStream::Lifetime));
        const double death = spawnTime + static_cast<double>(particle.lifetime);
        if (death <= _time) {
            return;  // it would be dead by now (a long frame): never visible
        }
        if (_particles.size() >= static_cast<size_t>(effect.maxParticles)) {
            ++_dropped;
            return;
        }

        const float speed = Lerp(effect.speedMin, effect.speedMax, Unit(effect, serial, ParticleStream::Speed));
        const float angle = (effect.direction + (Unit(effect, serial, ParticleStream::Angle) - kHalf) * effect.spread) * kDegreesToRadians;
        particle.velocityX = speed * std::cos(angle);
        particle.velocityY = speed * std::sin(angle);

        const float centerX = area.x + kHalf * area.w;
        const float centerY = area.y + kHalf * area.h;
        const float shapeA = Unit(effect, serial, ParticleStream::ShapeA);
        const float shapeB = Unit(effect, serial, ParticleStream::ShapeB);
        switch (effect.shape) {
            case ParticleShape::Rect:
                particle.originX = area.x + shapeA * area.w;
                particle.originY = area.y + shapeB * area.h;
                break;
            case ParticleShape::Circle: {
                const float radius = effect.shapeRadius * std::sqrt(shapeA);  // uniform over the disc
                particle.originX = centerX + radius * std::cos(kTwoPi * shapeB);
                particle.originY = centerY + radius * std::sin(kTwoPi * shapeB);
                break;
            }
            case ParticleShape::Point:
            default:
                particle.originX = centerX;
                particle.originY = centerY;
                break;
        }

        particle.sizeScale = 1.0f + effect.sizeJitter * Signed(effect, serial, ParticleStream::SizeJitter);
        particle.rotationOffset = effect.rotationJitter * Signed(effect, serial, ParticleStream::RotationJitter);
        particle.tint = tint;

        _particles.push_back(particle);
        _nextDeath = std::min(_nextDeath, death);
    }

    void ParticleSimulation::SpawnBurst(const ParticleEffect& effect, const Rect& area, int32_t count, double spawnTime, Color tint) {
        if (count <= 0) {
            return;
        }
        const size_t limit = static_cast<size_t>(effect.maxParticles);
        const size_t room = limit > _particles.size() ? limit - _particles.size() : 0;
        const auto wanted = static_cast<size_t>(count);
        const size_t attempts = std::min(wanted, room);
        for (size_t i = 0; i < attempts; ++i) {
            Spawn(effect, area, kBurstSerialBit | _burstSerial++, spawnTime, tint);
        }
        _dropped += wanted - attempts;  // counted without a loop, so a hostile count costs nothing
    }

    void ParticleSimulation::SpawnLoopDue(const ParticleEffect& effect, const Rect& area) {
        if (effect.rate <= 0.0f) {
            return;
        }
        const double rate = static_cast<double>(effect.rate);
        const double elapsed = _time - _playStart;
        const auto due = static_cast<uint64_t>(std::clamp(std::floor(elapsed * rate), 0.0, kMaxScheduleCount));
        if (due <= _loopSpawned) {
            return;
        }

        // Numbers whose scheduled moment is further back than the longest life are dead already: skip them in one step
        const double oldestAlive = std::floor((elapsed - static_cast<double>(effect.lifetimeMax)) * rate);
        uint64_t next = _loopSpawned;
        if (oldestAlive > static_cast<double>(next)) {
            next = static_cast<uint64_t>(std::min(oldestAlive, kMaxScheduleCount));
        }
        const size_t limit = static_cast<size_t>(effect.maxParticles);
        for (; next < due; ++next) {
            if (_particles.size() >= limit) {
                _dropped += due - next;  // a full pool drops the rest of this update, counted in one step
                break;
            }
            Spawn(effect, area, next, _playStart + static_cast<double>(next + 1) / rate, kWhite);
        }
        _loopSpawned = due;
    }

    void ParticleSimulation::RemoveDead() {
        const double now = _time;
        const auto isDead = [now](const Particle& particle) { return now >= particle.spawnTime + static_cast<double>(particle.lifetime); };
        _particles.erase(eastl::remove_if(_particles.begin(), _particles.end(), isDead), _particles.end());
        _nextDeath = std::numeric_limits<double>::infinity();
        for (const Particle& particle : _particles) {
            _nextDeath = std::min(_nextDeath, particle.spawnTime + static_cast<double>(particle.lifetime));
        }
    }

}  // namespace BECore
