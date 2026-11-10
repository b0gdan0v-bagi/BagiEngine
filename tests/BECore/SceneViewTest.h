#pragma once

#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/SceneView.h>

namespace BECore::Tests {

    /**
     * @brief Records the renderer's view transform as it is while the scene draws.
     *
     * The transform is only set around the scene's draw pass, so a test cannot read it afterwards; the scene's active view and
     * its version are recorded the same way, to show they are right inside OnDraw(). Reflected so a
     * SceneConfig-style XML can create it by type name (see SceneViewTest.cpp).
     */
    class ViewProbe : public IComponent {
        BE_CLASS(ViewProbe)
    public:
        ViewProbe() = default;
        ~ViewProbe() override = default;

        ViewTransform seen;
        ActiveSceneView seenView;
        uint32_t seenVersion = 0;
        int drawCount = 0;

        void OnDraw() override;
    };

}  // namespace BECore::Tests
