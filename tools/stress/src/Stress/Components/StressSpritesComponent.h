#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/Math/Vec2.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>
#include <Stress/StressSettings.h>
#include <cstddef>
#include <cstdint>

namespace BECore::SceneEvents {
    struct SceneUpdateEvent;
}

namespace BECore::Stress {

    /**
     * @brief The scene `StressSprites`: `StressState::GetCount()` spinning sprites on child nodes of this node, one `IRenderer` call each.
     *
     * The children are made when `StressState::Version()` moved: a bigger count appends nodes, a smaller one removes them from the tail, the
     * first nodes are kept. The atlases are made once with `IRenderer::CreateTexture` (the renderer may be missing at `OnAttached`, so at the
     * first update) from `SpriteField`'s pixels and shared by the sprites. A steady frame only turns each node by its spin through cached
     * pointers, and in a world larger than the view also moves it against the emulated camera (`SpriteField`'s CameraOffset): no lookup, no
     * allocation. Does nothing while the game is not the active `StressGame`.
     */
    class StressSpritesComponent : public IComponent {
        BE_CLASS(StressSpritesComponent)
    public:
        StressSpritesComponent() = default;
        ~StressSpritesComponent() override = default;
        bool HasOnDraw() const override {
            return false;
        }

        void OnAttached() override;

        /// The sprites made so far (a test counts them)
        size_t GetSpriteCount() const {
            return _sprites.size();
        }

    private:
        struct Sprite {
            IntrusivePtr<SceneNode> node;
            IntrusivePtr<TransformComponent> transform;
            IntrusivePtr<SpriteRendererComponent> renderer;
            Vec2 position;      // in the field; the transform holds it minus the camera's offset
            float spin = 0.0f;  // degrees per second
        };

        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
        void OnApplicationCleanUp();

        /// Makes the atlases once; false while there is no renderer or when the renderer refused them
        bool EnsureAtlases(uint32_t atlases);
        /// Brings the nodes to @p count: appends or removes from the tail, and re-places atlas and blend mode of the kept ones when the runs moved
        void Rebuild(uint32_t count, const StressSettings& settings);
        void ApplyRun(Sprite& sprite, uint32_t atlas, const Rect& srcRect, BlendMode blend);

        Vector<Sprite> _sprites;
        Vector<IntrusivePtr<ITexture>> _atlases;
        uint64_t _builtVersion = 0;   // the StressState version the nodes were last brought to; 0 is never a version
        uint32_t _placedCount = 0;    // the count the atlas and blend mode of the nodes follow (the runs depend on it)
        double _cameraSeconds = 0.0;  // how long the emulated camera has moved: a float sum would drift in a long run
        bool _atlasesFailed = false;
        bool _cleanedUp = false;
    };

}  // namespace BECore::Stress
