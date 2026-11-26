#include "StressSpritesComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Components/ParticleEmitterComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Text/TextComponent.h>
#include <Events/ApplicationEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/StressSpritesComponent.gen.hpp>
#include <Stress/Components/StressLifeComponent.h>
#include <Stress/Components/StressShapeComponent.h>
#include <Stress/StressGame.h>

namespace BECore::Stress {

    namespace {

        constexpr float kFullTurn = 360.0f;

        // A text of a mixed field: the word at this size fits the box of the smallest sprite of the spread, so a draw pushes no clip
        constexpr StringView kMixWord = "Go";
        constexpr float kMixFontSize = 10.0f;

        // An emitter of a mixed field: it emits its pool once per lifetime, so the pool stays about full
        constexpr int32_t kMixParticles = 16;
        constexpr float kMixParticleSeconds = 1.0f;
        constexpr float kMixParticleMinSpeed = 20.0f;
        constexpr float kMixParticleMaxSpeed = 60.0f;
        constexpr float kMixParticleSize = 8.0f;
        constexpr float kMixParticleEndSize = 2.0f;

        /// The names of the nodes of the field. A name is a PoolString, so a million nodes share one
        struct NodeNames {
            static constexpr auto sprite = "Sprite"_intern;
            static constexpr auto text = "MixText"_intern;
            static constexpr auto shape = "MixShape"_intern;
            static constexpr auto particles = "MixParticles"_intern;
            static constexpr auto life = "Life"_intern;  // the child of an object that can despawn
        };

        PoolString NameOf(MixKind kind) {
            switch (kind) {
                case MixKind::Sprite:
                    break;
                case MixKind::Text:
                    return NodeNames::text;
                case MixKind::Shape:
                    return NodeNames::shape;
                case MixKind::Particles:
                    return NodeNames::particles;
            }
            return NodeNames::sprite;
        }

        void AddText(SceneNode& node) {
            // Before the component is attached: that is when it loads its font
            IntrusivePtr<TextComponent> text = New<TextComponent>();
            text->_text.assign(kMixWord.data(), kMixWord.size());
            text->_fontSize = kMixFontSize;
            text->_wrap = false;
            node.AddComponent(std::move(text));
        }

