#include <BECore/UI/UIBoxMesh.h>
#include <EASTL/sort.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;
        constexpr float kPi = 3.14159265358979f;
        constexpr unsigned char kOpaque = 255;

        const Color kWhite{255, 255, 255, 255};
        const Color kRed{255, 0, 0, 255};
        const Color kBlue{0, 0, 255, 255};

        struct Mesh {
            eastl::vector<GeometryVertex> vertices;
            eastl::vector<int> indices;

            [[nodiscard]] size_t TriangleCount() const {
                return indices.size() / 3;
            }
        };

        struct Triangle {
            const GeometryVertex* a;
            const GeometryVertex* b;
            const GeometryVertex* c;

            /// Twice the signed area in y-down coordinates: positive is clockwise on the screen
            [[nodiscard]] float Cross() const {
                return (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
            }

            [[nodiscard]] bool IsCore() const {
                return a->color.a == kOpaque && b->color.a == kOpaque && c->color.a == kOpaque;
            }

            [[nodiscard]] bool Contains(float x, float y) const {
                const auto side = [](const GeometryVertex* p, const GeometryVertex* q, float px, float py) { return (q->x - p->x) * (py - p->y) - (px - p->x) * (q->y - p->y); };
                const float s1 = side(a, b, x, y);
                const float s2 = side(b, c, x, y);
                const float s3 = side(c, a, x, y);
                return (s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0);
            }
        };

        Triangle TriangleAt(const Mesh& mesh, size_t index) {
            return {&mesh.vertices[mesh.indices[index * 3]], &mesh.vertices[mesh.indices[index * 3 + 1]], &mesh.vertices[mesh.indices[index * 3 + 2]]};
        }

        Mesh Fill(const Rect& rect, const UICorners& corners, const Color& color, float ppu, const UIGradientSpec& gradient = {}) {
            Mesh mesh;
            UIBoxMesh::AppendFill(mesh.vertices, mesh.indices, rect, corners, color, gradient, ppu);
            return mesh;
        }

        Mesh Border(const Rect& rect, const UICorners& corners, float width, const Color& color, float ppu) {
            Mesh mesh;
            UIBoxMesh::AppendBorder(mesh.vertices, mesh.indices, rect, corners, width, color, ppu);
            return mesh;
        }

        Mesh Shadow(const Rect& rect, const UICorners& corners, const UIShadowSpec& spec, float ppu) {
            Mesh mesh;
            UIBoxMesh::AppendShadow(mesh.vertices, mesh.indices, rect, corners, spec, ppu);
            return mesh;
        }

        Mesh Highlight(const Rect& rect, const UICorners& corners, const Color& color, float ppu) {
            Mesh mesh;
            UIBoxMesh::AppendHighlight(mesh.vertices, mesh.indices, rect, corners, color, ppu);
            return mesh;
        }

        /// The area of the triangles whose three vertices are fully opaque
        float CoreArea(const Mesh& mesh) {
            float area = 0.0f;
            for (size_t i = 0; i < mesh.TriangleCount(); ++i) {
                const Triangle t = TriangleAt(mesh, i);
                if (t.IsCore()) {
                    area += t.Cross() * 0.5f;
                }
            }
            return area;
        }

        size_t CoreTriangleCount(const Mesh& mesh) {
            size_t count = 0;
            for (size_t i = 0; i < mesh.TriangleCount(); ++i) {
                count += TriangleAt(mesh, i).IsCore() ? 1 : 0;
            }
            return count;
        }

        /// Every index points at a vertex, the list is whole triangles, and no triangle runs the other way round (a zero-area one, from a collapsed corner, may)
        void ExpectWellFormed(const Mesh& mesh, const char* what) {
            ASSERT_EQ(mesh.indices.size() % 3, 0u) << what;
            for (const int index : mesh.indices) {
                ASSERT_GE(index, 0) << what;
                ASSERT_LT(static_cast<size_t>(index), mesh.vertices.size()) << what;
            }
            for (size_t i = 0; i < mesh.TriangleCount(); ++i) {
                const Triangle t = TriangleAt(mesh, i);
                const float longest = std::max({std::hypot(t.b->x - t.a->x, t.b->y - t.a->y), std::hypot(t.c->x - t.b->x, t.c->y - t.b->y), std::hypot(t.a->x - t.c->x, t.a->y - t.c->y)});
                // A sliver along a straight edge is as flat as float arithmetic can tell: allow the noise of its size
                EXPECT_GE(t.Cross(), -1e-5f * longest * longest - kTolerance) << what << ": triangle " << i << " runs the other way round";
            }
        }

        struct Extent {
            float minX = 1e30f;
            float minY = 1e30f;
            float maxX = -1e30f;
            float maxY = -1e30f;
        };

        Extent ExtentOf(const Mesh& mesh, int alpha = -1) {
            Extent extent;
            for (const GeometryVertex& v : mesh.vertices) {
                if (alpha >= 0 && v.color.a != alpha) {
                    continue;
                }
                extent.minX = std::min(extent.minX, v.x);
                extent.minY = std::min(extent.minY, v.y);
                extent.maxX = std::max(extent.maxX, v.x);
                extent.maxY = std::max(extent.maxY, v.y);
            }
            return extent;
        }

        /// The signed distance from a point to a rounded rect (negative inside)
        float RoundedRectDistance(const Rect& rect, float radius, float x, float y) {
            const float qx = std::fabs(x - (rect.x + rect.w * 0.5f)) - (rect.w * 0.5f - radius);
            const float qy = std::fabs(y - (rect.y + rect.h * 0.5f)) - (rect.h * 0.5f - radius);
            return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
        }

    }  // namespace

    TEST(UIBoxMeshTest, ZeroRadiusIsFourCornersAndTwoTrianglesPlusTheFeather) {
        const Rect rect{10, 20, 100, 40};
        const float ppu = 2.0f;
        const float feather = 1.0f / ppu;
        const Mesh mesh = Fill(rect, {}, kWhite, ppu);

        // Four core corners and four feather corners; the core is two triangles, the feather is a ring of eight
        EXPECT_EQ(mesh.vertices.size(), 8u);
        EXPECT_EQ(CoreTriangleCount(mesh), 2u);
        EXPECT_EQ(mesh.TriangleCount(), 10u);

        const Extent core = ExtentOf(mesh, kOpaque);
        EXPECT_NEAR(core.minX, rect.x + feather * 0.5f, kTolerance);
        EXPECT_NEAR(core.minY, rect.y + feather * 0.5f, kTolerance);
        EXPECT_NEAR(core.maxX, rect.x + rect.w - feather * 0.5f, kTolerance);
        EXPECT_NEAR(core.maxY, rect.y + rect.h - feather * 0.5f, kTolerance);
        EXPECT_NEAR(CoreArea(mesh), (rect.w - feather) * (rect.h - feather), 1e-2f);
    }

    TEST(UIBoxMeshTest, EveryIndexIsInsideTheVerticesAndEveryTriangleHasTheSameWinding) {
        const Rect rect{5, 7, 120, 64};
        const UICorners shapes[] = {{}, UICorners::Uniform(12), {20, 4, 0, 9}, UICorners::Uniform(1000), {0.2f, 0.2f, 0.2f, 0.2f}, UICorners::Uniform(32)};
        for (const float ppu : {0.5f, 1.0f, 3.0f}) {
            for (const UICorners& corners : shapes) {
                const UIGradientSpec gradient{UIGradientDirection::Vertical, kBlue};
                ExpectWellFormed(Fill(rect, corners, kRed, ppu, gradient), "fill");
                ExpectWellFormed(Border(rect, corners, 3.0f, kRed, ppu), "border");
                ExpectWellFormed(Border(rect, corners, 0.3f, kRed, ppu), "thin border");
                ExpectWellFormed(Border(rect, corners, 500.0f, kRed, ppu), "border of the whole shape");
                ExpectWellFormed(Shadow(rect, corners, {2, 4, 12, 3, kRed}, ppu), "shadow");
                ExpectWellFormed(Shadow(rect, corners, {0, 0, 0, 0, kRed}, ppu), "hard shadow");
                ExpectWellFormed(Shadow(rect, corners, {0, 0, 200, 0, kRed}, ppu), "shadow wider than the box");
                ExpectWellFormed(Highlight(rect, corners, kWhite, ppu), "highlight");
            }
        }
    }

    TEST(UIBoxMeshTest, FillStaysInsideTheRectGrownByTheFeather) {
        const Rect rect{-30, 12, 90, 50};
        for (const float ppu : {0.5f, 1.0f, 3.0f}) {
            const float feather = 1.0f / ppu;
            for (const UICorners& corners : {UICorners{}, UICorners::Uniform(14), UICorners{25, 0, 8, 3}}) {
                const Extent extent = ExtentOf(Fill(rect, corners, kRed, ppu));
                EXPECT_GE(extent.minX, rect.x - feather - kTolerance);
                EXPECT_GE(extent.minY, rect.y - feather - kTolerance);
                EXPECT_LE(extent.maxX, rect.x + rect.w + feather + kTolerance);
                EXPECT_LE(extent.maxY, rect.y + rect.h + feather + kTolerance);
            }
        }
    }

    TEST(UIBoxMeshTest, OpaqueAreaIsTheRectMinusTheCorners) {
        const Rect rect{0, 0, 100, 60};
        const float radius = 12.0f;
        const float ppu = 4.0f;
        const float feather = 1.0f / ppu;
        const float coreRadius = radius - feather * 0.5f;
        // The core is the rect inset by half a feather: the corners cut away a square minus a quarter disc each
        const float expected = (rect.w - feather) * (rect.h - feather) - 4.0f * (1.0f - kPi / 4.0f) * coreRadius * coreRadius;
        const float area = CoreArea(Fill(rect, UICorners::Uniform(radius), kWhite, ppu));
        EXPECT_NEAR(area, expected, expected * 0.005f);

        // One rounded corner cuts away one of the four
        const float oneCorner = CoreArea(Fill(rect, {radius, 0, 0, 0}, kWhite, ppu));
        EXPECT_NEAR(oneCorner, (rect.w - feather) * (rect.h - feather) - (1.0f - kPi / 4.0f) * coreRadius * coreRadius, expected * 0.005f);
    }

    TEST(UIBoxMeshTest, ARadiusLargerThanHalfTheSideIsClamped) {
        EXPECT_EQ(UICorners::Uniform(100).Clamped(40, 20), UICorners::Uniform(10));
        EXPECT_EQ(UICorners::Uniform(10).Clamped(40, 20), UICorners::Uniform(10));
        EXPECT_EQ(UICorners::Uniform(5).Clamped(40, 20), UICorners::Uniform(5));
        // Two radii on one side never exceed it: the left side of 20 holds 30 + 0, so every radius shrinks by 2 / 3
        const UICorners mixed = UICorners{30, 6, 0, 0}.Clamped(40, 20);
        EXPECT_NEAR(mixed.topLeft, 20.0f, kTolerance);
        EXPECT_NEAR(mixed.topRight, 4.0f, kTolerance);
        EXPECT_NEAR(mixed.topLeft + mixed.bottomLeft, 20.0f, kTolerance);
        // A negative, NaN or infinite radius is no radius
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_TRUE((UICorners{-4, nan, std::numeric_limits<float>::infinity(), 0}.Clamped(40, 20).IsZero()));
        EXPECT_TRUE(UICorners::Uniform(5).Clamped(0, 20).IsZero());

        // The mesh of an oversized radius is the mesh of the clamped one
        const Mesh big = Fill({0, 0, 40, 20}, UICorners::Uniform(100), kWhite, 2.0f);
        const Mesh clamped = Fill({0, 0, 40, 20}, UICorners::Uniform(10), kWhite, 2.0f);
        ASSERT_EQ(big.vertices.size(), clamped.vertices.size());
        for (size_t i = 0; i < big.vertices.size(); ++i) {
            EXPECT_NEAR(big.vertices[i].x, clamped.vertices[i].x, kTolerance);
            EXPECT_NEAR(big.vertices[i].y, clamped.vertices[i].y, kTolerance);
        }
        // A pill: the opaque area of the mesh is the area of a stadium
        EXPECT_NEAR(CoreArea(big), CoreArea(clamped), kTolerance);
    }

    TEST(UIBoxMeshTest, InsetShrinksEveryRadiusAndStopsAtZero) {
        EXPECT_EQ((UICorners{10, 4, 0, 1}.Inset(2)), (UICorners{8, 2, 0, 0}));
    }

    TEST(UIBoxMeshTest, PerCornerRadiiRoundTheRightCorners) {
        const Rect rect{0, 0, 80, 50};
        const float ppu = 2.0f;
        const float half = 0.25f;  // half a feather
        // Only the top-right and the bottom-left are rounded
        const Mesh mesh = Fill(rect, {0, 16, 0, 16}, kWhite, ppu);

        const auto hasCoreVertexAt = [&](float x, float y) {
            for (const GeometryVertex& v : mesh.vertices) {
                if (v.color.a == kOpaque && std::fabs(v.x - x) < kTolerance && std::fabs(v.y - y) < kTolerance) {
                    return true;
                }
            }
            return false;
        };
        EXPECT_TRUE(hasCoreVertexAt(rect.x + half, rect.y + half));                    // top-left stays square
        EXPECT_TRUE(hasCoreVertexAt(rect.x + rect.w - half, rect.y + rect.h - half));  // bottom-right stays square
        EXPECT_FALSE(hasCoreVertexAt(rect.x + rect.w - half, rect.y + half));          // top-right is cut
        EXPECT_FALSE(hasCoreVertexAt(rect.x + half, rect.y + rect.h - half));          // bottom-left is cut
        // The arc of the top-right corner starts on the top edge, 16 units before the right edge, and ends on the right edge 16 below the top
        EXPECT_TRUE(hasCoreVertexAt(rect.x + rect.w - 16.0f, rect.y + half));
        EXPECT_TRUE(hasCoreVertexAt(rect.x + rect.w - half, rect.y + 16.0f));
    }

    TEST(UIBoxMeshTest, AGradientPutsTheStartColourOnTheTopAndTheEndColourOnTheBottom) {
        const Rect rect{4, 6, 60, 40};
        const UICorners corners = UICorners::Uniform(8);

        const Mesh vertical = Fill(rect, corners, kRed, 2.0f, {UIGradientDirection::Vertical, kBlue});
        const Extent v = ExtentOf(vertical);
        float previousT = -1.0f;
        for (const GeometryVertex& vertex : vertical.vertices) {
            // The colour depends on the height only: the blue share grows with y, from none at the top to all at the bottom
            const float t = (vertex.y - v.minY) / (v.maxY - v.minY);
            if (vertex.y <= v.minY + kTolerance) {
                EXPECT_EQ(vertex.color.r, 255);
                EXPECT_EQ(vertex.color.b, 0);
            }
            if (vertex.y >= v.maxY - kTolerance) {
                EXPECT_EQ(vertex.color.r, 0);
                EXPECT_EQ(vertex.color.b, 255);
            }
            EXPECT_NEAR(static_cast<float>(vertex.color.b) / 255.0f, std::clamp((vertex.y - (rect.y + 0.25f)) / (rect.h - 0.5f), 0.0f, 1.0f), 0.01f);
            previousT = std::max(previousT, t);
        }
        EXPECT_GT(previousT, 0.99f);

        const Mesh horizontal = Fill(rect, corners, kRed, 2.0f, {UIGradientDirection::Horizontal, kBlue});
        const Extent h = ExtentOf(horizontal);
        for (const GeometryVertex& vertex : horizontal.vertices) {
            if (vertex.x <= h.minX + kTolerance) {
                EXPECT_EQ(vertex.color.b, 0);
            }
            if (vertex.x >= h.maxX - kTolerance) {
                EXPECT_EQ(vertex.color.b, 255);
            }
            EXPECT_NEAR(static_cast<float>(vertex.color.b) / 255.0f, std::clamp((vertex.x - (rect.x + 0.25f)) / (rect.w - 0.5f), 0.0f, 1.0f), 0.01f);
        }

        // The ends may differ in alpha: a fade to nothing, and the feather alpha still multiplies it
        const Mesh fade = Fill(rect, {}, kRed, 2.0f, {UIGradientDirection::Vertical, Color{255, 0, 0, 0}});
        EXPECT_GT(fade.vertices.size(), 0u);
        for (const GeometryVertex& vertex : fade.vertices) {
            if (vertex.y <= ExtentOf(fade).minY + kTolerance) {
                EXPECT_EQ(vertex.color.a, 0);  // the outer ring's alpha 0 and the start alpha 255 give 0
            }
        }
    }

    TEST(UIBoxMeshTest, TheFeatherIsOneOutputPixelWideAtAnyScale) {
        const Rect rect{10, 10, 80, 50};
        for (const float ppu : {0.5f, 1.0f, 3.0f}) {
            const float feather = 1.0f / ppu;
            // A square box: the outer ring sits one feather outside the opaque ring on every side
            const Mesh square = Fill(rect, {}, kWhite, ppu);
            EXPECT_NEAR(ExtentOf(square).maxX - ExtentOf(square, kOpaque).maxX, feather * 0.5f + feather * 0.5f - 0.0f, kTolerance);
            EXPECT_NEAR(ExtentOf(square, kOpaque).minX - ExtentOf(square).minX, feather, kTolerance);
            EXPECT_NEAR(ExtentOf(square).maxY - ExtentOf(square, kOpaque).maxY, feather, kTolerance);

            // A rounded box: every vertex of alpha 0 lies one feather from the opaque outline (its distance to the shape is half a feather outside, the core half inside)
            const Mesh round = Fill(rect, UICorners::Uniform(20), kWhite, ppu);
            for (const GeometryVertex& v : round.vertices) {
                const float distance = RoundedRectDistance(rect, 20.0f, v.x, v.y);
                EXPECT_NEAR(distance, v.color.a == kOpaque ? -feather * 0.5f : feather * 0.5f, kTolerance * 2.0f) << "ppu " << ppu;
            }
        }
    }

    TEST(UIBoxMeshTest, ABorderRingDoesNotOverlapTheFillOfTheInnerOutline) {
        const Rect rect{0, 0, 80, 50};
        const float width = 4.0f;
        const UICorners corners = UICorners::Uniform(10);
        for (const float ppu : {1.0f, 2.0f, 3.0f}) {
            const Mesh border = Border(rect, corners, width, kRed, ppu);
            const Mesh inner = Fill({rect.x + width, rect.y + width, rect.w - 2 * width, rect.h - 2 * width}, corners.Inset(width), kBlue, ppu);
            ASSERT_GT(border.vertices.size(), 0u);

            // Sample points: none is inside two triangles of the border, and none is inside both a border triangle and the core of the inner fill
            int sampled = 0;
            for (float y = -0.9f; y < rect.h + 1.0f; y += 0.37f) {
                for (float x = -0.9f; x < rect.w + 1.0f; x += 0.41f) {
                    int inBorder = 0;
                    for (size_t i = 0; i < border.TriangleCount(); ++i) {
                        inBorder += TriangleAt(border, i).Contains(x, y) ? 1 : 0;
                    }
                    int inInnerCore = 0;
                    for (size_t i = 0; i < inner.TriangleCount(); ++i) {
                        const Triangle t = TriangleAt(inner, i);
                        inInnerCore += t.IsCore() && t.Contains(x, y) ? 1 : 0;
                    }
                    EXPECT_LE(inBorder, 1) << "ppu " << ppu << " at " << x << ", " << y;
                    EXPECT_FALSE(inBorder > 0 && inInnerCore > 0) << "ppu " << ppu << " at " << x << ", " << y;
                    ++sampled;
                }
            }
            EXPECT_GT(sampled, 1000);
        }
    }

    TEST(UIBoxMeshTest, ABorderOpaqueAreaIsTheRingBetweenTheOutlines) {
        const Rect rect{0, 0, 100, 60};
        const float width = 5.0f;
        const float radius = 12.0f;
        const float ppu = 4.0f;
        const float feather = 1.0f / ppu;
        const auto roundedArea = [](float w, float h, float r) { return w * h - 4.0f * (1.0f - kPi / 4.0f) * r * r; };
        // Opaque from half a feather inside the outer edge to half a feather outside the inner edge
        const float expected =
            roundedArea(rect.w - feather, rect.h - feather, radius - feather * 0.5f) - roundedArea(rect.w - 2 * width + feather, rect.h - 2 * width + feather, radius - width + feather * 0.5f);
        const float area = CoreArea(Border(rect, UICorners::Uniform(radius), width, kWhite, ppu));
        EXPECT_NEAR(area, expected, expected * 0.01f);
    }

    TEST(UIBoxMeshTest, AThinBorderIsAFainterLineOfTheSameWeight) {
        const Rect rect{0, 0, 50, 50};
        const Mesh thin = Border(rect, {}, 0.25f, kWhite, 1.0f);  // a quarter of a pixel
        ASSERT_GT(thin.vertices.size(), 0u);
        unsigned char peak = 0;
        for (const GeometryVertex& v : thin.vertices) {
            peak = std::max(peak, v.color.a);
        }
        EXPECT_NEAR(static_cast<float>(peak), 255.0f * 0.25f, 2.0f);
        const Extent extent = ExtentOf(thin);
        EXPECT_GE(extent.minX, rect.x - 1.0f - kTolerance);
        EXPECT_LE(extent.maxX, rect.x + rect.w + 1.0f + kTolerance);
    }

    TEST(UIBoxMeshTest, AShadowFallsMonotonicallyOutwardAndIsZeroAtTheOuterRing) {
        const Rect rect{20, 20, 100, 60};
        const float radius = 10.0f;
        const float blur = 12.0f;
        const Color color{10, 20, 30, 200};
        const Mesh mesh = Shadow(rect, UICorners::Uniform(radius), {0, 0, blur, 0, color}, 2.0f);
        ASSERT_GT(mesh.vertices.size(), 0u);

        eastl::vector<eastl::pair<float, unsigned char>> samples;
        for (const GeometryVertex& v : mesh.vertices) {
            samples.push_back({RoundedRectDistance(rect, radius, v.x, v.y), v.color.a});
            EXPECT_EQ(v.color.r, color.r);
            EXPECT_EQ(v.color.g, color.g);
        }
        eastl::sort(samples.begin(), samples.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t i = 1; i < samples.size(); ++i) {
            if (samples[i].first - samples[i - 1].first > 1e-2f) {
                EXPECT_LE(samples[i].second, samples[i - 1].second) << "alpha rises between distance " << samples[i - 1].first << " and " << samples[i].first;
            }
        }
        // The outer ring is `blur` beyond the edge and transparent, the inner one `blur` inside and the colour's own alpha; the edge itself is half way
        EXPECT_NEAR(samples.back().first, blur, 1e-2f);
        EXPECT_EQ(samples.back().second, 0);
        EXPECT_NEAR(samples.front().first, -blur, 1e-2f);
        EXPECT_EQ(samples.front().second, color.a);
        for (const auto& [distance, alpha] : samples) {
            if (std::fabs(distance) < 1e-2f) {
                EXPECT_NEAR(static_cast<float>(alpha), 100.0f, 2.0f);
            }
        }
        ExpectWellFormed(mesh, "shadow");
    }

    TEST(UIBoxMeshTest, AShadowFollowsTheOffsetAndTheSpread) {
        const Rect rect{50, 50, 60, 40};
        const Mesh plain = Shadow(rect, {}, {0, 0, 8, 0, kBlue}, 1.0f);
        const Mesh moved = Shadow(rect, {}, {7, -3, 8, 0, kBlue}, 1.0f);
        const Mesh grown = Shadow(rect, {}, {0, 0, 8, 5, kBlue}, 1.0f);
        const Extent a = ExtentOf(plain);
        EXPECT_NEAR(a.minX, rect.x - 8.0f, kTolerance);
        EXPECT_NEAR(a.maxX, rect.x + rect.w + 8.0f, kTolerance);
        EXPECT_NEAR(ExtentOf(moved).minX, a.minX + 7.0f, kTolerance);
        EXPECT_NEAR(ExtentOf(moved).minY, a.minY - 3.0f, kTolerance);
        EXPECT_NEAR(ExtentOf(grown).minX, a.minX - 5.0f, kTolerance);
        EXPECT_NEAR(ExtentOf(grown).maxY, a.maxY + 5.0f, kTolerance);

        // A negative spread larger than the box leaves nothing
        EXPECT_EQ(Shadow(rect, {}, {0, 0, 4, -40, kBlue}, 1.0f).vertices.size(), 0u);
    }

    TEST(UIBoxMeshTest, AShadowWithoutBlurIsAHardEdgeAndAGlowIsTheSameCall) {
        const Rect rect{0, 0, 40, 30};
        const Mesh hard = Shadow(rect, UICorners::Uniform(6), {0, 0, 0, 2, kRed}, 2.0f);
        const Mesh fill = Fill({-2, -2, 44, 34}, UICorners::Uniform(8), kRed, 2.0f);
        ASSERT_EQ(hard.vertices.size(), fill.vertices.size());
        for (size_t i = 0; i < hard.vertices.size(); ++i) {
            EXPECT_NEAR(hard.vertices[i].x, fill.vertices[i].x, kTolerance);
            EXPECT_NEAR(hard.vertices[i].y, fill.vertices[i].y, kTolerance);
            EXPECT_EQ(hard.vertices[i].color.a, fill.vertices[i].color.a);
        }
    }

    TEST(UIBoxMeshTest, AShadowSmallerThanItsBlurNeverReachesFullAlpha) {
        const Mesh mesh = Shadow({0, 0, 6, 6}, {}, {0, 0, 20, 0, kRed}, 1.0f);
        ASSERT_GT(mesh.vertices.size(), 0u);
        unsigned char peak = 0;
        for (const GeometryVertex& v : mesh.vertices) {
            peak = std::max(peak, v.color.a);
        }
        EXPECT_GT(peak, 0);
        EXPECT_LT(peak, 255);
        ExpectWellFormed(mesh, "small shadow");
    }

    TEST(UIBoxMeshTest, TheSegmentCountGrowsWithTheRadiusInPixelsAndStaysInItsLimits) {
        EXPECT_EQ(UIBoxMesh::CornerSegments(0.0f), 0);
        EXPECT_EQ(UIBoxMesh::CornerSegments(-3.0f), 0);
        EXPECT_EQ(UIBoxMesh::CornerSegments(std::numeric_limits<float>::quiet_NaN()), 0);
        int previous = 0;
        for (float radius = 0.01f; radius < 5000.0f; radius *= 1.3f) {
            const int segments = UIBoxMesh::CornerSegments(radius);
            EXPECT_GE(segments, UIBoxMesh::kMinCornerSegments) << radius;
            EXPECT_LE(segments, UIBoxMesh::kMaxCornerSegments) << radius;
            EXPECT_GE(segments, previous) << radius;
            previous = segments;
        }
        EXPECT_EQ(UIBoxMesh::CornerSegments(0.01f), UIBoxMesh::kMinCornerSegments);
        EXPECT_EQ(UIBoxMesh::CornerSegments(5000.0f), UIBoxMesh::kMaxCornerSegments);
        EXPECT_GT(UIBoxMesh::CornerSegments(64.0f), UIBoxMesh::CornerSegments(8.0f));

        // The mesh follows it: a bigger radius in pixels is more vertices; the same radius at a bigger scale too
        const Rect rect{0, 0, 200, 200};
        EXPECT_GT(Fill(rect, UICorners::Uniform(40), kRed, 1.0f).vertices.size(), Fill(rect, UICorners::Uniform(4), kRed, 1.0f).vertices.size());
        EXPECT_GT(Fill(rect, UICorners::Uniform(10), kRed, 4.0f).vertices.size(), Fill(rect, UICorners::Uniform(10), kRed, 0.5f).vertices.size());
    }

    TEST(UIBoxMeshTest, TheCornerPolygonStaysWithinTheToleranceOfTheArc) {
        const Rect rect{0, 0, 200, 200};
        const float ppu = 2.0f;
        const float radius = 30.0f;
        const float feather = 1.0f / ppu;
        const Mesh mesh = Fill(rect, UICorners::Uniform(radius), kWhite, ppu);
        // The opaque ring is the second one, in outline order: the middle of each of its edges is at most the sagitta inside the arc
        const size_t ringSize = mesh.vertices.size() / 2;
        for (size_t i = 0; i < ringSize; ++i) {
            const GeometryVertex& from = mesh.vertices[ringSize + i];
            const GeometryVertex& to = mesh.vertices[ringSize + (i + 1) % ringSize];
            const float distance = RoundedRectDistance(rect, radius, (from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f);
            EXPECT_GE(distance, -feather * 0.5f - UIBoxMesh::kCornerSagittaPixels / ppu - kTolerance);
            EXPECT_LE(distance, -feather * 0.5f + kTolerance);
        }
    }

    TEST(UIBoxMeshTest, AHighlightIsAThinLineInsideTheTopEdgeThatFadesAroundTheTopCorners) {
        const Rect rect{10, 10, 120, 40};
        const float ppu = 2.0f;
        const float feather = 1.0f / ppu;
        const float radius = 12.0f;
        const Mesh mesh = Highlight(rect, UICorners::Uniform(radius), kWhite, ppu);
        ASSERT_GT(mesh.vertices.size(), 0u);
        ExpectWellFormed(mesh, "highlight");

        unsigned char peak = 0;
        for (const GeometryVertex& v : mesh.vertices) {
            // Inside the shape, never outside it
            EXPECT_LE(RoundedRectDistance(rect, radius, v.x, v.y), kTolerance);
            peak = std::max(peak, v.color.a);
            // On the straight top the line is within its width of the edge
            if (v.x > rect.x + radius && v.x < rect.x + rect.w - radius) {
                EXPECT_LE(v.y, rect.y + UIBoxMesh::kHighlightWidth + feather + kTolerance);
            }
            // Only the top: no vertex below the arcs' half way
            EXPECT_LE(v.y, rect.y + radius);
        }
        EXPECT_EQ(peak, 255);
        const Extent extent = ExtentOf(mesh);
        // The far ends of the half arcs are transparent
        for (const GeometryVertex& v : mesh.vertices) {
            if (v.x <= extent.minX + kTolerance || v.x >= extent.maxX - kTolerance) {
                EXPECT_EQ(v.color.a, 0);
            }
        }
        // The line starts partway round the corners, not at the left edge
        EXPECT_GT(extent.minX, rect.x + radius * 0.2f);
        EXPECT_LT(extent.maxX, rect.x + rect.w - radius * 0.2f);

        // Too small to hold it: nothing
        EXPECT_EQ(Highlight({0, 0, 100, 2}, {}, kWhite, 1.0f).vertices.size(), 0u);
        // Square corners: the line runs the whole width
        const Extent square = ExtentOf(Highlight(rect, {}, kWhite, ppu));
        EXPECT_LE(square.minX, rect.x + UIBoxMesh::kHighlightWidth + feather + kTolerance);
        EXPECT_GE(square.maxX, rect.x + rect.w - UIBoxMesh::kHighlightWidth - feather - kTolerance);
    }

    TEST(UIBoxMeshTest, AppendingKeepsEarlierGeometryAndNumbersIndicesAbsolutely) {
        Mesh mesh;
        UIBoxMesh::AppendShadow(mesh.vertices, mesh.indices, {0, 0, 30, 20}, UICorners::Uniform(4), {0, 2, 6, 0, kBlue}, 1.0f);
        const size_t shadowVertices = mesh.vertices.size();
        const size_t shadowIndices = mesh.indices.size();
        UIBoxMesh::AppendFill(mesh.vertices, mesh.indices, {0, 0, 30, 20}, UICorners::Uniform(4), kRed, {}, 1.0f);
        ASSERT_GT(mesh.vertices.size(), shadowVertices);
        ASSERT_GT(mesh.indices.size(), shadowIndices);
        for (size_t i = shadowIndices; i < mesh.indices.size(); ++i) {
            EXPECT_GE(static_cast<size_t>(mesh.indices[i]), shadowVertices);
            EXPECT_LT(static_cast<size_t>(mesh.indices[i]), mesh.vertices.size());
        }
        ExpectWellFormed(mesh, "appended");
    }

    TEST(UIBoxMeshTest, DegenerateInputDrawsNothingOrThePlainRect) {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        const Rect good{0, 0, 40, 30};
        const Rect empties[] = {{0, 0, 0, 30}, {0, 0, 40, 0}, {0, 0, -5, 30}, {0, 0, 0, 0}, {nan, 0, 40, 30}, {0, nan, 40, 30}, {0, 0, nan, 30}, {0, 0, 40, nan}, {inf, 0, 40, 30}, {0, 0, inf, 30}};
        for (const Rect& rect : empties) {
            EXPECT_EQ(Fill(rect, UICorners::Uniform(4), kRed, 1.0f).vertices.size(), 0u);
            EXPECT_EQ(Border(rect, {}, 2.0f, kRed, 1.0f).vertices.size(), 0u);
            EXPECT_EQ(Shadow(rect, {}, {0, 0, 4, 0, kRed}, 1.0f).vertices.size(), 0u);
            EXPECT_EQ(Highlight(rect, {}, kRed, 1.0f).vertices.size(), 0u);
            EXPECT_EQ(Fill(rect, {}, kRed, 1.0f).indices.size(), 0u);
        }
        for (const float scale : {0.0f, -1.0f, nan, inf}) {
            EXPECT_EQ(Fill(good, {}, kRed, scale).vertices.size(), 0u);
            EXPECT_EQ(Border(good, {}, 2.0f, kRed, scale).vertices.size(), 0u);
            EXPECT_EQ(Shadow(good, {}, {0, 0, 4, 0, kRed}, scale).vertices.size(), 0u);
            EXPECT_EQ(Highlight(good, {}, kRed, scale).vertices.size(), 0u);
        }

        // Nothing to see: a colour with no alpha, a border with no width, a non-finite width or spec
        EXPECT_EQ(Fill(good, {}, Color{1, 2, 3, 0}, 1.0f).vertices.size(), 0u);
        EXPECT_EQ(Border(good, {}, 0.0f, kRed, 1.0f).vertices.size(), 0u);
        EXPECT_EQ(Border(good, {}, -2.0f, kRed, 1.0f).vertices.size(), 0u);
        EXPECT_EQ(Border(good, {}, nan, kRed, 1.0f).vertices.size(), 0u);
        EXPECT_EQ(Border(good, {}, 2.0f, Color{1, 2, 3, 0}, 1.0f).vertices.size(), 0u);
        EXPECT_EQ(Shadow(good, {}, {nan, 0, 4, 0, kRed}, 1.0f).vertices.size(), 0u);
        EXPECT_EQ(Shadow(good, {}, {0, 0, nan, 0, kRed}, 1.0f).vertices.size(), 0u);
        EXPECT_EQ(Shadow(good, {}, {0, 0, 4, inf, kRed}, 1.0f).vertices.size(), 0u);
        EXPECT_EQ(Shadow(good, {}, {0, 0, 4, 0, Color{1, 2, 3, 0}}, 1.0f).vertices.size(), 0u);

        // A zero or NaN radius is the plain rect; a zero blur is a hard shadow; a negative blur too
        const Mesh plain = Fill(good, {}, kRed, 1.0f);
        for (const UICorners& corners : {UICorners{nan, nan, nan, nan}, UICorners{-3, -3, -3, -3}, UICorners{inf, 0, 0, 0}}) {
            const Mesh mesh = Fill(good, corners, kRed, 1.0f);
            ASSERT_EQ(mesh.vertices.size(), plain.vertices.size());
            ASSERT_EQ(mesh.indices.size(), plain.indices.size());
        }
        EXPECT_GT(Shadow(good, {}, {0, 0, 0, 0, kRed}, 1.0f).vertices.size(), 0u);
        EXPECT_GT(Shadow(good, {}, {0, 0, -5, 0, kRed}, 1.0f).vertices.size(), 0u);
    }

    TEST(UIBoxMeshTest, ATinyRectStillMakesASaneMesh) {
        for (const float ppu : {0.5f, 1.0f, 3.0f}) {
            ExpectWellFormed(Fill({0, 0, 0.3f, 0.3f}, UICorners::Uniform(5), kRed, ppu), "tiny fill");
            ExpectWellFormed(Border({0, 0, 1.0f, 1.0f}, UICorners::Uniform(5), 0.4f, kRed, ppu), "tiny border");
            ExpectWellFormed(Shadow({0, 0, 0.5f, 0.5f}, UICorners::Uniform(5), {0, 0, 4, 0, kRed}, ppu), "tiny shadow");
        }
    }

}  // namespace BECore::Tests
