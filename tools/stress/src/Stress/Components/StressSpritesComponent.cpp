#include "StressSpritesComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/ApplicationEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/StressSpritesComponent.gen.hpp>
#include <Stress/SpriteField.h>
#include <Stress/StressGame.h>

namespace BECore::Stress {

    namespace {

        constexpr float kFullTurn = 360.0f;

        /// The one name of every sprite node: a name is a PoolString, so a million nodes share it
        PoolString SpriteNodeName() {
            return "Sprite"_intern;
        }

    }  // namespace

    void StressSpritesComponent::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &StressSpritesComponent::OnUpdate>(this);
        // Subscribed after the renderer's own handler, so it runs before the renderer destroys its device (RenderProbeComponent does the same)
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &StressSpritesComponent::OnApplicationCleanUp>(this);
    }

    void StressSpritesComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& event) {
        StressGame* game = StressGame::GetActive();
        if (game == nullptr || _cleanedUp || !EnsureAtlases(game->GetSettings().atlases)) {
            return;
        }
        const StressState& state = game->GetState();
        if (state.Version() != _builtVersion) {
            _builtVersion = state.Version();
            Rebuild(state.GetCount(), game->GetSettings());
        }

        for (Sprite& sprite : _sprites) {
            float rotation = sprite.transform->_rotation + sprite.spin * event.deltaTime;
            if (rotation >= kFullTurn) {
                rotation -= kFullTurn;
            } else if (rotation < 0.0f) {
                rotation += kFullTurn;
            }
            sprite.transform->_rotation = rotation;
        }
    }

    bool StressSpritesComponent::EnsureAtlases(uint32_t atlases) {
        if (!_atlases.empty()) {
            return true;
        }
        const IntrusivePtr<IRenderer>& renderer = CoreManager::GetRenderer();
        if (_atlasesFailed || !renderer) {
            return false;
        }
        const uint32_t side = kAtlasCellsPerSide * kAtlasCellSize;
        _atlases.reserve(atlases);
        for (uint32_t atlas = 0; atlas < atlases; ++atlas) {
            const Vector<uint8_t> pixels = MakeAtlasPixels(atlas, kAtlasCellsPerSide, kAtlasCellSize);
            IntrusivePtr<ITexture> texture = renderer->CreateTexture(pixels.data(), side, side);
            if (!texture) {
                LOG_ERROR(Format("StressSpritesComponent: the renderer did not make atlas {}; no sprites are shown", atlas));
                _atlases.clear();
                _atlasesFailed = true;
                return false;
            }
            _atlases.push_back(std::move(texture));
        }
        return true;
    }

    void StressSpritesComponent::ApplyRun(Sprite& sprite, uint32_t atlas, const Rect& srcRect, BlendMode blend) {
        sprite.renderer->SetTexture(_atlases[atlas]);
        sprite.renderer->_srcRect = srcRect;
        sprite.renderer->_blend = blend;
    }

    void StressSpritesComponent::Rebuild(uint32_t count, const StressSettings& settings) {
        const FieldView view{GetScene().GetViewWidth(), GetScene().GetViewHeight()};
        const size_t target = count;

        // Shrink from the tail
        while (_sprites.size() > target) {
            GetNode().RemoveChild(_sprites.back().node.Get());
            _sprites.pop_back();
        }

        // The runs of atlas and blend mode depend on the count: the kept nodes follow them again when it moved
        if (count != _placedCount) {
            for (size_t i = 0; i < _sprites.size(); ++i) {
                const SpritePlacement placement = Place(static_cast<uint32_t>(i), count, settings, view);
                ApplyRun(_sprites[i], placement.atlas, placement.srcRect, placement.blend);
            }
            _placedCount = count;
        }

        // Grow at the tail
        _sprites.reserve(target);
        while (_sprites.size() < target) {
            const SpritePlacement placement = Place(static_cast<uint32_t>(_sprites.size()), count, settings, view);
            Sprite sprite;
            sprite.node = GetNode().AddChild(SpriteNodeName());
            sprite.transform = New<TransformComponent>();
            sprite.transform->_x = placement.x;
            sprite.transform->_y = placement.y;
            sprite.transform->_width = placement.size;
            sprite.transform->_height = placement.size;
            sprite.node->AddComponent(sprite.transform);
            sprite.renderer = New<SpriteRendererComponent>();
            sprite.node->AddComponent(sprite.renderer);
            sprite.spin = placement.spin;
            ApplyRun(sprite, placement.atlas, placement.srcRect, placement.blend);
            _sprites.push_back(std::move(sprite));
        }
    }

    void StressSpritesComponent::OnApplicationCleanUp() {
        if (const IntrusivePtr<IRenderer>& renderer = CoreManager::GetRenderer()) {
            renderer->WaitIdle();
        }
        for (Sprite& sprite : _sprites) {
            sprite.renderer->SetTexture({});
        }
        _atlases.clear();
        _cleanedUp = true;
    }

}  // namespace BECore::Stress
