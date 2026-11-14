#pragma once

#include <BECore/Renderer/Gpu/GpuValidation.h>
#include <BECore/Renderer/Gpu/IGpuDevice.h>
#include <BECore/Renderer/ITexture.h>

namespace BECore::Tests {

    class RecordedGpuBuffer : public IGpuBuffer {
    public:
        RecordedGpuBuffer(const BufferDesc& desc, eastl::span<const std::byte> initial) : _desc(desc), _bytes(desc.size, std::byte{0}) {
            std::copy(initial.begin(), initial.end(), _bytes.begin());
        }

        const BufferDesc& GetDesc() const override {
            return _desc;
        }
        /// The content: @p desc.size bytes, the initial data first
        const eastl::vector<std::byte>& GetBytes() const {
            return _bytes;
        }

    private:
        BufferDesc _desc;
        eastl::vector<std::byte> _bytes;
    };

    class RecordedGpuPipeline : public IGpuPipeline {
    public:
        explicit RecordedGpuPipeline(const PipelineDesc& desc) : _desc(desc) {}

        const PipelineDesc& GetDesc() const override {
            return _desc;
        }

    private:
        PipelineDesc _desc;
    };

    class RecordedGpuDepthTexture : public IGpuDepthTexture {
    public:
        RecordedGpuDepthTexture(uint32_t width, uint32_t height) : _width(width), _height(height) {}

        uint32_t GetWidth() const override {
            return _width;
        }
        uint32_t GetHeight() const override {
            return _height;
        }
        DepthFormat GetFormat() const override {
            return DepthFormat::Depth32Float;
        }

    private:
        uint32_t _width = 0;
        uint32_t _height = 0;
    };

    CORE_ENUM(GpuCommandKind, uint8_t, BeginPass, EndPass, SetPipeline, SetVertexBuffer, SetIndexBuffer, SetTexture, SetConstants, SetViewport, SetScissor, DrawIndexed)

    /// One call of the recording part of IGpuDevice; only the fields of its kind are set
    struct GpuCommand {
        GpuCommandKind kind = GpuCommandKind::BeginPass;
        PassDesc pass;                                       ///< BeginPass
        const IGpuPipeline* pipeline = nullptr;              ///< SetPipeline
        const IGpuBuffer* buffer = nullptr;                  ///< SetVertexBuffer, SetIndexBuffer
        IndexType indexType = IndexType::Uint32;             ///< SetIndexBuffer
        uint32_t textureSlot = 0;                            ///< SetTexture
        const ITexture* texture = nullptr;                   ///< SetTexture
        ConstantsSlot constantsSlot = ConstantsSlot::Frame;  ///< SetConstants
        eastl::vector<std::byte> constants;                  ///< SetConstants: the bytes as they were passed
        Rect rect;                                           ///< SetViewport, SetScissor
        uint32_t indexCount = 0;                             ///< DrawIndexed
        uint32_t firstIndex = 0;                             ///< DrawIndexed
        int32_t vertexOffset = 0;                            ///< DrawIndexed
    };

    /// A DrawIndexed() with the state it saw: what a test of a renderer wants to read without replaying the commands
    struct RecordedDraw {
        const IGpuPipeline* pipeline = nullptr;
        const IGpuBuffer* vertexBuffer = nullptr;
        const IGpuBuffer* indexBuffer = nullptr;
        IndexType indexType = IndexType::Uint32;
        std::array<eastl::vector<std::byte>, kConstantsSlotCount> constants;  ///< by ConstantsSlot, the last block set (empty when none)
        std::array<const ITexture*, kMaxTextureSlots> textures{};
        Rect viewport;  ///< as set in the pass; empty when none was (the whole target)
        Rect scissor;
        uint32_t indexCount = 0;
        uint32_t firstIndex = 0;
        int32_t vertexOffset = 0;

