#include "UILayoutValidator.h"

namespace BECore {

    namespace {

        constexpr float kTolerance = 0.01f;

        bool IsInside(const Rect& inner, const Rect& outer) {
            return inner.x >= outer.x - kTolerance && inner.y >= outer.y - kTolerance && inner.x + inner.w <= outer.x + outer.w + kTolerance && inner.y + inner.h <= outer.y + outer.h + kTolerance;
        }

        bool Overlap(const Rect& a, const Rect& b) {
            const float width = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x);
            const float height = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y);
            return width > kTolerance && height > kTolerance;
        }

        class Walker {
        public:
            Walker(const UICanvasResolved& resolved, const UILayoutValidatorOptions& options, const UIRegionSet& regions, eastl::vector<UILayoutIssue>& issues)
                : _resolved(resolved), _options(options), _regions(regions), _issues(issues) {}

            void VisitRoot(const UIElement& root) {
                CheckStackSiblings(root);
                const bool regionsIgnored = root.layout == UILayoutKind::Stack;
                for (const auto& child : root.children) {
                    const bool keepsOffView = eastl::find(_options.keepOffView.begin(), _options.keepOffView.end(), child->name) != _options.keepOffView.end();
                    Visit(*child, child->ignoreSafeArea, keepsOffView, regionsIgnored);
                }
            }

        private:
            void Report(UILayoutIssueKind kind, const UIElement& element, PoolString other = {}) {
                _issues.push_back({kind, element.name, other, element.rect});
            }

            // @p ignoresSafeArea: a child of the root asked for the whole canvas (`ignoreSafeArea`), and so does everything under it; the flag of a deeper element means nothing
            // @p keepsOffView: a named child of the root (`keepOffView`), and so is everything under it, down to the first element that lies over the view
            // @p regionsIgnored: the parent is a stack or a popup element, which place their children themselves, so a `region` of this element does nothing
            void Visit(const UIElement& element, bool ignoresSafeArea, bool keepsOffView, bool regionsIgnored) {
                if (!element.active.visible) {
                    return;
                }

                CheckRegion(element, regionsIgnored);

                if (!element.rect.IsEmpty()) {
                    if (!IsInside(element.rect, _resolved.bounds)) {
                        Report(UILayoutIssueKind::OutsideCanvas, element);
                    } else if (!ignoresSafeArea && !IsInside(element.rect, _resolved.contentBounds)) {
                        Report(UILayoutIssueKind::OutsideSafeArea, element);
                    }
                }

                if (element.IsInteractive()) {
                    const float size = std::min(element.rect.w, element.rect.h) * _resolved.scale / std::max(_options.pixelDensity, 1e-3f);
                    if (size < _options.minTouchSize - kTolerance) {
                        Report(UILayoutIssueKind::TouchTargetTooSmall, element);
                    }
                }

                const UIContentFit fit = element.CheckContent(element.rect.w, element.rect.h, _resolved.scale);
                if (fit.hasContent && !fit.fits && !fit.canShrink) {
                    Report(UILayoutIssueKind::TextDoesNotFit, element);
                }

                if (keepsOffView && !element.rect.IsEmpty() && Overlap(element.rect, _resolved.viewBounds)) {
                    Report(UILayoutIssueKind::OverlapsSceneView, element);
                    return;
                }

                // The children of a popup are out of flow: a closed one has none to look at, an open one is checked like anything else, and its options are not siblings in a stack
                if (element.HasPopupChildren() && !element.IsPopupOpen()) {
                    return;
                }
                if (!element.HasPopupChildren()) {
                    CheckStackSiblings(element);
                }
                const bool childrenIgnoreRegions = element.layout == UILayoutKind::Stack || element.HasPopupChildren();
                for (const auto& child : element.children) {
                    Visit(*child, ignoresSafeArea, keepsOffView, childrenIgnoreRegions);
                }
            }

            void CheckRegion(const UIElement& element, bool regionsIgnored) {
                const PoolString name = element.active.rect.region;
                if (name.Empty()) {
                    return;
                }
                if (regionsIgnored) {
                    Report(UILayoutIssueKind::RegionIgnored, element, name);
                    return;
                }
                const Rect* region = _regions.Find(name);
                if (!region) {
                    Report(UILayoutIssueKind::MissingRegion, element, name);
                } else if (!element.rect.IsEmpty() && !IsInside(element.rect, *region)) {
                    Report(UILayoutIssueKind::OutsideRegion, element, name);
                }
            }

            void CheckStackSiblings(const UIElement& parent) {
                if (parent.layout != UILayoutKind::Stack) {
                    return;
                }
                for (size_t i = 0; i < parent.children.size(); ++i) {
                    const UIElement& first = *parent.children[i];
                    if (!first.active.visible || first.rect.IsEmpty()) {
                        continue;
                    }
                    for (size_t j = i + 1; j < parent.children.size(); ++j) {
                        const UIElement& second = *parent.children[j];
                        if (second.active.visible && !second.rect.IsEmpty() && Overlap(first.rect, second.rect)) {
                            Report(UILayoutIssueKind::SiblingsOverlap, first, second.name);
                        }
                    }
                }
            }

            const UICanvasResolved& _resolved;
            const UILayoutValidatorOptions& _options;
            const UIRegionSet& _regions;
            eastl::vector<UILayoutIssue>& _issues;
        };

    }  // namespace

    eastl::vector<UILayoutIssue> UILayoutValidator::Validate(const UIElement& root, const UICanvasResolved& resolved, const UILayoutValidatorOptions& options, const UIRegionSet& regions) {
        eastl::vector<UILayoutIssue> issues;
        Walker(resolved, options, regions, issues).VisitRoot(root);
        return issues;
    }

}  // namespace BECore
