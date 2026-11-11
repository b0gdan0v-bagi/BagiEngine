#pragma once

#include <BECore/Renderer/Gpu/GpuTypes.h>

namespace BECore {

    /// A depth attachment. It is an object the caller makes and keeps (IGpuDevice::CreateDepthTexture()) and names in PassDesc::depth, not a
    /// hidden buffer of the device: it has to match the size of the target, the caller knows when that changes, and a shadow map
    /// is the same object with another size. It is not an ITexture: nothing samples it in this interface.
    class IGpuDepthTexture : public RefCounted {
    public:
        ~IGpuDepthTexture() override = default;

        [[nodiscard]] virtual uint32_t GetWidth() const = 0;
        [[nodiscard]] virtual uint32_t GetHeight() const = 0;
        [[nodiscard]] virtual DepthFormat GetFormat() const = 0;
    };

}  // namespace BECore
