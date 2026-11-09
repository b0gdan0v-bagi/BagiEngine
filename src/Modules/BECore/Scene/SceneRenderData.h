#pragma once

#include <BECore/Renderer/BlendMode.h>
#include <BECore/Scene/Components/SpriteFit.h>

namespace BECore {

    class IComponent;
    class ITexture;

    /**
     * The render data of a node in the scene's registry: components of the node's entity, written by SceneDrawSystem::Run() and
     * by nothing else. They are a mirror. TransformComponent, QuadRendererComponent and SpriteRendererComponent stay the source
     * of truth (reflection, the XML, the scene editor and game code write their fields), and the system copies the fields at the
     * start of every Draw(), so a change shows in the next draw with no event or setter hook. A node that lost the component
     * loses the registry component in the same walk. The structs are valid after a Draw(); game code does not read them.
     */

    /// Mirror of TransformComponent: the rect of the node in scene units, and the rotation (degrees, clockwise) about the pivot,
    /// which is normalized in that rect (0.5, 0.5 is the center). A rotation of 0 draws the plain, unturned rect.
    struct TransformData {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float rotation = 0.0f;
        float pivotX = 0.5f;
        float pivotY = 0.5f;
    };

    /// Mirror of QuadRendererComponent: the fill color of the node's rect and the blend mode of its draw. @c owner is the component it was copied from.
    struct QuadDrawData {
        Color color{};
        BlendMode blend = BlendMode::Alpha;
        const IComponent* owner = nullptr;
    };

    /**
     * Mirror of SpriteRendererComponent. @c blend is the blend mode of its draw, @c flipX / @c flipY mirror the picture. @c owner is the
     * component it was copied from. @c texture is null when the sprite has none.
     *
     * @c texture is a raw pointer taken from the component's handle in the same Run() that draws. It is safe only because nothing may
     * change the component (SetTexture(), ReloadTexture(), RemoveComponent()) between the sync and the draw: nothing adds or removes
     * nodes or components while Scene::Draw() runs.
     */
    struct SpriteDrawData {
        ITexture* texture = nullptr;
        Rect srcRect{};  ///< empty means the whole texture
        SpriteFit fit = SpriteFit::Stretch;
        BlendMode blend = BlendMode::Alpha;
        bool flipX = false;
        bool flipY = false;
        const IComponent* owner = nullptr;
    };

}  // namespace BECore
