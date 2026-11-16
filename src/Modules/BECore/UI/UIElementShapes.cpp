#include "UIElementShapes.h"

#include <BECore/UI/UIRegions.h>

namespace BECore {

    namespace {

        /// Whether @p target is in the subtree of @p node (none when it is not) and whether it or an ancestor names a region the canvas published
        Optional<bool> UnderRegion(const UIRegionSet& regions, const UIElement& node, const UIElement& target, bool inherited) {
            const bool named = inherited || regions.Find(node.active.rect.region) != nullptr;
            if (&node == &target) {
                return named;
            }
            for (const auto& child : node.children) {
                if (const Optional<bool> found = UnderRegion(regions, *child, target, named)) {
                    return found;
                }
            }
            return {};
        }

        void SaveTransitions(UIElement& element, Vector<UIElementShapesScratch::SavedTransition>& saved) {
            if (UITransitionComponent* transition = element.GetComponent<UITransitionComponent>()) {
                saved.push_back({transition, transition->run, transition->held});
            }
            for (const auto& child : element.children) {
                SaveTransitions(*child, saved);
            }
        }

        /// A shape shows the authored state: a run that starts from nothing snaps to its target at the first layout
        void ForgetRuns(Span<const UIElementShapesScratch::SavedTransition> saved) {
            for (const auto& entry : saved) {
                entry.component->run = UITransitionRun{};
                entry.component->held = false;
            }
        }

        void RestoreRuns(Span<const UIElementShapesScratch::SavedTransition> saved) {
            for (const auto& entry : saved) {
                entry.component->run = entry.run;
                entry.component->held = entry.held;
            }
        }

        bool SameContexts(Span<const UILayoutContext> contexts, const Vector<UILayoutContext>& kept) {
            if (contexts.size() != kept.size()) {
                return false;
            }
            for (size_t i = 0; i < contexts.size(); ++i) {
                if (!(contexts[i] == kept[i])) {
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    UILayoutContext UIElementShapes::ContextFor(const UILayoutContext& live, const PreviewDevice& device, bool landscape) {
        UILayoutContext context = live;
        const float portraitWidth = static_cast<float>(device.width);
        const float portraitHeight = static_cast<float>(device.height);
        context.outputWidth = landscape ? portraitHeight : portraitWidth;
        context.outputHeight = landscape ? portraitWidth : portraitHeight;
        context.pixelDensity = 1.0f;
        context.safeArea = UIPreview::SimulatedSafeArea(SafeAreaSimulation::Device, &device, context.outputWidth, context.outputHeight);
        return context;
    }

    void UIElementShapes::Collect(UICanvasLayout& canvas, const UIElement& element, Span<const UILayoutContext> contexts, UIElementShapesResult& result) {
        UIElementShapesScratch scratch;
        Collect(canvas, element, contexts, result, scratch);
    }

    void UIElementShapes::Collect(UICanvasLayout& canvas, const UIElement& element, Span<const UILayoutContext> contexts, UIElementShapesResult& result, UIElementShapesScratch& scratch) {
        result.rects.assign(contexts.size(), Optional<Rect>{});
        result.approximate = false;
        if (contexts.empty() || !canvas.HasLayout() || !canvas.root || canvas.params.space != CanvasSpace::Screen || !UnderRegion(canvas.regions, *canvas.root, element, false)) {
            return;
        }

        const UILayoutContext live = canvas.GetContext();
        scratch.transitions.clear();
        SaveTransitions(*canvas.root, scratch.transitions);

        for (size_t i = 0; i < contexts.size(); ++i) {
            ForgetRuns(scratch.transitions);
            canvas.Invalidate();
            canvas.Update(contexts[i]);
            UIPreviewOverlay::ElementMarks(canvas, scratch.marks);
            for (const ElementMark& mark : scratch.marks) {
                if (mark.element == &element) {
                    result.rects[i] = mark.rect;
                    break;
                }
            }
            result.approximate = result.approximate || *UnderRegion(canvas.regions, *canvas.root, element, false);
        }

        // The runs go back before the layout: the pass of the transitions reads them, and a run that a shape started would keep an element visible in the live shape
        RestoreRuns(scratch.transitions);
        canvas.Invalidate();
        canvas.Update(live);
    }

    const UIElementShapesResult& UIElementShapesCache::Get(UICanvasLayout& canvas, const UIElement& element, Span<const UILayoutContext> contexts) {
        if (contexts.empty()) {
            Reset();
            return _result;
        }
        if (!_hasResult || _canvas != &canvas || _element != &element || _layoutRuns != canvas.GetLayoutRuns() || !SameContexts(contexts, _contexts)) {
            UIElementShapes::Collect(canvas, element, contexts, _result, _scratch);
            _canvas = &canvas;
            _element = &element;
            _layoutRuns = canvas.GetLayoutRuns();
            _contexts.assign(contexts.begin(), contexts.end());
            _hasResult = true;
            ++_probeRuns;
        }
        return _result;
    }

    void UIElementShapesCache::Reset() {
        _result.rects.clear();
        _result.approximate = false;
        _canvas = nullptr;
        _element = nullptr;
        _hasResult = false;
    }

}  // namespace BECore
