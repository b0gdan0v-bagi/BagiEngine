#pragma once

#include <BECore/Scene/SceneView.h>
#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIPreview.h>

namespace BECore::Tests {

    /// One shape of the output that a screen must survive: a size, a safe area and the user scale
    struct UIMatrixCase {
        eastl::string label;
        UILayoutContext context;
    };

    /// A case of the matrix that left issues
    struct UIMatrixFailure {
        eastl::string label;
        eastl::vector<UILayoutIssue> issues;
    };

    /// The view declaration of a scene (`viewWidth`, `viewHeight`, `<viewVariants>`): ValidateOnMatrix() picks from it the view the scene would draw with in each case
    struct UIMatrixViews {
        float defaultWidth = 0.0f;
        float defaultHeight = 0.0f;
        eastl::span<const SceneViewVariant> variants;
    };

    /// Fills the regions a game publishes for one case: @p regions is cleared first, @p resolved (the scale, `bounds`, `contentBounds`, `viewBounds`) is what the canvas will lay out in.
    /// A game passes the function it calls itself when its layout changes (the one that computes the scene regions from the bounds).
    using UIMatrixRegions = eastl::function<void(UIRegionSet& regions, const UICanvasResolved& resolved)>;

    namespace UIMatrixDetail {

        struct Aspect {
            const char* name;
            float width;
            float height;
        };

        // The aspect ratios of the plan: wide and ultrawide monitors, laptops, tablets, phones in both shapes
        constexpr Aspect kAspects[] = {{"16:9", 16, 9}, {"16:10", 16, 10}, {"3:2", 3, 2},   {"4:3", 4, 3},        {"1:1", 1, 1},
                                       {"21:9", 21, 9}, {"32:9", 32, 9},   {"9:16", 9, 16}, {"9:19.5", 9, 19.5f}, {"3:4", 3, 4}};

        // The short side of the output in pixels: small phone window up to 4K; a size whose long side passes 3840 is left out
        constexpr float kShortSides[] = {480.0f, 720.0f, 1080.0f, 1440.0f, 2160.0f};
        constexpr float kLongSideLimit = 3840.0f;

        constexpr const char* SafeName(SafeAreaSimulation kind) {
            switch (kind) {
                case SafeAreaSimulation::None:
                    return "no insets";
                case SafeAreaSimulation::TopNotch:
                    return "notch";
                case SafeAreaSimulation::SideInsets:
                    return "side insets";
            }
            return "";
        }

    }  // namespace UIMatrixDetail

    /// The output sizes and safe areas that every shipped screen is checked on: each aspect ratio at each size that fits 4K, with no insets, a top notch and side insets.
    /// The user scale and the scale mode are not part of it: ValidateOnMatrix() runs every case with each of them.
    inline const eastl::vector<UIMatrixCase>& UILayoutMatrix() {
        static const eastl::vector<UIMatrixCase> cases = [] {
            eastl::vector<UIMatrixCase> built;
            for (const auto& aspect : UIMatrixDetail::kAspects) {
                for (float shortSide : UIMatrixDetail::kShortSides) {
                    const bool landscape = aspect.width >= aspect.height;
                    const float width = landscape ? shortSide * aspect.width / aspect.height : shortSide;
                    const float height = landscape ? shortSide : shortSide * aspect.height / aspect.width;
                    if (std::max(width, height) > UIMatrixDetail::kLongSideLimit) {
                        continue;
                    }
                    for (auto kind : {SafeAreaSimulation::None, SafeAreaSimulation::TopNotch, SafeAreaSimulation::SideInsets}) {
                        UIMatrixCase matrixCase;
                        matrixCase.label = Format("{} {}x{} {}", aspect.name, static_cast<int>(width), static_cast<int>(height), UIMatrixDetail::SafeName(kind));
                        matrixCase.context.outputWidth = width;
                        matrixCase.context.outputHeight = height;
                        matrixCase.context.safeArea = UIPreview::SimulatedSafeArea(kind, width, height);
                        built.push_back(std::move(matrixCase));
                    }
                }
            }
            return built;
        }();
        return cases;
    }

