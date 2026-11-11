#pragma once

#include <BECore/Renderer/BlendMode.h>

namespace BECore {

    class IGpuDepthTexture;
    class IRenderTarget;

    /// The name of a shader of the build. The backend resolves it to the blobs it compiled in (the shaders are written once in Slang,
    /// .claude/rules/rendering-module.md); no GPU handle or shader language appears above the device.
    using ShaderId = PoolString;

    /// The type of one vertex attribute
    CORE_ENUM(VertexFormat, uint8_t,
              Float2,   ///< two 32-bit floats
              Float3,   ///< three 32-bit floats
              Float4,   ///< four 32-bit floats
              Unorm8x4  ///< four bytes read as 0 to 1 (a Color)
    )

    CORE_ENUM(IndexType, uint8_t, Uint16, Uint32)

    CORE_ENUM(CullMode, uint8_t,
              None,   ///< both sides are drawn
              Back,   ///< triangles that are clockwise seen from the camera are dropped (the front face is counter-clockwise)
              Front)  ///< the other way round

    CORE_ENUM(DepthCompare, uint8_t, Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always)

    /// What a pass does with an attachment when it opens
    CORE_ENUM(PassLoadOp, uint8_t,
              Clear,     ///< fills it with the clear value
              Load,      ///< keeps what is there (a 2D pass drawn after the 3D one)
              DontCare)  ///< the old content is not used (undefined until drawn over)

    CORE_ENUM(DepthFormat, uint8_t,
              None,          ///< no depth attachment
              Depth32Float)  ///< 32-bit float depth

    CORE_ENUM(BufferUsage, uint8_t, Vertex, Index)

    /// The two blocks of constants a draw reads. Slot values are the numbers the shaders bind them to.
    CORE_ENUM(ConstantsSlot, uint8_t,
              Frame,  ///< slot 0: what the whole pass shares (view-projection, light)
              Draw)   ///< slot 1: what one draw owns (model matrix, tint)

    /// The size of a constant block in the pipeline description and in SetConstants() is at most this (the Vulkan push-constant minimum), and a multiple of kConstantsAlignment
    constexpr uint32_t kMaxConstantsBytes = 128;
    constexpr uint32_t kConstantsAlignment = 16;
    constexpr size_t kMaxVertexAttributes = 8;
    constexpr uint32_t kMaxTextureSlots = 4;
    constexpr size_t kConstantsSlotCount = 2;
    constexpr uint32_t kFloatBytes = sizeof(float);
    constexpr uint32_t kUnorm8x4Bytes = 4;

    /// The size in bytes of one attribute of @p format
    [[nodiscard]] constexpr uint32_t VertexFormatSize(VertexFormat format) {
        switch (format) {
            case VertexFormat::Float2:
                return 2 * kFloatBytes;
            case VertexFormat::Float3:
                return 3 * kFloatBytes;
            case VertexFormat::Float4:
                return 4 * kFloatBytes;
            case VertexFormat::Unorm8x4:
                return kUnorm8x4Bytes;
        }
        return 0;
    }

    [[nodiscard]] constexpr uint32_t IndexTypeSize(IndexType type) {
        return type == IndexType::Uint16 ? sizeof(uint16_t) : sizeof(uint32_t);
    }

    struct VertexAttribute {
        uint32_t location = 0;  ///< the `location` of the vertex shader input
        VertexFormat format = VertexFormat::Float3;
        uint32_t offset = 0;  ///< bytes from the start of the vertex
    };

    /// One vertex buffer of vertices @p stride bytes apart, read as @p attributeCount attributes
    struct VertexLayout {
        uint32_t stride = 0;
        std::array<VertexAttribute, kMaxVertexAttributes> attributes{};
        uint32_t attributeCount = 0;

        /// Appends an attribute; false (and nothing added) when kMaxVertexAttributes are in use
        bool AddAttribute(uint32_t location, VertexFormat format, uint32_t offset) {
            if (attributeCount >= kMaxVertexAttributes) {
                return false;
            }
            attributes[attributeCount++] = VertexAttribute{location, format, offset};
            return true;
        }
    };

    /// All the state of a pipeline. The device makes it from this and a shader of the build; nothing else is hidden in it.
    /// The color format is not here: it is the one of the pass target (the back buffer or an IRenderTarget).
    /// The defaults are the 2D defaults: no depth, straight alpha, both sides drawn.
    struct PipelineDesc {
        ShaderId shader;
        VertexLayout vertexLayout;
        CullMode cullMode = CullMode::None;
        bool depthTest = false;
        bool depthWrite = false;
        DepthCompare depthCompare = DepthCompare::Less;
        BlendMode blend = BlendMode::Alpha;
        DepthFormat depthFormat = DepthFormat::None;  ///< the depth attachment the pipeline is used with; a pass with another one cannot bind it
        uint32_t frameConstantsSize = 0;              ///< bytes of ConstantsSlot::Frame, 0 for none
        uint32_t drawConstantsSize = 0;               ///< bytes of ConstantsSlot::Draw, 0 for none
        PoolString debugName;

        [[nodiscard]] uint32_t ConstantsSize(ConstantsSlot slot) const {
            return slot == ConstantsSlot::Frame ? frameConstantsSize : drawConstantsSize;
        }
    };

    /// The depth attachment of a pass
    struct PassDepth {
        IGpuDepthTexture* texture = nullptr;  ///< from IGpuDevice::CreateDepthTexture(), alive until the pass ends; the size of the target
        PassLoadOp loadOp = PassLoadOp::Clear;
        float clearValue = 1.0f;  ///< 0 (near) to 1 (far)
    };

    /// One pass: where it draws, what it does with the color on opening, and the optional depth attachment
    struct PassDesc {
        IRenderTarget* target = nullptr;  ///< null is the back buffer
        PassLoadOp colorLoadOp = PassLoadOp::Clear;
        Color clearColor{0, 0, 0, 255};
        std::optional<PassDepth> depth;  ///< none: no depth attachment
        PoolString debugName;
    };

    struct BufferDesc {
        BufferUsage usage = BufferUsage::Vertex;
        uint32_t size = 0;  ///< bytes
        PoolString debugName;
    };

}  // namespace BECore
