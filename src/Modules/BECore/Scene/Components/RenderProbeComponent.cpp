#include "RenderProbeComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/ApplicationEvents.h>
#include <Generated/RenderProbeComponent.gen.hpp>

namespace BECore {

    namespace {

        constexpr float kCell = 100.0f;
        constexpr float kGap = 20.0f;
        constexpr float kCheckerSquare = 10.0f;
        constexpr float kClipOverflow = 30.0f;
        constexpr float kInnerClipInset = 25.0f;
        constexpr uint32_t kGradientSize = 16;
        constexpr uint32_t kGradientStep = 16;
        constexpr uint8_t kGradientBlue = 128;
        constexpr uint8_t kOpaque = 255;

        const Color kCheckerDark{70, 70, 70, kOpaque};
        const Color kCheckerLight{150, 150, 150, kOpaque};
        const Color kTintRed{kOpaque, 64, 64, kOpaque};
        const Color kHalfAlpha{kOpaque, kOpaque, kOpaque, 128};
        const Color kGlassRed{kOpaque, 0, 0, 128};
        const Color kGlassBlue{0, 0, kOpaque, 128};
        const Color kClipOuter{kOpaque, 0, 0, kOpaque};
        const Color kClipInner{0, kOpaque, 0, kOpaque};

        /// A square of red and green gradients: the corners tell the orientation of the texture and of the tint
        IntrusivePtr<ITexture> MakeGradient(IRenderer& renderer) {
            eastl::vector<uint8_t> pixels;
            pixels.reserve(static_cast<size_t>(kGradientSize) * kGradientSize * 4);
            for (uint32_t y = 0; y < kGradientSize; ++y) {
                for (uint32_t x = 0; x < kGradientSize; ++x) {
                    pixels.push_back(static_cast<uint8_t>(x * kGradientStep));
                    pixels.push_back(static_cast<uint8_t>(y * kGradientStep));
                    pixels.push_back(kGradientBlue);
                    pixels.push_back(kOpaque);
                }
            }
            return renderer.CreateTexture(pixels.data(), kGradientSize, kGradientSize);
        }

        const Color kCornerRed{kOpaque, 0, 0, kOpaque};
        const Color kCornerGreen{0, kOpaque, 0, kOpaque};
        const Color kCornerBlue{0, 0, kOpaque, kOpaque};
        const Color kCornerFaint{kOpaque, kOpaque, kOpaque, 64};
        const Color kWhite{kOpaque, kOpaque, kOpaque, kOpaque};

        /// Two triangles over the unit square at the origin, wound the same way the quad of a texture is
        constexpr std::array<int, 6> kQuadIndices{0, 1, 2, 2, 1, 3};

        /// Untextured: a color per corner, so the interpolation and the blending of the last corner (it is faint) are visible
        void DrawColorQuad(IRenderer& renderer, float x, float y, float size) {
            const std::array<GeometryVertex, 4> vertices{{
                {0.0f, 0.0f, kCornerRed, 0.0f, 0.0f},
                {size, 0.0f, kCornerGreen, 1.0f, 0.0f},
                {0.0f, size, kCornerBlue, 0.0f, 1.0f},
                {size, size, kCornerFaint, 1.0f, 1.0f},
            }};
            renderer.DrawGeometry(nullptr, {vertices.data(), vertices.size()}, {kQuadIndices.data(), kQuadIndices.size()}, x, y);
        }

        /// Textured and mirrored left to right (u runs from 1 to 0), so it differs from DrawTexture() in the corners, drawn through the translation
        void DrawMirroredTexture(IRenderer& renderer, ITexture& texture, float x, float y, float size) {
            const std::array<GeometryVertex, 4> vertices{{
                {0.0f, 0.0f, kWhite, 1.0f, 0.0f},
                {size, 0.0f, kWhite, 0.0f, 0.0f},
                {0.0f, size, kWhite, 1.0f, 1.0f},
                {size, size, kWhite, 0.0f, 1.0f},
            }};
            renderer.DrawGeometry(&texture, {vertices.data(), vertices.size()}, {kQuadIndices.data(), kQuadIndices.size()}, x, y);
        }