        /// The bytes of @p slot read back as a @p T (what is missing stays zero when the block is shorter)
        template <typename T>
        [[nodiscard]] T ConstantsAs(ConstantsSlot slot) const {
            T value{};
            const eastl::vector<std::byte>& bytes = constants[static_cast<size_t>(slot)];
            std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(T)));
            return value;
        }
    };

    /**
     * @brief An IGpuDevice that draws nothing and records what it was asked to do. Needs no GPU, so the renderer above it is tested anywhere.
     *
     * Keeps the commands in order, a RecordedDraw per draw (the state at the call), the content of the buffers, and counters of what it made. It
     * applies the shared rules of GpuValidation.h and the order rules of a pass; a call that breaks one is not recorded and its reason is
     * appended to `errors`, so a test asserts on it.
     */
    class RecordingGpuDevice : public IGpuDevice {
    public:
        IntrusivePtr<IGpuBuffer> CreateBuffer(const BufferDesc& desc, eastl::span<const std::byte> initial) override {
            if (desc.size == 0 || initial.size() > desc.size) {
                Fail(Format("a buffer of {} bytes cannot hold {} bytes of initial data", desc.size, initial.size()));
                return {};
            }
            ++buffersCreated;
            return New<RecordedGpuBuffer>(desc, initial);
        }

        std::expected<IntrusivePtr<IGpuPipeline>, String> CreatePipeline(const PipelineDesc& desc) override {
            if (GpuValidation valid = ValidatePipelineDesc(desc); !valid) {
                return std::unexpected(valid.error());
            }
            ++pipelinesCreated;
            return IntrusivePtr<IGpuPipeline>(New<RecordedGpuPipeline>(desc));
        }

        IntrusivePtr<IGpuDepthTexture> CreateDepthTexture(uint32_t width, uint32_t height) override {
            if (width == 0 || height == 0) {
                Fail("a depth texture needs a size");
                return {};
            }
            ++depthTexturesCreated;
            return New<RecordedGpuDepthTexture>(width, height);
        }

        void BeginPass(const PassDesc& desc) override {
            if (_passOpen) {
                Fail("BeginPass inside a pass");
                return;
            }
            if (GpuValidation valid = ValidatePassDesc(desc); !valid) {
                Fail(valid.error());
                return;
            }
            _passOpen = true;
            _state = {};
            _passDesc = desc;
            GpuCommand command;
            command.kind = GpuCommandKind::BeginPass;
            command.pass = desc;
            commands.push_back(std::move(command));
        }

        void EndPass() override {
            if (!RequirePass("EndPass")) {
                return;
            }
            _passOpen = false;
            GpuCommand command;
            command.kind = GpuCommandKind::EndPass;
            commands.push_back(std::move(command));
        }

        void SetPipeline(IGpuPipeline& pipeline) override {
            if (!RequirePass("SetPipeline")) {
                return;
            }
            const bool passHasDepth = _passDesc.depth.has_value();
            const bool pipelineHasDepth = pipeline.GetDesc().depthFormat != DepthFormat::None;
            if (passHasDepth != pipelineHasDepth) {
                Fail("the pipeline and the pass disagree about the depth attachment");
                return;
            }
            _state.pipeline = &pipeline;
            _state.constants = {};  // the layouts of two pipelines may differ
            _state.constantsSet = {};
            GpuCommand command;
            command.kind = GpuCommandKind::SetPipeline;
            command.pipeline = &pipeline;
            commands.push_back(std::move(command));
        }

        void SetVertexBuffer(IGpuBuffer& buffer) override {
            if (!RequirePass("SetVertexBuffer")) {
                return;
            }
            if (buffer.GetDesc().usage != BufferUsage::Vertex) {
                Fail("SetVertexBuffer got a buffer that is not a vertex buffer");
                return;
            }
            _state.vertexBuffer = &buffer;
            GpuCommand command;
            command.kind = GpuCommandKind::SetVertexBuffer;
            command.buffer = &buffer;
            commands.push_back(std::move(command));
        }

        void SetIndexBuffer(IGpuBuffer& buffer, IndexType type) override {
            if (!RequirePass("SetIndexBuffer")) {
                return;
            }
            if (buffer.GetDesc().usage != BufferUsage::Index) {
                Fail("SetIndexBuffer got a buffer that is not an index buffer");
                return;
            }
            _state.indexBuffer = &buffer;
            _state.indexType = type;
            GpuCommand command;
            command.kind = GpuCommandKind::SetIndexBuffer;
            command.buffer = &buffer;
            command.indexType = type;
            commands.push_back(std::move(command));
        }

        void SetTexture(uint32_t slot, ITexture& texture) override {
            if (!RequirePass("SetTexture")) {
                return;
            }
            if (slot >= kMaxTextureSlots) {
                Fail(Format("texture slot {} is past the {} slots", slot, kMaxTextureSlots));
                return;
            }
            _state.textures[slot] = &texture;
            GpuCommand command;
            command.kind = GpuCommandKind::SetTexture;
            command.textureSlot = slot;
            command.texture = &texture;
            commands.push_back(std::move(command));
        }

        void SetConstants(ConstantsSlot slot, eastl::span<const std::byte> bytes) override {
            if (!RequirePass("SetConstants")) {
                return;
            }
            if (_state.pipeline == nullptr) {
                Fail("SetConstants without a pipeline");
                return;
            }
            if (GpuValidation valid = ValidateConstants(_state.pipeline->GetDesc(), slot, bytes.size()); !valid) {
                Fail(valid.error());
                return;
            }
            _state.constants[static_cast<size_t>(slot)].assign(bytes.begin(), bytes.end());
            _state.constantsSet[static_cast<size_t>(slot)] = true;
            GpuCommand command;
            command.kind = GpuCommandKind::SetConstants;
            command.constantsSlot = slot;
            command.constants.assign(bytes.begin(), bytes.end());
            commands.push_back(std::move(command));
        }

        void SetViewport(const Rect& rect) override {
            if (!RequirePass("SetViewport")) {
                return;
            }
            _state.viewport = rect;
            GpuCommand command;
            command.kind = GpuCommandKind::SetViewport;
            command.rect = rect;
            commands.push_back(std::move(command));
        }

        void SetScissor(const Rect& rect) override {
            if (!RequirePass("SetScissor")) {
                return;
            }
            _state.scissor = rect;
            GpuCommand command;
            command.kind = GpuCommandKind::SetScissor;
            command.rect = rect;
            commands.push_back(std::move(command));
        }

        void DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) override {
            if (!RequirePass("DrawIndexed")) {
                return;
            }
            if (_state.pipeline == nullptr) {
                Fail("DrawIndexed without a pipeline");
                return;
            }
            if (_state.vertexBuffer == nullptr || _state.indexBuffer == nullptr) {
                Fail("DrawIndexed without a vertex buffer and an index buffer");
                return;
            }
            const PipelineDesc& desc = _state.pipeline->GetDesc();
            for (size_t slot = 0; slot < kConstantsSlotCount; ++slot) {
                if (desc.ConstantsSize(static_cast<ConstantsSlot>(slot)) != 0 && !_state.constantsSet[slot]) {
                    Fail(Format("DrawIndexed before the constants of slot {} were set", slot));
                    return;
                }
            }
            RecordedDraw draw;
            draw.pipeline = _state.pipeline;
            draw.vertexBuffer = _state.vertexBuffer;
            draw.indexBuffer = _state.indexBuffer;
            draw.indexType = _state.indexType;
            draw.constants = _state.constants;
            draw.textures = _state.textures;
            draw.viewport = _state.viewport;
            draw.scissor = _state.scissor;
            draw.indexCount = indexCount;
            draw.firstIndex = firstIndex;
            draw.vertexOffset = vertexOffset;
            draws.push_back(std::move(draw));
            GpuCommand command;
            command.kind = GpuCommandKind::DrawIndexed;
            command.indexCount = indexCount;
            command.firstIndex = firstIndex;
            command.vertexOffset = vertexOffset;
            commands.push_back(std::move(command));
        }

        /// How many recorded commands are of @p kind
        [[nodiscard]] size_t CountCommands(GpuCommandKind kind) const {
            return static_cast<size_t>(std::count_if(commands.begin(), commands.end(), [kind](const GpuCommand& command) { return command.kind == kind; }));
        }

        /// The recorded commands of @p kind, in order
        [[nodiscard]] eastl::vector<const GpuCommand*> CommandsOf(GpuCommandKind kind) const {
            eastl::vector<const GpuCommand*> result;
            for (const GpuCommand& command : commands) {
                if (command.kind == kind) {
                    result.push_back(&command);
                }
            }
            return result;
        }

        [[nodiscard]] bool IsPassOpen() const {
            return _passOpen;
        }

        eastl::vector<GpuCommand> commands;
        eastl::vector<RecordedDraw> draws;
        eastl::vector<String> errors;  ///< one reason per call that broke a rule
        uint32_t buffersCreated = 0;
        uint32_t pipelinesCreated = 0;
        uint32_t depthTexturesCreated = 0;

    private:
        /// What is bound in the open pass
        struct BoundState {
            const IGpuPipeline* pipeline = nullptr;
            const IGpuBuffer* vertexBuffer = nullptr;
            const IGpuBuffer* indexBuffer = nullptr;
            IndexType indexType = IndexType::Uint32;
            std::array<eastl::vector<std::byte>, kConstantsSlotCount> constants;
            std::array<bool, kConstantsSlotCount> constantsSet{};
            std::array<const ITexture*, kMaxTextureSlots> textures{};
            Rect viewport;
            Rect scissor;
        };

        void Fail(String reason) {
            errors.push_back(std::move(reason));
        }

        bool RequirePass(const char* call) {
            if (!_passOpen) {
                Fail(Format("{} outside a pass", call));
            }
            return _passOpen;
        }

        bool _passOpen = false;
        PassDesc _passDesc;
        BoundState _state;
    };

}  // namespace BECore::Tests
