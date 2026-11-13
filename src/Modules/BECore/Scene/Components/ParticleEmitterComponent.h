#pragma once

#include <BECore/Particles/ParticleEffect.h>
#include <BECore/Particles/ParticleSimulation.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }  // namespace InputEvents

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }  // namespace SceneEvents

    /**
     * @brief Plays a `ParticleEffect` at its node: the glue between the effect, the scene and the renderer (`Particles/CLAUDE.md`).
     *
     * Updates by game time (the sum of `SceneUpdateEvent::deltaTime`, so a held frame moves nothing) and draws all its live particles
     * as one `DrawGeometry` batch under the effect's blend mode. Emission is centered on the node's `TransformComponent` rect (`Rect`
     * fills it). The particles keep the place they were born at when the node moves later. The effect is read on every update, so an
     * edit in the inspector takes effect at once without a restart.
     */
    class ParticleEmitterComponent : public IComponent {
        BE_CLASS(ParticleEmitterComponent, ICON = sparkles)
    public:
        ParticleEmitterComponent() = default;
        ~ParticleEmitterComponent() override = default;

        static constexpr int32_t kEffectBurstCount = -1;  ///< the `count` of `Burst` that means the effect's own `burstCount`

        BE_REFLECT_FIELD ParticleEffect _effect;
        BE_REFLECT_FIELD bool _playOnStart = true;  ///< `Play()` when the component is attached; the first update then fires a one-shot effect
        BE_REFLECT_FIELD PoolString _burstAction;   ///< an `InputEvents::ActionEvent` of this name bursts the effect's `burstCount` at the node's center; empty is none

        void OnAttached() override;
        void OnDraw() override;

        /// Restart the schedule (a one-shot effect fires at the next update); the live particles stay and finish
        void Play();

        /// No new particles; the live ones finish
        void Stop();

        /// Stop and remove every live particle
        void Clear();

        /// Spawn @p count particles now around (@p x, @p y) in scene units; -1 is the effect's `burstCount`. @p tint multiplies their color
        void Burst(float x, float y, int32_t count = kEffectBurstCount, Color tint = Color(255, 255, 255, 255));

        [[nodiscard]] bool IsEmitting() const {
            return _simulation.IsEmitting();
        }

        [[nodiscard]] size_t GetLiveCount() const {
            return _simulation.GetLiveCount();
        }

        /// Whether the frame loop has to keep running for this emitter (`ParticleSimulation::NeedsFrames`)
        [[nodiscard]] bool NeedsFrames() const {
            return _simulation.NeedsFrames();
        }

        [[nodiscard]] const ParticleSimulation& GetSimulation() const {
            return _simulation;
        }

        /**
         * @brief Draw the particles with a texture that does not come from the library or a path
         *
         * For a texture made from pixels (`IRenderer::CreateTexture()`). The part of it comes from the sprite (`spriteId`), else the
         * whole texture. A change of `spriteId` or `texturePath` of the effect loads the sprite again and drops this texture.
         */
        void SetTexture(IntrusivePtr<ITexture> texture);

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent& e);
        void OnAction(const InputEvents::ActionEvent& e);
        // The texture is a GPU object, and components outlive the renderer: it goes when the application cleans up
        void OnApplicationCleanUp();

        void ResolveSprite();
        void ReservePools();
        [[nodiscard]] Rect NodeRect() const;

        ParticleSimulation _simulation;
        ResourceHandle<ITexture> _texture;
        Rect _source{};                ///< the part of the texture in pixels; empty is the whole texture
        PoolString _resolvedSpriteId;  ///< the `spriteId` and `texturePath` the texture was loaded for
        PoolString _resolvedTexturePath;
        bool _resolved = false;
        int32_t _reservedParticles = 0;           ///< the pool size reserved so far
        eastl::vector<GeometryVertex> _vertices;  ///< scratch of `OnDraw`, reserved with the pool
        eastl::vector<int> _indices;
    };

}  // namespace BECore
