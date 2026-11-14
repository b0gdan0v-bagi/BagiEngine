#pragma once

#include <BECore/Renderer/Gpu/GpuTypes.h>

namespace BECore {

    using GpuValidation = std::expected<void, String>;

    /// The rules every IGpuDevice applies to a description, so the backends and the test double agree: the constant sizes (each at most
    /// kMaxConstantsBytes, a multiple of kConstantsAlignment), a vertex layout that fits its stride with distinct locations, depth state only
    /// with a depth attachment.
    [[nodiscard]] GpuValidation ValidatePipelineDesc(const PipelineDesc& desc);

    /// A block for @p slot of a pipeline described by @p desc must be exactly the size the description declares
    [[nodiscard]] GpuValidation ValidateConstants(const PipelineDesc& desc, ConstantsSlot slot, size_t byteCount);

    /// The depth clear value is 0 to 1 and a depth attachment names a texture
    [[nodiscard]] GpuValidation ValidatePassDesc(const PassDesc& desc);

}  // namespace BECore
