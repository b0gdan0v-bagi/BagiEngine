#pragma once

#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/Resource/IResource.h>

namespace BECore {

    /// The path a texture reports when it was made from pixels in memory and no file sits behind it.
    struct TexturePaths {
        static constexpr auto memory = "<memory>"_intern;
    };

    class ITexture : public IResource {
        BE_CLASS(ITexture, FACTORY_BASE)
    public:
        ~ITexture() override = default;

        [[nodiscard]] virtual float GetWidth() const = 0;
        [[nodiscard]] virtual float GetHeight() const = 0;

        [[nodiscard]] virtual void* GetNativeHandle() const = 0;
    };

}  // namespace BECore
