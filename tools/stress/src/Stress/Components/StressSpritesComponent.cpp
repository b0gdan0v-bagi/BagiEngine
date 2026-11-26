#include "StressSpritesComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/ApplicationEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/StressSpritesComponent.gen.hpp>
#include <Stress/Components/StressLifeComponent.h>
#include <Stress/StressGame.h>

namespace BECore::Stress {

    namespace {

        constexpr float kFullTurn = 360.0f;

        /// The names of the nodes of the field. A name is a PoolString, so a million nodes share one
        struct NodeNames {
            static constexpr auto sprite = "Sprite"_intern;
            static constexpr auto life = "Life"_intern;  // the child of a sprite that can despawn
        };

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
        if (game->GetSettings().load.churn > 0) {
            Churn(state.GetCount(), game->GetSettings());
        }

        // After the rebuild and the churn, so that a new sprite is shifted before it is first drawn. A world of one view has no camera and writes no position
        const uint32_t world = game->GetSettings().load.world;
        const bool cameraMoves = world > 1;
        Vec2 camera;
        if (cameraMoves) {
            _cameraSeconds += event.deltaTime;
            camera = CameraOffset(_cameraSeconds, world, FieldView{GetScene().GetViewWidth(), GetScene().GetViewHeight()});
        }

        for (Sprite& sprite : _sprites) {
            if (cameraMoves) {
                sprite.transform->_x = sprite.position.x - camera.x;
                sprite.transform->_y = sprite.position.y - camera.y;
            }
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
                LOG_ERROR("StressSpritesComponent: the renderer did not make atlas {}; no sprites are shown", atlas);
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
        const size_t target = count;

        // Shrink from the tail
        while (_sprites.size() > target) {
            GetNode().RemoveChild(_sprites.back().node.Get());
            _sprites.pop_back();
        }

        // The runs of atlas and blend mode depend on the count: the kept nodes follow them again when it moved
        if (count != _placedCount) {
            for (size_t i = 0; i < _sprites.size(); ++i) {
                const SpritePlacement placement = PlacementOf(i, count, settings);
                ApplyRun(_sprites[i], placement.atlas, placement.srcRect, placement.blend);
            }
            _placedCount = count;
        }

        Grow(count, settings);
    }

    void StressSpritesComponent::Churn(uint32_t count, const StressSettings& settings) {
        const size_t replaced = std::min<size_t>(settings.load.churn, _sprites.size());
        for (size_t i = 0; i < replaced; ++i) {
            GetNode().RemoveChild(_sprites[i].node.Get());
        }
        // Before the new ones are made: the last references go here, so the pools hand the same blocks out again
        _sprites.erase(_sprites.begin(), _sprites.begin() + static_cast<ptrdiff_t>(replaced));
        _firstSerial += static_cast<uint32_t>(replaced);
        Grow(count, settings);
    }

    SpritePlacement StressSpritesComponent::PlacementOf(size_t index, uint32_t count, const StressSettings& settings) const {
        const float world = static_cast<float>(settings.load.world);
        const FieldView field{GetScene().GetViewWidth() * world, GetScene().GetViewHeight() * world};
        return Place(_firstSerial + static_cast<uint32_t>(index), count, settings, field);
    }

    void StressSpritesComponent::Grow(uint32_t count, const StressSettings& settings) {
        _sprites.reserve(count);
        while (_sprites.size() < count) {
            const SpritePlacement placement = PlacementOf(_sprites.size(), count, settings);
            Sprite sprite;
            sprite.node = GetNode().AddChild(NodeNames::sprite);
            sprite.transform = New<TransformComponent>();
            sprite.transform->_x = placement.x;
            sprite.transform->_y = placement.y;
            sprite.transform->_width = placement.size;
            sprite.transform->_height = placement.size;
            sprite.node->AddComponent(sprite.transform);
            sprite.renderer = New<SpriteRendererComponent>();
            sprite.node->AddComponent(sprite.renderer);
            // On a child: a third component of the sprite's node would not fit its inline list and cost a heap block per spawn
            if (settings.load.churn > 0) {
                sprite.node->AddChild(NodeNames::life)->AddComponent(New<StressLifeComponent>());
            }
            sprite.position = Vec2(placement.x, placement.y);
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
