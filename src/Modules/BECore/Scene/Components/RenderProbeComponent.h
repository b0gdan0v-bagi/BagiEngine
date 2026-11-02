#pragma once

#include <BECore/Renderer/ITexture.h>
#include <BECore/Scene/IComponent.h>

namespace BECore {

    /**
     * @brief Debug: draws a row of cells that show the renderer primitives, for a look at both backends.
     *
     * From the node's transform origin, left to right, on a checkerboard: a texture made from pixels, the same texture
     * tinted red, the same texture at half alpha, two overlapping translucent rects, and a clipped cell (a red square
     * with a nested clip: a green square with the texture, both cut to their clip), a quad of DrawGeometry() with a
     * color per corner (the last one faint), and a textured DrawGeometry() quad mirrored left to right and drawn
     * through the translation. Put it on a node with a TransformComponent: `<component type="RenderProbeComponent"/>`.
     */
    class RenderProbeComponent : public IComponent {
        BE_CLASS(RenderProbeComponent)
    public:
        RenderProbeComponent() = default;
        ~RenderProbeComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        // The texture is a GPU object, and components outlive the renderer: it goes when the application cleans up
        void OnApplicationCleanUp();

        IntrusivePtr<ITexture> _gradient;
    };

}  // namespace BECore
