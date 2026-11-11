#include <BECore/Renderer/Gpu/GpuValidation.h>
#include <BECore/Renderer/Gpu/IGpuDepthTexture.h>

namespace BECore {

    namespace {

        GpuValidation ValidateConstantsSize(const char* what, uint32_t size) {
            if (size > kMaxConstantsBytes) {
                return std::unexpected(Format("{} constants are {} bytes, at most {} are allowed", what, size, kMaxConstantsBytes));
            }
            if (size % kConstantsAlignment != 0) {
                return std::unexpected(Format("{} constants are {} bytes, not a multiple of {}", what, size, kConstantsAlignment));
            }
            return {};
        }

        GpuValidation ValidateVertexLayout(const VertexLayout& layout) {
            if (layout.attributeCount > kMaxVertexAttributes) {
                return std::unexpected(Format("the vertex layout has {} attributes, at most {} are allowed", layout.attributeCount, kMaxVertexAttributes));
            }
            for (uint32_t i = 0; i < layout.attributeCount; ++i) {
                const VertexAttribute& attribute = layout.attributes[i];
                if (attribute.offset + VertexFormatSize(attribute.format) > layout.stride) {
                    return std::unexpected(Format("the vertex attribute at location {} does not fit the stride of {} bytes", attribute.location, layout.stride));
                }
                for (uint32_t other = 0; other < i; ++other) {
                    if (layout.attributes[other].location == attribute.location) {
                        return std::unexpected(Format("the vertex layout uses location {} twice", attribute.location));
                    }
                }
            }
            return {};
        }

    }  // namespace

    GpuValidation ValidatePipelineDesc(const PipelineDesc& desc) {
        if (desc.shader.Empty()) {
            return std::unexpected(eastl::string("the pipeline has no shader"));
        }
        if (auto frame = ValidateConstantsSize("frame", desc.frameConstantsSize); !frame) {
            return frame;
        }
        if (auto draw = ValidateConstantsSize("draw", desc.drawConstantsSize); !draw) {
            return draw;
        }
        if (auto layout = ValidateVertexLayout(desc.vertexLayout); !layout) {
            return layout;
        }
        if ((desc.depthTest || desc.depthWrite) && desc.depthFormat == DepthFormat::None) {
            return std::unexpected(eastl::string("the pipeline tests or writes depth but has no depth format"));
        }
        return {};
    }

    GpuValidation ValidateConstants(const PipelineDesc& desc, ConstantsSlot slot, size_t byteCount) {
        const uint32_t declared = desc.ConstantsSize(slot);
        if (byteCount > kMaxConstantsBytes) {
            return std::unexpected(Format("a constants block of {} bytes is over the limit of {}", byteCount, kMaxConstantsBytes));
        }
        if (byteCount != declared) {
            return std::unexpected(Format("the pipeline declares {} bytes for this constants slot, not {}", declared, byteCount));
        }
        return {};
    }

    GpuValidation ValidatePassDesc(const PassDesc& desc) {
        if (desc.depth) {
            if (desc.depth->texture == nullptr) {
                return std::unexpected(eastl::string("the pass has a depth attachment without a texture"));
            }
            if (desc.depth->clearValue < 0.0f || desc.depth->clearValue > 1.0f) {
                return std::unexpected(Format("the depth clear value {} is outside 0 to 1", desc.depth->clearValue));
            }
        }
        return {};
    }

}  // namespace BECore
