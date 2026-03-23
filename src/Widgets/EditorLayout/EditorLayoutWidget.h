#pragma once

#include <BECore/Widgets/IWidget.h>

namespace BECore {

    class IDeserializer;

    // Full-screen DockSpace host that provides the unified editor layout.
    // Must be registered immediately after ImGuiWidget so all other editor
    // windows dock into this space.
    class EditorLayoutWidget : public IWidget {
        BE_CLASS(EditorLayoutWidget)
    public:
        EditorLayoutWidget() = default;
        ~EditorLayoutWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;
    };

}  // namespace BECore
