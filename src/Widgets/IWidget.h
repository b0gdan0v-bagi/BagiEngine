#pragma once

#include <Core/Utils/RefCounted.h>

namespace Core {

    class IWidget : public RefCounted {
    public:
        IWidget() = default;
        virtual ~IWidget() = default;

        virtual void Draw() = 0;
    };

}  // namespace Core

