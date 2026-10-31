#pragma once

#include <BECore/Widgets/IWidget.h>

namespace BECore {

    class IDeserializer;

    // Draws the active scene straight into the back buffer: the Game run mode's replacement for ViewportWidget.
    // ClearScreenWidget::Update() opens the swapchain pass, so the scene is drawn in DrawScreen(), after every
    // widget's Update(). Window coordinates are scene coordinates, so mouse events need no remapping.
    class SceneRenderWidget : public IWidget {
        BE_CLASS(SceneRenderWidget)
    public:
        SceneRenderWidget() = default;
        ~SceneRenderWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void DrawScreen() override;
        BE_FUNCTION void Draw() override;
    };

}  // namespace BECore
