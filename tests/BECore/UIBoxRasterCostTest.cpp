#include <BECore/UI/UIElement.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/UIBoxRasterCost.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreSDL/SDLRendererBackend.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kWindowConfig = "config/SDLWindowConfig.xml";
        constexpr float kScreenWidth = 1920.0f;  // the benchmark screen is this many output pixels at every scale
        constexpr float kScreenHeight = 1080.0f;
        constexpr int kWarmUpFrames = 5;
        constexpr int kTimedFrames = 30;
        constexpr double kMillisecondsPerSecond = 1000.0;

        GeometryVertex Vertex(float x, float y, unsigned char alpha) {
            GeometryVertex vertex;
            vertex.x = x;
            vertex.y = y;
            vertex.color = Color{255, 255, 255, alpha};
            return vertex;
        }

        /// One box of the benchmark screen: the rect is in output pixels at scale 1, the skin in UI units
        struct BoxSpec {
            Rect rect;
            UIBoxStyle style;
            Color fill;
            Color gradientEnd;
        };

        /// The parts of a box a benchmark case keeps; a part that is off is not in the style (a fill that is off has no alpha)
        struct Parts {
            bool shadow = false;
            bool glow = false;
            bool fill = false;
            bool gradient = false;
            bool border = false;
            bool highlight = false;
        };

        constexpr Parts kAllParts{true, true, true, true, true, true};

        struct PartCase {
            const char* name;
            Parts parts;
        };

        constexpr PartCase kPartCases[] = {
            {"shadow", {.shadow = true}}, {"glow", {.glow = true}},           {"fill_flat", {.fill = true}}, {"fill_gradient", {.fill = true, .gradient = true}},
            {"border", {.border = true}}, {"highlight", {.highlight = true}},
        };

        const Color kFillStart{200, 60, 60, 255};
        const Color kFillEnd{40, 60, 200, 255};
        const Color kBorderColor{255, 255, 255, 255};
        const Color kHighlightColor{255, 255, 255, 90};
        const Color kBackground{10, 20, 30, 255};

        UIBoxStyle Skin(float radius, float borderWidth, float shadowBlur, float glowBlur) {
            UIBoxStyle style;
            style.cornerRadius = UICorners::Uniform(radius);
            style.borderWidth = borderWidth;
            style.borderColor = kBorderColor;
            style.gradientDirection = UIGradientDirection::Vertical;
            style.gradientColor = kFillEnd;
            style.shadow = {0.0f, shadowBlur * 0.5f, shadowBlur, 0.0f, Color{0, 0, 0, 200}};
            style.glow = {0.0f, 0.0f, glowBlur, 0.0f, Color{255, 215, 0, 80}};
            style.highlight = kHighlightColor;
            return style;
        }

        /// A skinned screen of a game in 1920x1080: a big panel with two rows of buttons on it, three mid panels, seven small tiles
        Vector<BoxSpec> BenchmarkScreen() {
            Vector<BoxSpec> boxes;
            const auto add = [&boxes](const Rect& rect, const UIBoxStyle& style) { boxes.push_back({rect, style, kFillStart, kFillEnd}); };

            add({60.0f, 60.0f, 1200.0f, 800.0f}, Skin(24.0f, 4.0f, 32.0f, 16.0f));
            for (int i = 0; i < 3; ++i) {
                add({1320.0f, 60.0f + 340.0f * static_cast<float>(i), 540.0f, 300.0f}, Skin(16.0f, 3.0f, 20.0f, 10.0f));
            }
            for (int row = 0; row < 2; ++row) {
                for (int column = 0; column < 6; ++column) {
                    add({96.0f + 188.0f * static_cast<float>(column), 680.0f + 90.0f * static_cast<float>(row), 176.0f, 56.0f}, Skin(12.0f, 2.0f, 12.0f, 8.0f));
                }
            }
            for (int i = 0; i < 7; ++i) {
                add({60.0f + 100.0f * static_cast<float>(i), 940.0f, 72.0f, 72.0f}, Skin(10.0f, 2.0f, 8.0f, 6.0f));
            }
            return boxes;
        }

        /// The elements that draw @p boxes at @p scale: the rects shrink by it, so the output is 1920x1080 pixels, while the skin stays in UI units as a game's does
        Vector<IntrusivePtr<UIElement>> MakeElements(const Vector<BoxSpec>& boxes, float scale, const Parts& parts) {
            Vector<IntrusivePtr<UIElement>> elements;
            for (const BoxSpec& spec : boxes) {
                auto element = New<UIElement>();
                element->rect = {spec.rect.x / scale, spec.rect.y / scale, spec.rect.w / scale, spec.rect.h / scale};
                element->box = spec.style;
                element->color = parts.fill ? spec.fill : Color{0, 0, 0, 0};
                element->box.gradientColor = parts.fill ? spec.gradientEnd : Color{0, 0, 0, 0};
                element->box.gradientDirection = parts.gradient ? spec.style.gradientDirection : UIGradientDirection::None;
                if (!parts.shadow) {
                    element->box.shadow = {};
                }
                if (!parts.glow) {
                    element->box.glow = {};
                }
                if (!parts.border) {
                    element->box.borderWidth = 0.0f;
                }
                if (!parts.highlight) {
                    element->box.highlight = Color{0, 0, 0, 0};
                }
                elements.push_back(std::move(element));
            }
            return elements;
        }

        /// A recorder that asks for what the software renderer asks for
        class ReducedRecorder : public RecordingRenderer {
        public:
            [[nodiscard]] GeometryDetail GetGeometryDetail() const override {
                return GeometryDetail::Reduced;
            }
        };

        /// The cost of the geometry the elements draw at @p scale, counted on the recorded output pixels
        SoftwareRasterCost MeasureElements(const Vector<IntrusivePtr<UIElement>>& elements, float scale, GeometryDetail detail) {
            SoftwareRasterCost total;
            const auto measure = [&](RecordingRenderer& recorder) {
                recorder.width = kScreenWidth;
                recorder.height = kScreenHeight;
                recorder.SetViewTransform({scale, 0.0f, 0.0f});
                for (const auto& element : elements) {
                    element->DrawSelf(recorder);
                }
                const Rect output{0.0f, 0.0f, kScreenWidth, kScreenHeight};
                for (const DrawCall& call : recorder.calls) {
                    if (call.kind == DrawKind::Geometry) {
                        total += MeasureRasterCost(call.vertices, call.indices, 1.0f, output);
                    }
                }
            };
            if (detail == GeometryDetail::Reduced) {
                ReducedRecorder recorder;
                measure(recorder);
            } else {
                RecordingRenderer recorder;
                measure(recorder);
            }
            return total;
        }

        /// A headless window with the SDL software renderer on it, as UIBoxCaptureTest has; a machine without the dummy video driver skips the case
        class UIBoxRasterCostTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                window = New<SDLMainWindow>();
                window->SetHeadless(true);
                if (!window->Initialize(kWindowConfig)) {
                    window.Reset();
                    GTEST_SKIP() << "no headless window: " << SDL_GetError();
                }
                renderer = New<SDLRendererBackend>();
                if (!renderer->Initialize(*window)) {
                    renderer.Reset();
                    GTEST_SKIP() << "no software renderer: " << SDL_GetError();
                }
            }

            void TearDown() override {
                if (renderer) {
                    renderer->Destroy();
                }
                if (window) {
                    window->Destroy();
                }
            }

            /// The median over kTimedFrames of the Present() of a frame that was cleared and drawn by @p draw, in milliseconds
            template <typename Draw>
            double MedianPresentMs(Draw&& draw) {
                Vector<double> samples;
                for (int frame = 0; frame < kWarmUpFrames + kTimedFrames; ++frame) {
                    renderer->Clear(kBackground);
                    draw();
                    const auto start = std::chrono::steady_clock::now();
                    renderer->Present();
                    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    if (frame >= kWarmUpFrames) {
                        samples.push_back(seconds * kMillisecondsPerSecond);
                    }
                }
                std::sort(samples.begin(), samples.end());
                return samples[samples.size() / 2];
            }

            /// Prints the line of one case: what the rasterizer is asked for (counted at the same scale on the recorder) and what the software present costs
            void PrintCase(const char* part, float scale, const Vector<BoxSpec>& boxes, const Parts& parts) {
                const Vector<IntrusivePtr<UIElement>> elements = MakeElements(boxes, scale, parts);
                const SoftwareRasterCost cost = MeasureElements(elements, scale, renderer->GetGeometryDetail());
                const double presentMs = MedianPresentMs([&] {
                    renderer->SetViewTransform({scale, 0.0f, 0.0f});
                    for (const auto& element : elements) {
                        element->DrawSelf(*renderer);
                    }
                    renderer->SetViewTransform({});
                });
                std::printf("UIBoxRasterCost part=%s scale=%d triangles=%zu bbox_px=%.0f blended_px=%.0f present_ms=%.3f\n", part, static_cast<int>(scale), cost.triangles, cost.bboxPixels,
                            cost.blendedPixels, presentMs);
            }

            IntrusivePtr<SDLMainWindow> window;
            IntrusivePtr<SDLRendererBackend> renderer;
        };

        constexpr float kScales[] = {1.0f, 2.0f};

    }  // namespace

    TEST(UIBoxRasterCostHelperTest, ATriangleCostsItsBoundingRectangleAndIsBlendedByAnyVertexAlpha) {
        const Vector<GeometryVertex> opaque = {Vertex(0.0f, 0.0f, 255), Vertex(10.0f, 0.0f, 255), Vertex(0.0f, 10.0f, 255)};
        const Vector<GeometryVertex> oneSoft = {Vertex(0.0f, 0.0f, 255), Vertex(10.0f, 0.0f, 255), Vertex(0.0f, 10.0f, 254)};
        const Vector<int> indices = {0, 1, 2};

        const SoftwareRasterCost flat = MeasureRasterCost(opaque, indices);
        EXPECT_EQ(flat.triangles, 1u);
        EXPECT_DOUBLE_EQ(flat.bboxPixels, 100.0);
        EXPECT_DOUBLE_EQ(flat.blendedPixels, 0.0);

        const SoftwareRasterCost soft = MeasureRasterCost(oneSoft, indices);
        EXPECT_DOUBLE_EQ(soft.bboxPixels, 100.0);
        EXPECT_DOUBLE_EQ(soft.blendedPixels, 100.0);
    }

    TEST(UIBoxRasterCostHelperTest, AStripOfTwoTrianglesPaysTheRectangleTwiceAndAFractionRoundsOut) {
        // A quad of 10 x 4 units at (0.5, 0.5) at 2 pixels per unit spans 1..21 by 1..9 output pixels: two triangles of 20 x 8 each
        const Vector<GeometryVertex> quad = {Vertex(0.5f, 0.5f, 0), Vertex(10.5f, 0.5f, 0), Vertex(10.5f, 4.5f, 0), Vertex(0.5f, 4.5f, 0)};
        const Vector<int> indices = {0, 1, 2, 0, 2, 3};

        const SoftwareRasterCost cost = MeasureRasterCost(quad, indices, 2.0f);
        EXPECT_EQ(cost.triangles, 2u);
        EXPECT_DOUBLE_EQ(cost.bboxPixels, 2.0 * 20.0 * 8.0);
        EXPECT_DOUBLE_EQ(cost.blendedPixels, cost.bboxPixels);
    }

    TEST(UIBoxRasterCostHelperTest, TheRectangleIsCutToTheOutputAndATriangleOutsideCostsNothing) {
        const Vector<GeometryVertex> vertices = {Vertex(-10.0f, -10.0f, 255), Vertex(10.0f, -10.0f, 255), Vertex(-10.0f, 10.0f, 255),
                                                 Vertex(50.0f, 50.0f, 255),   Vertex(60.0f, 50.0f, 255),  Vertex(50.0f, 60.0f, 255)};
        const Vector<int> indices = {0, 1, 2, 3, 4, 5};

        const SoftwareRasterCost cost = MeasureRasterCost(vertices, indices, 1.0f, Rect{0.0f, 0.0f, 20.0f, 20.0f});
        EXPECT_EQ(cost.triangles, 2u);
        EXPECT_DOUBLE_EQ(cost.bboxPixels, 10.0 * 10.0);
    }

    TEST(UIBoxRasterCostHelperTest, TheBenchmarkScreenHoldsTwentyThreeBoxesInsideTheView) {
        const Vector<BoxSpec> boxes = BenchmarkScreen();
        EXPECT_EQ(boxes.size(), 23u);
        for (const BoxSpec& spec : boxes) {
            EXPECT_GE(spec.rect.x, 0.0f);
            EXPECT_GE(spec.rect.y, 0.0f);
            EXPECT_LE(spec.rect.x + spec.rect.w, kScreenWidth);
            EXPECT_LE(spec.rect.y + spec.rect.h, kScreenHeight);
        }
    }

    // The benchmarks print one line per case; `.claude/rules/ui-shapes.md` ("Cost on the software renderer") holds the table and `perf-project.md` the matching regexes. Run them in
    // macos-release: `BagiEngineTests --gtest_also_run_disabled_tests --gtest_filter='UIBoxRasterCost*'` from the build folder.

    TEST_F(UIBoxRasterCostTest, DISABLED_Parts) {
        const Vector<BoxSpec> boxes = BenchmarkScreen();
        for (const float scale : kScales) {
            for (const PartCase& partCase : kPartCases) {
                PrintCase(partCase.name, scale, boxes, partCase.parts);
            }
        }
    }

    TEST_F(UIBoxRasterCostTest, DISABLED_Box) {
        // The big panel alone with every part of its skin
        const Vector<BoxSpec> panel = {BenchmarkScreen().front()};
        for (const float scale : kScales) {
            PrintCase("box", scale, panel, kAllParts);
        }
    }

    TEST_F(UIBoxRasterCostTest, DISABLED_Screen) {
        const Vector<BoxSpec> boxes = BenchmarkScreen();
        for (const float scale : kScales) {
            PrintCase("screen", scale, boxes, kAllParts);
        }
    }

    TEST_F(UIBoxRasterCostTest, DISABLED_TriangleFixedCost) {
        // The same area of 400 x 400 pixels as 5000 tiny blended triangles and as two big ones: the difference is what a triangle costs on its own
        constexpr int kCells = 50;
        constexpr float kCellSize = 8.0f;
        constexpr unsigned char kAlpha = 200;
        const auto vertexAt = [](float x, float y) { return Vertex(x, y, kAlpha); };

        Vector<GeometryVertex> tinyVertices;
        Vector<int> tinyIndices;
        for (int row = 0; row < kCells; ++row) {
            for (int column = 0; column < kCells; ++column) {
                const float x = static_cast<float>(column) * kCellSize;
                const float y = static_cast<float>(row) * kCellSize;
                const int first = static_cast<int>(tinyVertices.size());
                tinyVertices.push_back(vertexAt(x, y));
                tinyVertices.push_back(vertexAt(x + kCellSize, y));
                tinyVertices.push_back(vertexAt(x + kCellSize, y + kCellSize));
                tinyVertices.push_back(vertexAt(x, y + kCellSize));
                for (const int offset : {0, 1, 2, 0, 2, 3}) {
                    tinyIndices.push_back(first + offset);
                }
            }
        }
        constexpr float kSide = kCells * kCellSize;
        const Vector<GeometryVertex> bigVertices = {vertexAt(0.0f, 0.0f), vertexAt(kSide, 0.0f), vertexAt(kSide, kSide), vertexAt(0.0f, kSide)};
        const Vector<int> bigIndices = {0, 1, 2, 0, 2, 3};

        const auto run = [&](const char* part, const Vector<GeometryVertex>& vertices, const Vector<int>& indices) {
            const SoftwareRasterCost cost = MeasureRasterCost(vertices, indices);
            const double presentMs = MedianPresentMs([&] { renderer->DrawGeometry(nullptr, vertices, indices, 0.0f, 0.0f); });
            std::printf("UIBoxRasterCost part=%s scale=1 triangles=%zu bbox_px=%.0f blended_px=%.0f present_ms=%.3f\n", part, cost.triangles, cost.bboxPixels, cost.blendedPixels, presentMs);
        };
        run("tiny_triangles", tinyVertices, tinyIndices);
        run("big_triangles", bigVertices, bigIndices);
    }

}  // namespace BECore::Tests
