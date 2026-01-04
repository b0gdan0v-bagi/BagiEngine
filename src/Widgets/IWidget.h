#pragma once

#include <Core/RefCounted/RefCounted.h>
#include <Core/Config/XmlNode.h>

namespace Core {

    class IWidget : public RefCounted {
    public:
        IWidget() = default;
        virtual ~IWidget() = default;

        virtual bool Initialize(const XmlNode& node) = 0;

        virtual void Update() = 0;
        virtual void Draw() = 0;
    };

}  // namespace Core

