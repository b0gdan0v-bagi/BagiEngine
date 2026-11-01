#pragma once

#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Scene/IComponent.h>

namespace BECore::Tests {

    /**
     * @brief Records the renderer's view transform as it is while the scene draws.
     *
     * The transform is only set around the scene's draw pass, so a test cannot read it afterwards. Reflected so a
     * SceneConfig-style XML can create it by type name (see SceneViewTest.cpp).
     */
    class ViewProbe : public IComponent {
        BE_CLASS(ViewProbe)
    public:
        ViewProbe() = default;
        ~ViewProbe() override = default;

        ViewTransform seen;
        int drawCount = 0;

        void OnDraw() override;
    };

}  // namespace BECore::Tests
