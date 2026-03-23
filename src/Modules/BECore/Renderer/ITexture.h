#pragma once

#include <BECore/Resource/IResource.h>

namespace BECore {

    class ITexture : public IResource {
        BE_CLASS(ITexture, FACTORY_BASE)
    public:
        ~ITexture() override = default;

        [[nodiscard]] virtual float GetWidth() const = 0;
        [[nodiscard]] virtual float GetHeight() const = 0;
    };

}  // namespace BECore
