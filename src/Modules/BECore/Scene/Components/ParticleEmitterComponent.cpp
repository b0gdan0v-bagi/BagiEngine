#include "ParticleEmitterComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/ApplicationEvents.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/ParticleEmitterComponent.gen.hpp>

namespace BECore {

    namespace {

        constexpr size_t kVerticesPerQuad = 4;
        constexpr size_t kIndicesPerQuad = 6;
        constexpr float kHalf = 0.5f;

    }  // namespace

    void ParticleEmitterComponent::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ParticleEmitterComponent::OnUpdate>(this);
        SubscribeInScene<InputEvents::ActionEvent, &ParticleEmitterComponent::OnAction>(this);
        // Subscribed after the renderer's own handler, so it runs before the renderer destroys its device
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &ParticleEmitterComponent::OnApplicationCleanUp>(this);

        ResolveSprite();
        ReservePools();
        if (_playOnStart) {
            Play();
        }
    }

    void ParticleEmitterComponent::OnApplicationCleanUp() {
        if (const auto& renderer = CoreManager::GetRenderer()) {
            renderer->WaitIdle();
        }
        _texture.Reset();
    }

    void ParticleEmitterComponent::Play() {
        _simulation.Play();
    }

    void ParticleEmitterComponent::Stop() {
        _simulation.Stop();
    }

    void ParticleEmitterComponent::Clear() {
        _simulation.Clear();
    }

    void ParticleEmitterComponent::Burst(float x, float y, int32_t count, Color tint) {
        ReservePools();
        _simulation.Burst(_effect, Vec2(x, y), count < 0 ? _effect.burstCount : count, tint);
    }

    void ParticleEmitterComponent::SetTexture(IntrusivePtr<ITexture> texture) {
        _texture = ResourceHandle<ITexture>(std::move(texture));
    }

    Rect ParticleEmitterComponent::NodeRect() const {
        if (const auto transform = GetNode().GetComponent<TransformComponent>()) {
            return Rect(transform->_x, transform->_y, transform->_width, transform->_height);
        }
        return Rect();
    }

    void ParticleEmitterComponent::ResolveSprite() {
        _resolved = true;
        _resolvedSpriteId = _effect.spriteId;
        _resolvedTexturePath = _effect.texturePath;
        _texture.Reset();
        _source = Rect();

        PoolString path = _effect.texturePath;
        if (!_effect.spriteId.Empty()) {
            // The sprite wins over the path; an unknown one draws plain squares
            path = PoolString();
            if (const auto* entry = CoreManager::GetTextureLibrary().GetSprite(_effect.spriteId)) {
                path = entry->texturePath;
                _source = entry->srcRect;
            } else {
                LOG_ERROR(Format("ParticleEmitterComponent: sprite '{}' not found in TextureLibrary", _effect.spriteId.ToStringView()).c_str());
            }
        }
        if (path.Empty()) {
            return;
        }
        _texture = CoreManager::GetResourceManager().Load<ITexture>(path.ToStringView());
        if (!_texture) {
            LOG_ERROR(Format("ParticleEmitterComponent: failed to load texture '{}'", path.ToStringView()).c_str());
        }
    }

    void ParticleEmitterComponent::ReservePools() {
        const int32_t wanted = std::clamp(_effect.maxParticles, ParticleLimits::kMinParticles, ParticleLimits::kMaxParticles);
        if (wanted <= _reservedParticles) {
            return;
        }
        _reservedParticles = wanted;
        _simulation.Reserve(wanted);
        ParticleSimulation::ReserveGeometry(wanted, _vertices, _indices);
    }

    void ParticleEmitterComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& e) {
        if (_effect.spriteId != _resolvedSpriteId || _effect.texturePath != _resolvedTexturePath) {
            ResolveSprite();
        }
        ReservePools();
        _simulation.Update(_effect, NodeRect(), e.deltaTime);
    }

    void ParticleEmitterComponent::OnAction(const InputEvents::ActionEvent& e) {
        if (_burstAction.Empty() || e.action != _burstAction) {
            return;
        }
        const Rect rect = NodeRect();
        Burst(rect.x + rect.w * kHalf, rect.y + rect.h * kHalf);
    }

    void ParticleEmitterComponent::OnDraw() {
        if (_simulation.GetLiveCount() == 0) {
            return;
        }
        const auto& renderer = CoreManager::GetRenderer();
        if (!renderer) {
            return;
        }
        ITexture* texture = _texture.Get();
        const Vec2 textureSize = texture ? Vec2(texture->GetWidth(), texture->GetHeight()) : Vec2();
        const size_t quads = _simulation.BuildGeometry(_effect, _source, textureSize, _vertices, _indices);
        if (quads == 0) {
            return;
        }
        const ScopedBlendMode blend(*renderer, _effect.blend);
        renderer->DrawGeometry(texture, {_vertices.data(), quads * kVerticesPerQuad}, {_indices.data(), quads * kIndicesPerQuad}, 0.0f, 0.0f);
    }

}  // namespace BECore
