#pragma once

#include <BECore/Renderer/Gpu/GpuTypes.h>

namespace BECore {

    /// A pipeline made by IGpuDevice::CreatePipeline() from a PipelineDesc. Bind it with IGpuDevice::SetPipeline() inside a pass whose
    /// attachments match the description.
    class IGpuPipeline : public RefCounted {
    public:
        ~IGpuPipeline() override = default;

        [[nodiscard]] virtual const PipelineDesc& GetDesc() const = 0;
    };

}  // namespace BECore
