#pragma once

#include "IWidget.h"

namespace Core {

    class ImGuiWidget : public IWidget {
    public:
        ImGuiWidget() = default;
        ~ImGuiWidget() override = default;

        void Draw() override;
    };

}  // namespace Core

