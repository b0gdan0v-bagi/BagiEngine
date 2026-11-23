#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIBoxTexture.h>
#include <BECore/UI/UIBoxTextureCache.h>
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
        constexpr size_t kQuadTriangles = 2;

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
                    } else if (call.kind == DrawKind::Texture) {
                        // A blit of a box texture is a quad that the software renderer blends over its destination
                        const double pixels = static_cast<double>(call.rect.w) * static_cast<double>(call.rect.h);
                        total += SoftwareRasterCost{kQuadTriangles, pixels, pixels};
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
                CoreManager::GetUIBoxTextureCache().Clear();  // the textures are of this renderer
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

            /// Splits the triangles of one part at scale 1 into its core (every vertex at the strongest alpha of its call: the plateau a fill or a blur hides or shows whole) and the
            /// rest (the soft rim), prints a line for each and draws each alone
            void PrintCoreAndRim(const char* part, const Vector<BoxSpec>& boxes, const Parts& parts) {
                ReducedRecorder recorder;
                recorder.width = kScreenWidth;
                recorder.height = kScreenHeight;
                for (const auto& element : MakeElements(boxes, 1.0f, parts)) {
                    element->DrawSelf(recorder);
                }
                Vector<GeometryVertex> vertices[2];
                Vector<int> indices[2];  // [0] the core, [1] the rim
                for (const DrawCall& call : recorder.calls) {
                    unsigned char strongest = 0;
                    for (const GeometryVertex& vertex : call.vertices) {
                        strongest = std::max(strongest, vertex.color.a);
                    }
                    for (size_t i = 0; i + 3 <= call.indices.size(); i += 3) {
                        const GeometryVertex* corners[3] = {&call.vertices[static_cast<size_t>(call.indices[i])], &call.vertices[static_cast<size_t>(call.indices[i + 1])],
                                                            &call.vertices[static_cast<size_t>(call.indices[i + 2])]};
                        const size_t kind = corners[0]->color.a == strongest && corners[1]->color.a == strongest && corners[2]->color.a == strongest ? 0 : 1;
                        for (const GeometryVertex* corner : corners) {
                            indices[kind].push_back(static_cast<int>(vertices[kind].size()));
                            vertices[kind].push_back(*corner);
                        }
                    }
                }
                const Rect output{0.0f, 0.0f, kScreenWidth, kScreenHeight};
                const char* const names[2] = {"core", "rim"};
                for (size_t kind = 0; kind < 2; ++kind) {
                    const SoftwareRasterCost cost = MeasureRasterCost(vertices[kind], indices[kind], 1.0f, output);
                    const double presentMs = MedianPresentMs([&] { renderer->DrawGeometry(nullptr, vertices[kind], indices[kind], 0.0f, 0.0f); });
                    std::printf("UIBoxRasterCost part=%s_%s scale=1 triangles=%zu bbox_px=%.0f blended_px=%.0f present_ms=%.3f\n", part, names[kind], cost.triangles, cost.bboxPixels,
                                cost.blendedPixels, presentMs);
                }
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

    TEST_F(UIBoxRasterCostTest, DISABLED_CoresAndRims) {
        const Vector<BoxSpec> boxes = BenchmarkScreen();
        for (const PartCase& partCase : kPartCases) {
            PrintCoreAndRim(partCase.name, boxes, partCase.parts);
        }
    }

    // What a cache miss costs: the textures of the boxes of the screen made one after the other (the first frame of a screen, or a hover that changes a colour)
    TEST_F(UIBoxRasterCostTest, DISABLED_TextureMake) {
        const Vector<BoxSpec> boxes = BenchmarkScreen();
        for (const float scale : kScales) {
            double totalMs = 0.0;
            double largestMs = 0.0;
            for (const BoxSpec& box : boxes) {
                const UIBoxLook look = UIBoxTexture::MakeLook(box.style, box.fill, box.gradientEnd, scale, box.rect.w / scale, box.rect.h / scale);
                const auto layout = UIBoxTexture::MakeLayout(look, static_cast<int>(box.rect.w), static_cast<int>(box.rect.h));
                ASSERT_TRUE(layout.has_value());
                const auto start = std::chrono::steady_clock::now();
                const Vector<uint8_t> pixels = UIBoxTexture::Render(*layout);
                const double ms = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() * kMillisecondsPerSecond;
                EXPECT_FALSE(pixels.empty());
                totalMs += ms;
                largestMs = std::max(largestMs, ms);
            }
            std::printf("UIBoxRasterCost part=texture_make scale=%d total_ms=%.3f largest_ms=%.3f\n", static_cast<int>(scale), totalMs, largestMs);
        }
    }

    TEST_F(UIBoxRasterCostTest, DISABLED_TextureFloor) {
        // The floor of a box drawn as a texture: one blended, stretched blit of a translucent texture over the area the box and its shadow cover (the rect grown by twice the
        // blur of its shadow on every side), which is what a cached box costs at best whatever the nine-slice does with the edges
        constexpr uint32_t kTextureSide = 16;
        constexpr unsigned char kTexelAlpha = 200;
        const Vector<uint8_t> texels(static_cast<size_t>(kTextureSide) * kTextureSide * 4, kTexelAlpha);
        const IntrusivePtr<ITexture> texture = renderer->CreateTexture(texels.data(), kTextureSide, kTextureSide);
        ASSERT_TRUE(texture);
        const Vector<BoxSpec> boxes = BenchmarkScreen();
        double pixels = 0.0;
        for (const BoxSpec& spec : boxes) {
            const float grow = spec.style.shadow.blur * 2.0f;
            pixels += static_cast<double>(spec.rect.w + 2.0f * grow) * static_cast<double>(spec.rect.h + 2.0f * grow);
        }
        const double presentMs = MedianPresentMs([&] {
            for (const BoxSpec& spec : boxes) {
                const float grow = spec.style.shadow.blur * 2.0f;
                renderer->DrawTexture(*texture, nullptr, spec.rect.x - grow, spec.rect.y - grow, spec.rect.w + 2.0f * grow, spec.rect.h + 2.0f * grow);
            }
        });
        std::printf("UIBoxRasterCost part=texture_floor scale=1 triangles=0 bbox_px=%.0f blended_px=%.0f present_ms=%.3f\n", pixels, pixels, presentMs);
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
