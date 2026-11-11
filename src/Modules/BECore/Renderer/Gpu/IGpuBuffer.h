#pragma once

#include <BECore/Renderer/Gpu/GpuTypes.h>

namespace BECore {

    /// A vertex or an index buffer of a device. Its content is set at creation (IGpuDevice::CreateBuffer()); a way to update one comes with the
    /// 2D batcher that needs it. Release it only when no recorded frame still uses it (the same rule as a texture, IRenderer::WaitIdle()).
    class IGpuBuffer : public RefCounted {
    public:
        ~IGpuBuffer() override = default;

        [[nodiscard]] virtual const BufferDesc& GetDesc() const = 0;
    };

}  // namespace BECore
