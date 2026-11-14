// What the UI loaders and the pure geometry do with content that is larger, longer or odder than any real file: the string and theme tables and the numbers of boxes, panels and
// layouts here, the web front end in UIWebTest.cpp

#include <BECore/UI/UIBoxMesh.h>
#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIGeometryBounds.h>
#include <BECore/UI/UILayout.h>
#include <BECore/UI/UINineSlice.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UITidyMessage.h>
#include <cmath>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// The problems a table gives for the text of a file, each of which must be one short line
        template <typename Table>
        eastl::vector<String> Load(Table& table, const std::string& xml) {
            pugi::xml_document document;
            EXPECT_TRUE(document.load_buffer(xml.data(), xml.size()));
            return table.LoadFromXml(XmlNode{document.document_element()});
        }

        void ExpectOneShortLine(const eastl::vector<String>& problems) {
            ASSERT_FALSE(problems.empty());
            for (const String& problem : problems) {
                EXPECT_LE(problem.size(), UIText::kMaxMessageBytes);
                EXPECT_EQ(problem.find('\n'), String::npos);
                EXPECT_EQ(problem.find('\t'), String::npos);
            }
        }

    }  // namespace

    TEST(UIContentLimitsTest, TheProblemsOfAStringTableAreShortLinesWhateverTheFileSays) {
        const std::string longKey = std::string(5000, 'k') + "}";
        const std::string xml = "<UIStrings default=\"" + std::string(2000, 'd') +
                                "\">"
                                "<language id=\"en\"><string key=\"" +
                                longKey + "\" text=\"x\"/><string key=\"a&#10;b\"/><other-" + std::string(3000, 'o') +
                                "/></language>"
                                "<language id=\"" +
                                std::string(1000, 'l') + "\"/><weird/></UIStrings>";
        UIStrings strings;
        ExpectOneShortLine(Load(strings, xml));
    }

    TEST(UIContentLimitsTest, TheProblemsOfAThemeTableAreShortLinesWhateverTheFileSays) {
        const std::string xml = "<UIThemes default=\"" + std::string(2000, 'd') +
                                "\">"
                                "<theme id=\"t\"><color name=\"" +
                                std::string(5000, 'n') + "\" value=\"#fff\"/><color name=\"a\" value=\"" + std::string(4000, 'v') + "&#10;\"/><color name=\"b&#9;c\" value=\"#fff\"/><x-" +
                                std::string(3000, 'x') + "/></theme><theme id=\"" + std::string(1000, 't') + "\"/></UIThemes>";
        UIThemes themes;
        ExpectOneShortLine(Load(themes, xml));
    }

    TEST(UIContentLimitsTest, AShortCleanMessageIsLeftAsItIs) {
        EXPECT_EQ(UIText::TidyMessage("a plain message"), "a plain message");
        EXPECT_EQ(UIText::TidyMessage("tab\tand\nline"), "tab and line");
        const std::string exact(UIText::kMaxMessageBytes, 'e');
        EXPECT_EQ(UIText::TidyMessage(FromStd(exact)).size(), UIText::kMaxMessageBytes);
        const std::string over(UIText::kMaxMessageBytes + 1, 'e');
        const String cut = UIText::TidyMessage(FromStd(over));
        EXPECT_EQ(cut.size(), UIText::kMaxMessageBytes);
        EXPECT_EQ(cut.substr(cut.size() - 3), "...");
        EXPECT_EQ(UIText::Excerpt(FromStd(over)).size(), UIText::kMaxExcerptBytes);
    }

    // =========================================================================
    // The numbers of a content file in the pure UI geometry (the target UiGeometry of tests/Fuzz)
    // =========================================================================

    namespace {

        constexpr float kInfinity = std::numeric_limits<float>::infinity();
        constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
        constexpr float kLargest = std::numeric_limits<float>::max();
        constexpr float kDenormal = std::numeric_limits<float>::denorm_min();

        /// The values a file can write that are not a number of a screen
        constexpr float kExtremes[] = {kNan, kInfinity, -kInfinity, kLargest, -kLargest, kDenormal, -kDenormal, 1.0e8f};

        void ExpectLaidOut(const Rect& rect) {
            EXPECT_TRUE(UIBounds::IsBounded(rect.x)) << rect.x;
            EXPECT_TRUE(UIBounds::IsBounded(rect.y)) << rect.y;
            EXPECT_TRUE(UIBounds::IsBounded(rect.w)) << rect.w;
            EXPECT_TRUE(UIBounds::IsBounded(rect.h)) << rect.h;
            EXPECT_GE(rect.w, 0.0f);
            EXPECT_GE(rect.h, 0.0f);
        }

        bool IsFinite(const Rect& rect) {
            return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.w) && std::isfinite(rect.h);
        }

        /// A spec that pins a 220 x 96 element, the kind every screen has
        UIRectSpec PinnedSpec() {
            UIRectSpec spec;
            spec.x = UILength::Px(16.0f);
            spec.y = UILength::Px(16.0f);
            spec.width = UILength::Px(220.0f);
            spec.height = UILength::Px(96.0f);
            return spec;
        }

        /// Every float field of a spec, one at a time (a pointer into the spec it was made for)
        eastl::vector<float*> FloatFields(UIRectSpec& spec) {
            eastl::vector<float*> fields = {&spec.x.value,     &spec.y.value,    &spec.width.value,  &spec.height.value,  &spec.aspectRatio,
                                            &spec.margin.left, &spec.margin.top, &spec.margin.right, &spec.margin.bottom, &spec.flex};
            spec.minWidth = UILength::Px(10.0f);
            spec.maxWidth = UILength::Px(500.0f);
            spec.minHeight = UILength::Percent(10.0f);
            spec.maxHeight = UILength::Percent(90.0f);
            spec.anchorMin = Vec2{0.0f, 0.0f};
            spec.anchorMax = Vec2{1.0f, 1.0f};
            spec.pivot = Vec2{0.5f, 0.5f};
            for (float* field : {&spec.minWidth->value, &spec.maxWidth->value, &spec.minHeight->value, &spec.maxHeight->value, &spec.anchorMin->x, &spec.anchorMin->y, &spec.anchorMax->x,
                                 &spec.anchorMax->y, &spec.pivot->x, &spec.pivot->y}) {
                fields.push_back(field);
            }
            return fields;
        }

    }  // namespace

    TEST(UIContentLimitsTest, ANumberOfARealScreenIsNotChangedByTheBound) {
        for (const float value : {0.0f, -0.5f, 1.0f, 220.0f, 1280.0f, 30720.0f, UIBounds::kMaxExtent, -UIBounds::kMaxExtent}) {
            EXPECT_EQ(UIBounds::Bound(value), value);
            EXPECT_TRUE(UIBounds::IsBounded(value));
        }
        EXPECT_EQ(UIBounds::Bound(kNan), 0.0f);
        EXPECT_EQ(UIBounds::Bound(kInfinity), UIBounds::kMaxExtent);
        EXPECT_EQ(UIBounds::Bound(-kLargest), -UIBounds::kMaxExtent);
        EXPECT_FALSE(UIBounds::IsBounded(kNan));
        EXPECT_FALSE(UIBounds::IsBounded(kInfinity));
        EXPECT_FALSE(UIBounds::IsBounded(UIBounds::kMaxExtent * 1.01f));
        const Rect pinned = UILayout::ResolveRect(Rect{0.0f, 0.0f, 1280.0f, 720.0f}, PinnedSpec());
        EXPECT_EQ(pinned.x, 16.0f);
        EXPECT_EQ(pinned.y, 16.0f);
        EXPECT_EQ(pinned.w, 220.0f);
        EXPECT_EQ(pinned.h, 96.0f);
    }

    TEST(UIContentLimitsTest, AnyNumberOfASpecGivesABoundedRectWithNoNegativeSize) {
        const Rect parent{0.0f, 0.0f, 1280.0f, 720.0f};
        UIRectSpec probe = PinnedSpec();
        const size_t fieldCount = FloatFields(probe).size();
        for (size_t index = 0; index < fieldCount; ++index) {
            for (const float extreme : kExtremes) {
                UIRectSpec spec = PinnedSpec();
                *FloatFields(spec)[index] = extreme;
                ExpectLaidOut(UILayout::ResolveRect(parent, spec));
                spec.anchor = UIAnchor::Stretch;
                ExpectLaidOut(UILayout::ResolveRect(parent, spec));
                const UIEffectiveAnchors anchors = UILayout::EffectiveAnchors(spec);
                EXPECT_TRUE(UIBounds::IsBounded(anchors.min.x) && UIBounds::IsBounded(anchors.max.y) && UIBounds::IsBounded(anchors.pivot.x));
            }
        }
        for (const float extreme : kExtremes) {
            ExpectLaidOut(UILayout::ResolveRect(Rect{extreme, extreme, extreme, extreme}, PinnedSpec()));
            ExpectLaidOut(UILayout::Inset(parent, UIEdges{extreme, extreme, extreme, extreme}));
        }
    }

    TEST(UIContentLimitsTest, AnyNumberOfAStackGivesBoundedRectsWithNoNegativeSize) {
        const Rect parent{0.0f, 0.0f, 320.0f, 480.0f};
        for (const bool wrap : {false, true}) {
            for (const float extreme : kExtremes) {
                UIStackSpec stack;
                stack.wrap = wrap;
                eastl::vector<UIRectSpec> children(3, PinnedSpec());
                children[1].flex = 1.0f;
                children[2].width = UILength::Px(0.0f);
                children[2].aspectRatio = 2.0f;
                eastl::vector<Rect> rects;

                stack.spacing = extreme;
                UILayout::LayoutStack(parent, stack, children, rects);
                ASSERT_EQ(rects.size(), children.size());
                for (const Rect& rect : rects) {
                    ExpectLaidOut(rect);
                }

                stack = UIStackSpec{};
                stack.wrap = wrap;
                stack.padding = UIEdges{extreme, extreme, extreme, extreme};
                UILayout::LayoutStack(parent, stack, children, rects);
                for (const Rect& rect : rects) {
                    ExpectLaidOut(rect);
                }

                stack = UIStackSpec{};
                stack.wrap = wrap;
                children[1].flex = extreme;
                children[2].aspectRatio = extreme;
                children[0].height = UILength::Percent(extreme);
                UILayout::LayoutStack(Rect{extreme, 0.0f, extreme, extreme}, stack, children, rects);
                for (const Rect& rect : rects) {
                    ExpectLaidOut(rect);
                }
            }
        }
    }

    TEST(UIContentLimitsTest, ANineSliceWithANegativeDestinationAndTinyBordersHasNoInfinitePiece) {
        // The factor that shrinks the borders was extent / borders: -10 / 2e-44 is -infinity, and a border times it is -infinity
        const UIEdges tiny{1.0e-44f, 1.0e-44f, 1.0e-44f, 1.0e-44f};
        for (const float extent : {-10.0f, -kLargest, kNan, -kInfinity}) {
            for (const UINineSlicePiece& piece : UINineSlice::Compute(Rect{0.0f, 0.0f, 64.0f, 64.0f}, tiny, Rect{0.0f, 0.0f, extent, extent})) {
                EXPECT_TRUE(IsFinite(piece.source));
                EXPECT_TRUE(IsFinite(piece.destination));
            }
        }
    }

    TEST(UIContentLimitsTest, AnyNumberOfANineSliceGivesFinitePiecesThatHaveAreaOnBothSidesOrNeither) {
        for (const float extreme : kExtremes) {
            const Rect rects[] = {{extreme, 0.0f, 64.0f, 64.0f}, {0.0f, 0.0f, extreme, 64.0f}, {0.0f, 0.0f, 64.0f, extreme}};
            const UIEdges borders[] = {{extreme, 16.0f, 16.0f, 16.0f}, {16.0f, extreme, 16.0f, 16.0f}, {16.0f, 16.0f, extreme, 16.0f}, {16.0f, 16.0f, 16.0f, extreme}};
            for (const Rect& source : rects) {
                for (const UIEdges& border : borders) {
                    for (const Rect& destination : rects) {
                        for (const UINineSlicePiece& piece : UINineSlice::Compute(source, border, destination)) {
                            EXPECT_TRUE(IsFinite(piece.source));
                            EXPECT_TRUE(IsFinite(piece.destination));
                            EXPECT_GE(piece.source.w, 0.0f);
                            EXPECT_GE(piece.destination.w, 0.0f);
                            EXPECT_EQ(piece.source.w > 0.0f && piece.source.h > 0.0f, piece.destination.w > 0.0f && piece.destination.h > 0.0f);
                        }
                    }
                }
            }
        }
    }

    TEST(UIContentLimitsTest, ACanvasWithAnyContextGetsAPositiveScaleAndBoundedBounds) {
        const auto expectResolved = [](const UICanvasResolved& resolved) {
            EXPECT_GE(resolved.scale, UIBounds::kMinScale);
            EXPECT_LE(resolved.scale, UIBounds::kMaxScale);
            for (const Rect& rect : {resolved.bounds, resolved.viewBounds, resolved.safeBounds, resolved.contentBounds}) {
                ExpectLaidOut(rect);
            }
            EXPECT_TRUE(std::isfinite(resolved.transform.offsetX) && std::isfinite(resolved.transform.offsetY));
        };
        for (const CanvasSpace space : {CanvasSpace::Screen, CanvasSpace::SceneView, CanvasSpace::World}) {
            for (const CanvasScaleMode mode : {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ConstantPhysicalSize, CanvasScaleMode::ScaleWithScreen}) {
                for (const float extreme : kExtremes) {
                    UICanvasScaleParams params;
                    params.space = space;
                    params.scaleMode = mode;
                    params.minScale = 0.0f;
                    params.maxScale = 0.0f;
                    UILayoutContext context;
                    context.outputWidth = 1280.0f;
                    context.outputHeight = 720.0f;
                    context.sceneViewWidth = 1280.0f;
                    context.sceneViewHeight = 720.0f;

                    // A scale of 1e-45 divided the output into infinity before the range of the scale was kept
                    for (float* field : {&context.pixelDensity, &context.displayScale, &context.userScale, &context.outputWidth, &context.outputHeight, &context.sceneViewWidth,
                                         &context.sceneViewHeight, &context.safeArea.x, &context.safeArea.w, &context.worldBox.x, &context.worldBox.w, &context.worldBox.h}) {
                        const float kept = *field;
                        *field = extreme;
                        expectResolved(UICanvasScaler::Resolve(params, context));
                        params.referenceWidth = extreme;
                        expectResolved(UICanvasScaler::Resolve(params, context));
                        params.referenceWidth = 1280.0f;
                        params.match = extreme;
                        expectResolved(UICanvasScaler::Resolve(params, context));
                        params.match = 0.5f;
                        *field = kept;
                    }
                }
            }
        }
    }

    TEST(UIContentLimitsTest, ACanvasTreeOfAnyNumbersIsLaidOutInBoundedRects) {
        for (const float extreme : kExtremes) {
            UICanvasLayout canvas;
            UIRectSpec spec = PinnedSpec();
            spec.anchor = UIAnchor::Stretch;
            spec.margin.left = extreme;
            spec.aspectRatio = extreme;
            canvas.root->AddChild(PoolString::Intern("Panel"))->style.rect = spec;
            const auto stack = canvas.root->AddChild(PoolString::Intern("Rows"));
            stack->layout = UILayoutKind::Stack;
            stack->style.stack.spacing = extreme;
            stack->AddChild(PoolString::Intern("Row"))->style.rect = spec;
            UILayoutContext context;
            context.outputWidth = 1280.0f;
            context.outputHeight = 720.0f;
            context.userScale = extreme;
            canvas.Update(context);
            const auto expectTree = [](const UIElement& element, const auto& self) -> void {
                ExpectLaidOut(element.rect);
                for (const auto& child : element.children) {
                    self(*child, self);
                }
            };
            expectTree(*canvas.root, expectTree);
            canvas.HitTest(extreme, extreme);
        }
    }

    TEST(UIContentLimitsTest, ABoxWithNumbersPastTheBoundIsNotDrawnAndAnyOtherIsInsideTheCounts) {
        const Color color{40, 90, 160, 255};
        const UICorners round = UICorners::Uniform(12.0f);
        const Rect box{16.0f, 16.0f, 220.0f, 48.0f};
        const UIShadowSpec shadow{0.0f, 4.0f, 8.0f, 0.0f, Color{0, 0, 0, 120}};

        const auto drawAll = [&](const Rect& rect, const UICorners& corners, const UIShadowSpec& spec, float width, float pixelsPerUnit) {
            eastl::vector<GeometryVertex> vertices;
            eastl::vector<int> indices;
            UIBoxMesh::AppendFill(vertices, indices, rect, corners, color, UIGradientSpec{UIGradientDirection::Vertical, color}, pixelsPerUnit);
            UIBoxMesh::AppendBorder(vertices, indices, rect, corners, width, color, pixelsPerUnit);
            UIBoxMesh::AppendShadow(vertices, indices, rect, corners, spec, pixelsPerUnit);
            UIBoxMesh::AppendHighlight(vertices, indices, rect, corners, color, pixelsPerUnit);
            EXPECT_LE(vertices.size(), static_cast<size_t>(4 * UIBoxMesh::kMaxVerticesPerCall));
            EXPECT_LE(indices.size(), static_cast<size_t>(4 * UIBoxMesh::kMaxIndicesPerCall));
            for (const GeometryVertex& vertex : vertices) {
                EXPECT_TRUE(std::isfinite(vertex.x) && std::isfinite(vertex.y));
            }
            for (const int index : indices) {
                EXPECT_LT(static_cast<size_t>(index), vertices.size());
            }
            return vertices.size();
        };

        EXPECT_GT(drawAll(box, round, shadow, 2.0f, 1.5f), 0u);
        // A shadow of 8 units blur and 12 units round corners at the largest scale is the dearest real case; the counts hold for it too
        EXPECT_GT(drawAll(Rect{0.0f, 0.0f, 400.0f, 400.0f}, UICorners::Uniform(200.0f), UIShadowSpec{0.0f, 0.0f, 100.0f, 0.0f, Color{0, 0, 0, 255}}, 3.0f, UIBounds::kMaxScale), 0u);

        for (const float extreme : kExtremes) {
            for (const float pixelsPerUnit : {extreme, 1.0f}) {
                drawAll(Rect{extreme, 0.0f, 100.0f, 100.0f}, round, shadow, 2.0f, pixelsPerUnit);
                drawAll(Rect{0.0f, 0.0f, extreme, 100.0f}, round, shadow, 2.0f, pixelsPerUnit);
                drawAll(box, UICorners{extreme, extreme, extreme, extreme}, shadow, extreme, pixelsPerUnit);
                drawAll(box, round, UIShadowSpec{extreme, extreme, extreme, extreme, Color{0, 0, 0, 120}}, 2.0f, pixelsPerUnit);
                drawAll(box, round, UIShadowSpec{0.0f, 0.0f, extreme, 0.0f, Color{0, 0, 0, 120}}, 2.0f, pixelsPerUnit);
                drawAll(box, round, UIShadowSpec{0.0f, 0.0f, 0.0f, extreme, Color{0, 0, 0, 120}}, 2.0f, pixelsPerUnit);
            }
        }

        // A box or a shadow past the bound is not drawn at all: the sum of its edges would be infinite
        eastl::vector<GeometryVertex> vertices;
        eastl::vector<int> indices;
        UIBoxMesh::AppendShadow(vertices, indices, box, round, UIShadowSpec{0.0f, 0.0f, kLargest, 0.0f, Color{0, 0, 0, 120}}, 1.0f);
        UIBoxMesh::AppendFill(vertices, indices, Rect{kLargest, 0.0f, 100.0f, 100.0f}, round, color, UIGradientSpec{}, 1.0f);
        UIBoxMesh::AppendFill(vertices, indices, box, round, color, UIGradientSpec{}, kDenormal);
        EXPECT_TRUE(vertices.empty());
        EXPECT_TRUE(indices.empty());
    }

    TEST(UIContentLimitsTest, TheRadiiOfAnyCornersAreFiniteAndNotNegative) {
        for (const float extreme : kExtremes) {
            const UICorners clamped = UICorners{extreme, 1.0f, extreme, -1.0f}.Clamped(100.0f, 50.0f);
            for (const float radius : {clamped.topLeft, clamped.topRight, clamped.bottomRight, clamped.bottomLeft}) {
                EXPECT_TRUE(std::isfinite(radius));
                EXPECT_GE(radius, 0.0f);
            }
            for (const float radius : {clamped.Inset(extreme).topLeft, clamped.Inset(extreme).bottomRight}) {
                EXPECT_TRUE(std::isfinite(radius));
                EXPECT_GE(radius, 0.0f);
            }
        }
        EXPECT_GE(UIBoxMesh::CornerSegments(kNan), 0);
        EXPECT_EQ(UIBoxMesh::CornerSegments(kInfinity), UIBoxMesh::kMaxCornerSegments);
        EXPECT_EQ(UIBoxMesh::CornerSegments(-1.0f), 0);
    }

}  // namespace BECore::Tests