        void AddEmitter(SceneNode& node, uint32_t serial, IntrusivePtr<ITexture> atlas) {
            IntrusivePtr<ParticleEmitterComponent> emitter = New<ParticleEmitterComponent>();
            ParticleEffect& effect = emitter->_effect;
            effect.looping = true;
            effect.maxParticles = kMixParticles;
            effect.rate = static_cast<float>(kMixParticles) / kMixParticleSeconds;
            effect.lifetimeMin = kMixParticleSeconds;
            effect.lifetimeMax = kMixParticleSeconds;
            effect.speedMin = kMixParticleMinSpeed;
            effect.speedMax = kMixParticleMaxSpeed;
            effect.size = ParticleCurve(kMixParticleSize, kMixParticleEndSize);
            effect.seed = serial;
            effect.frameClass = ParticleFrameClass::DecorativeLoop;
            node.AddComponent(emitter);
            // After the attach, which loads the texture the effect names (none) and would drop this one
            emitter->SetTexture(std::move(atlas));
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

        for (FieldObject& object : _objects) {
            if (cameraMoves) {
                object.transform->_x = object.position.x - camera.x;
                object.transform->_y = object.position.y - camera.y;
            }
            float rotation = object.transform->_rotation + object.spin * event.deltaTime;
            if (rotation >= kFullTurn) {
                rotation -= kFullTurn;
            } else if (rotation < 0.0f) {
                rotation += kFullTurn;
            }
            object.transform->_rotation = rotation;
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

    void StressSpritesComponent::ApplyRun(SpriteRendererComponent& sprite, uint32_t atlas, const Rect& srcRect, BlendMode blend) {
        sprite.SetTexture(_atlases[atlas]);
        sprite._srcRect = srcRect;
        sprite._blend = blend;
    }

    void StressSpritesComponent::Rebuild(uint32_t count, const StressSettings& settings) {
        const size_t target = count;

        // Shrink from the tail
        while (_objects.size() > target) {
            GetNode().RemoveChild(_objects.back().node.Get());
            _objects.pop_back();
        }

        // The runs of atlas and blend mode depend on the count: the kept sprites follow them again when it moved
        if (count != _placedCount) {
            for (size_t i = 0; i < _objects.size(); ++i) {
                if (_objects[i].sprite) {
                    const SpritePlacement placement = PlacementOf(i, count, settings);
                    ApplyRun(*_objects[i].sprite, placement.atlas, placement.srcRect, placement.blend);
                }
            }
            _placedCount = count;
        }

        Grow(count, settings);
    }

    void StressSpritesComponent::Churn(uint32_t count, const StressSettings& settings) {
        const size_t replaced = std::min<size_t>(settings.load.churn, _objects.size());
        for (size_t i = 0; i < replaced; ++i) {
            GetNode().RemoveChild(_objects[i].node.Get());
        }
        // Before the new ones are made: the last references go here, so the pools hand the same blocks out again
        _objects.erase(_objects.begin(), _objects.begin() + static_cast<ptrdiff_t>(replaced));
        _firstSerial += static_cast<uint32_t>(replaced);
        Grow(count, settings);
    }

    SpritePlacement StressSpritesComponent::PlacementOf(size_t index, uint32_t count, const StressSettings& settings) const {
        const float world = static_cast<float>(settings.load.world);
        const FieldView field{GetScene().GetViewWidth() * world, GetScene().GetViewHeight() * world};
        return Place(_firstSerial + static_cast<uint32_t>(index), count, settings, field);
    }

    void StressSpritesComponent::Grow(uint32_t count, const StressSettings& settings) {
        _objects.reserve(count);
        while (_objects.size() < count) {
            const uint32_t serial = _firstSerial + static_cast<uint32_t>(_objects.size());
            const SpritePlacement placement = PlacementOf(_objects.size(), count, settings);
            const MixKind kind = settings.load.mix ? MixKindOf(serial) : MixKind::Sprite;
            FieldObject object;
            object.node = GetNode().AddChild(NameOf(kind));
            if (settings.load.mix) {
                object.node->SetLayer(MixLayerOf(serial));
            }
            object.transform = New<TransformComponent>();
            object.transform->_x = placement.x;
            object.transform->_y = placement.y;
            object.transform->_width = placement.size;
            object.transform->_height = placement.size;
            object.node->AddComponent(object.transform);
            object.position = Vec2(placement.x, placement.y);
            switch (kind) {
                case MixKind::Sprite:
                    object.sprite = New<SpriteRendererComponent>();
                    object.node->AddComponent(object.sprite);
                    ApplyRun(*object.sprite, placement.atlas, placement.srcRect, placement.blend);
                    object.spin = placement.spin;
                    break;
                case MixKind::Text:
                    AddText(*object.node);
                    break;
                case MixKind::Shape:
                    object.node->AddComponent(New<StressShapeComponent>());
                    break;
                case MixKind::Particles:
                    AddEmitter(*object.node, serial, _atlases[placement.atlas]);
                    break;
            }
            // On a child: a third component of the object's node would not fit its inline list and cost a heap block per spawn
            if (settings.load.churn > 0) {
                object.node->AddChild(NodeNames::life)->AddComponent(New<StressLifeComponent>());
            }
            _objects.push_back(std::move(object));
        }
    }

    void StressSpritesComponent::OnApplicationCleanUp() {
        if (const IntrusivePtr<IRenderer>& renderer = CoreManager::GetRenderer()) {
            renderer->WaitIdle();
        }
        for (FieldObject& object : _objects) {
            if (object.sprite) {
                object.sprite->SetTexture({});
            }
        }
        _atlases.clear();
        _cleanedUp = true;
    }

}  // namespace BECore::Stress