    /**
     * @brief Lays @p canvas out on every case of UILayoutMatrix() and returns the cases where the validator found something.
     *
     * A Screen canvas runs with each of the three scale modes and the user scales 1.0 and 1.5; a SceneView or World canvas ignores both, so it runs once.
     * The canvas keeps its own params and regions afterwards. For a World canvas set `context.worldBox` through @p baseContext. A screen test is one line:
     * `EXPECT_TRUE(ValidateOnMatrix(canvas).empty())`.
     * @param baseContext the fields that the matrix does not set (the scene view size, the world box, the density)
     * @param views the scene's view declaration, for a SceneView canvas of a scene with `viewVariants`: every case sets `sceneViewWidth` / `sceneViewHeight` to the view
     *        that SceneViewSelector::Pick() gives for the output of the case (what Scene::Draw() would pick) and the label ends with `view <name>` (`view default` for the
     *        default, `view #<index>` for an unnamed variant). Left empty (the default), the matrix keeps the view size of @p baseContext in every case, as before.
     * @param publishRegions the game's function that computes its named regions (`UICanvasLayout::regions`) from the bounds of the case: called per case with an empty set and the
     *        resolved canvas (`UICanvasScaler::Resolve` of the case's params and context, before the layout), so the validator checks "inside its region" on the whole matrix.
     *        A name the function does not set is a `MissingRegion` in every case. Left empty (the default), the canvas keeps the regions it has in every case.
     */
    inline eastl::vector<UIMatrixFailure> ValidateOnMatrix(UICanvasLayout& canvas, const UILayoutValidatorOptions& options = {}, const UILayoutContext& baseContext = {},
                                                           const UIMatrixViews& views = {}, const UIMatrixRegions& publishRegions = {}) {
        const bool pickView = views.defaultWidth > 0.0f && views.defaultHeight > 0.0f;
        const UICanvasScaleParams saved = canvas.params;
        const UIRegionSet savedRegions = canvas.regions;
        const bool screen = saved.space == CanvasSpace::Screen;
        const CanvasScaleMode modes[] = {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ScaleWithScreen, CanvasScaleMode::ConstantPhysicalSize};
        const float userScales[] = {1.0f, 1.5f};

        eastl::vector<UIMatrixFailure> failures;
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            for (CanvasScaleMode mode : modes) {
                for (float userScale : userScales) {
                    if (!screen && (mode != modes[0] || userScale != userScales[0])) {
                        continue;
                    }
                    canvas.params = saved;
                    if (screen) {
                        canvas.params.scaleMode = mode;
                    }
                    UILayoutContext context = baseContext;
                    context.outputWidth = matrixCase.context.outputWidth;
                    context.outputHeight = matrixCase.context.outputHeight;
                    context.safeArea = matrixCase.context.safeArea;
                    context.userScale = userScale;
                    eastl::string viewName;
                    if (pickView) {
                        const ActiveSceneView view = SceneViewSelector::Pick(views.defaultWidth, views.defaultHeight, views.variants, context.outputWidth, context.outputHeight);
                        context.sceneViewWidth = view.width;
                        context.sceneViewHeight = view.height;
                        if (view.index < 0) {
                            viewName = " view default";
                        } else if (view.name.ToStringView().empty()) {
                            viewName = Format(" view #{}", view.index);
                        } else {
                            viewName = Format(" view {}", view.name.ToStringView());
                        }
                    }
                    if (publishRegions) {
                        canvas.regions.Clear();
                        publishRegions(canvas.regions, UICanvasScaler::Resolve(canvas.params, context));
                    }
                    canvas.Update(context);

                    UILayoutValidatorOptions caseOptions = options;
                    caseOptions.pixelDensity = context.pixelDensity;
                    auto issues = canvas.Validate(caseOptions);
                    if (!issues.empty()) {
                        const eastl::string_view modeName = screen ? EnumUtils<CanvasScaleMode>::ToString(mode) : eastl::string_view("-");
                        failures.push_back({Format("{} {} x{}{}", matrixCase.label, modeName, userScale, viewName), std::move(issues)});
                    }
                }
            }
        }
        canvas.params = saved;
        canvas.regions = savedRegions;
        canvas.Invalidate();
        return failures;
    }

    /// The first failures as text, for `EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures)`
    inline eastl::string DescribeMatrixFailures(const eastl::vector<UIMatrixFailure>& failures, size_t limit = 8) {
        eastl::string text = Format("{} case(s) with layout issues:\n", failures.size());
        for (size_t i = 0; i < failures.size() && i < limit; ++i) {
            text += Format("  {}\n", failures[i].label.c_str());
            for (const UILayoutIssue& issue : failures[i].issues) {
                text += Format("    {} '{}' at ({:.1f}, {:.1f}, {:.1f} x {:.1f})\n", EnumUtils<UILayoutIssueKind>::ToString(issue.kind), issue.element.ToStringView(), issue.rect.x, issue.rect.y,
                               issue.rect.w, issue.rect.h);
            }
        }
        return text;
    }

}  // namespace BECore::Tests