        void DrawChecker(IRenderer& renderer, float x, float y, float size) {
            const int squares = static_cast<int>(size / kCheckerSquare);
            for (int row = 0; row < squares; ++row) {
                for (int column = 0; column < squares; ++column) {
                    const bool dark = ((row + column) % 2) == 0;
                    renderer.DrawFilledRect(x + static_cast<float>(column) * kCheckerSquare, y + static_cast<float>(row) * kCheckerSquare, kCheckerSquare, kCheckerSquare,
                                            dark ? kCheckerDark : kCheckerLight);
                }
            }
        }

        /// The clip is in output pixels, so a rect in scene units goes through the view transform first
        void PushSceneClip(IRenderer& renderer, float x, float y, float w, float h) {
            renderer.PushClipRect(renderer.GetViewTransform().ToOutput(x, y, w, h));
        }

    }  // namespace

    void RenderProbeComponent::OnAttached() {
        // Subscribed after the renderer's own handler, so it runs before the renderer destroys its device
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &RenderProbeComponent::OnApplicationCleanUp>(this);
    }

    void RenderProbeComponent::OnApplicationCleanUp() {
        if (const auto& renderer = CoreManager::GetRenderer()) {
            renderer->WaitIdle();
        }
        _gradient = {};
    }

    void RenderProbeComponent::OnDraw() {
        const auto& rendererPtr = CoreManager::GetRenderer();
        if (!rendererPtr) {
            return;
        }
        IRenderer& renderer = *rendererPtr;
        if (!_gradient) {
            _gradient = MakeGradient(renderer);
        }

        float originX = 0.0f;
        float originY = 0.0f;
        if (auto transform = GetNode().GetComponent<TransformComponent>()) {
            originX = transform->_x;
            originY = transform->_y;
        }
        const auto cellX = [originX](int index) { return originX + static_cast<float>(index) * (kCell + kGap); };

        // Cells 0 to 3, and 5 and 6, sit on a checkerboard, so transparency is visible
        for (const int cell : {0, 1, 2, 3, 5, 6}) {
            DrawChecker(renderer, cellX(cell), originY, kCell);
        }

        if (_gradient) {
            renderer.DrawTexture(*_gradient, nullptr, cellX(0), originY, kCell, kCell);
            renderer.DrawTexture(*_gradient, nullptr, cellX(1), originY, kCell, kCell, kTintRed);
            renderer.DrawTexture(*_gradient, nullptr, cellX(2), originY, kCell, kCell, kHalfAlpha);
        }

        renderer.DrawFilledRect(cellX(3), originY, kCell * 0.6f, kCell * 0.6f, kGlassRed);
        renderer.DrawFilledRect(cellX(3) + kCell * 0.4f, originY + kCell * 0.4f, kCell * 0.6f, kCell * 0.6f, kGlassBlue);

        // Cell 4: both squares overflow their clip, which must cut them to the cell and to the inner square
        PushSceneClip(renderer, cellX(4), originY, kCell, kCell);
        renderer.DrawFilledRect(cellX(4) - kClipOverflow, originY - kClipOverflow, kCell + 2.0f * kClipOverflow, kCell + 2.0f * kClipOverflow, kClipOuter);
        PushSceneClip(renderer, cellX(4) + kInnerClipInset, originY + kInnerClipInset, kCell - 2.0f * kInnerClipInset, kCell - 2.0f * kInnerClipInset);
        renderer.DrawFilledRect(cellX(4) - kClipOverflow, originY - kClipOverflow, kCell + 2.0f * kClipOverflow, kCell + 2.0f * kClipOverflow, kClipInner);
        if (_gradient) {
            renderer.DrawTexture(*_gradient, nullptr, cellX(4) + kInnerClipInset, originY + kInnerClipInset, kCell, kCell);
        }
        renderer.PopClipRect();
        renderer.PopClipRect();

        // Cells 5 and 6: DrawGeometry(), untextured with a color per corner, and textured, mirrored and moved by its translation
        DrawColorQuad(renderer, cellX(5), originY, kCell);
        if (_gradient) {
            DrawMirroredTexture(renderer, *_gradient, cellX(6), originY, kCell);
        }
    }

}  // namespace BECore
