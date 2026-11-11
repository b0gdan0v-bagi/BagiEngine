#pragma once

#include <BECore/Renderer/Gpu/IGpuDevice.h>
#include <BECore/Renderer/Render3D/MeshData.h>

namespace BECore {

    /// The GPU side of a MeshData: a vertex buffer and a 32-bit index buffer of one device. Release it before the device (the rule of IGpuBuffer).
    class Mesh : public RefCounted {
    public:
        Mesh(IntrusivePtr<IGpuBuffer> vertexBuffer, IntrusivePtr<IGpuBuffer> indexBuffer, uint32_t indexCount)
            : _vertexBuffer(std::move(vertexBuffer)), _indexBuffer(std::move(indexBuffer)), _indexCount(indexCount) {}

        /// Uploads @p data through @p device; empty when @p data has no triangles or a buffer could not be made
        [[nodiscard]] static IntrusivePtr<Mesh> Create(IGpuDevice& device, const MeshData& data, PoolString debugName = {}) {
            if (data.vertices.empty() || data.indices.empty()) {
                return {};
            }
            const eastl::span<const std::byte> vertexBytes = AsBytes(data.vertices);
            const eastl::span<const std::byte> indexBytes = AsBytes(data.indices);
            IntrusivePtr<IGpuBuffer> vertexBuffer = device.CreateBuffer(BufferDesc{BufferUsage::Vertex, static_cast<uint32_t>(vertexBytes.size()), debugName}, vertexBytes);
            IntrusivePtr<IGpuBuffer> indexBuffer = device.CreateBuffer(BufferDesc{BufferUsage::Index, static_cast<uint32_t>(indexBytes.size()), debugName}, indexBytes);
            if (!vertexBuffer || !indexBuffer) {
                return {};
            }
            return New<Mesh>(std::move(vertexBuffer), std::move(indexBuffer), static_cast<uint32_t>(data.indices.size()));
        }

        [[nodiscard]] IGpuBuffer& GetVertexBuffer() const {
            return *_vertexBuffer;
        }
        [[nodiscard]] IGpuBuffer& GetIndexBuffer() const {
            return *_indexBuffer;
        }
        /// The index type of the index buffer
        [[nodiscard]] static constexpr IndexType GetIndexType() {
            return IndexType::Uint32;
        }
        [[nodiscard]] uint32_t IndexCount() const {
            return _indexCount;
        }

    private:
        template <typename T>
        [[nodiscard]] static eastl::span<const std::byte> AsBytes(const eastl::vector<T>& values) {
            return {reinterpret_cast<const std::byte*>(values.data()), values.size() * sizeof(T)};
        }

        IntrusivePtr<IGpuBuffer> _vertexBuffer;
        IntrusivePtr<IGpuBuffer> _indexBuffer;
        uint32_t _indexCount = 0;
    };

}  // namespace BECore
