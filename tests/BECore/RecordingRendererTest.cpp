#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kFrames = 16;
        constexpr size_t kLargeVertexCount = 64;
        constexpr size_t kSmallVertexCount = 3;

        Vector<GeometryVertex> MakeVertices(size_t count, float x) {
            Vector<GeometryVertex> vertices(count);
            for (GeometryVertex& vertex : vertices) {
                vertex.x = x;
            }
            return vertices;
        }

        Vector<int> MakeIndices(size_t count) {
            Vector<int> indices(count);
            for (size_t i = 0; i < count; ++i) {
                indices[i] = static_cast<int>(i);
            }
            return indices;
        }

        void Draw(RecordingRenderer& renderer, const Vector<GeometryVertex>& vertices, const Vector<int>& indices) {
            renderer.DrawGeometry(nullptr, Span<const GeometryVertex>(vertices.data(), vertices.size()), Span<const int>(indices.data(), indices.size()), 0.0f, 0.0f);
        }

    }  // namespace

    TEST(RecordingRendererTest, EachDrawOfAnEqualFrameGetsTheStorageItHadBefore) {
        RecordingRenderer renderer;
        const Vector<GeometryVertex> large = MakeVertices(kLargeVertexCount, 1.0f);
        const Vector<GeometryVertex> small = MakeVertices(kSmallVertexCount, 2.0f);
        const Vector<int> largeIndices = MakeIndices(kLargeVertexCount);
        const Vector<int> smallIndices = MakeIndices(kSmallVertexCount);

        Draw(renderer, large, largeIndices);
        Draw(renderer, small, smallIndices);
        const GeometryVertex* const firstVertices = At(renderer.calls, 0).vertices.data();
        const GeometryVertex* const secondVertices = At(renderer.calls, 1).vertices.data();
        const int* const firstIndices = At(renderer.calls, 0).indices.data();

        renderer.ClearCalls();
        Draw(renderer, large, largeIndices);
        Draw(renderer, small, smallIndices);

        ASSERT_EQ(renderer.calls.size(), 2u);
        EXPECT_EQ(At(renderer.calls, 0).vertices.data(), firstVertices);
        EXPECT_EQ(At(renderer.calls, 1).vertices.data(), secondVertices);
        EXPECT_EQ(At(renderer.calls, 0).indices.data(), firstIndices);
    }

    TEST(RecordingRendererTest, ReusedStorageHoldsOnlyTheGeometryOfTheNewDraw) {
        RecordingRenderer renderer;
        Draw(renderer, MakeVertices(kLargeVertexCount, 1.0f), MakeIndices(kLargeVertexCount));

        renderer.ClearCalls();
        Draw(renderer, MakeVertices(kSmallVertexCount, 2.0f), MakeIndices(kSmallVertexCount));

        ASSERT_EQ(renderer.calls.size(), 1u);
        const DrawCall& call = At(renderer.calls, 0);
        EXPECT_EQ(call.kind, DrawKind::Geometry);
        ASSERT_EQ(call.vertices.size(), kSmallVertexCount);
        EXPECT_EQ(call.indices.size(), kSmallVertexCount);
        for (const GeometryVertex& vertex : call.vertices) {
            EXPECT_FLOAT_EQ(vertex.x, 2.0f);
        }
    }

    TEST(RecordingRendererTest, ARepeatedFrameOfGeometryAllocatesNothing) {
        RecordingRenderer renderer;
        const Vector<GeometryVertex> large = MakeVertices(kLargeVertexCount, 1.0f);
        const Vector<GeometryVertex> small = MakeVertices(kSmallVertexCount, 2.0f);
        const Vector<int> largeIndices = MakeIndices(kLargeVertexCount);
        const Vector<int> smallIndices = MakeIndices(kSmallVertexCount);
        const auto frame = [&] {
            renderer.ClearCalls();
            Draw(renderer, large, largeIndices);
            Draw(renderer, small, smallIndices);
            renderer.DrawFilledRect(0.0f, 0.0f, 1.0f, 1.0f, Color{});
        };
        frame();
        frame();

        const uint64_t before = AllocTracker::GetThreadCounters().count;
        for (int i = 0; i < kFrames; ++i) {
            frame();
        }
        const uint64_t allocations = AllocTracker::GetThreadCounters().count - before;

        EXPECT_EQ(renderer.calls.size(), 3u);
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(allocations, 0u);
        }
    }

}  // namespace BECore::Tests
