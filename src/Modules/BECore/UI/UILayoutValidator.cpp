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
            Walker(const UICanvasResolved& resolved, const UILayoutValidatorOptions& options, eastl::vector<UILayoutIssue>& issues) : _resolved(resolved), _options(options), _issues(issues) {}

            void VisitRoot(const UIElement& root) {
                CheckStackSiblings(root);
                for (const auto& child : root.children) {
                    Visit(*child, true);
                }
            }

        private:
            void Report(UILayoutIssueKind kind, const UIElement& element, PoolString other = {}) {
                _issues.push_back({kind, element.name, other, element.rect});
            }

            void Visit(const UIElement& element, bool childOfRoot) {
                if (!element.active.visible) {
                    return;
                }

                if (!element.rect.IsEmpty()) {
                    const bool keepsInSafeArea = !(element.ignoreSafeArea && childOfRoot);
                    if (!IsInside(element.rect, _resolved.bounds)) {
                        Report(UILayoutIssueKind::OutsideCanvas, element);
                    } else if (keepsInSafeArea && !IsInside(element.rect, _resolved.contentBounds)) {
                        Report(UILayoutIssueKind::OutsideSafeArea, element);
                    }
                }

                if (element.interactive) {
                    const float size = std::min(element.rect.w, element.rect.h) * _resolved.scale / std::max(_options.pixelDensity, 1e-3f);
                    if (size < _options.minTouchSize - kTolerance) {
                        Report(UILayoutIssueKind::TouchTargetTooSmall, element);
                    }
                }

                const UIContentFit fit = element.CheckContent(element.rect.w, element.rect.h, _resolved.scale);
                if (fit.hasContent && !fit.fits && !fit.canShrink) {
                    Report(UILayoutIssueKind::TextDoesNotFit, element);
                }

                CheckStackSiblings(element);
                for (const auto& child : element.children) {
                    Visit(*child, false);
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
            eastl::vector<UILayoutIssue>& _issues;
        };

    }  // namespace

    eastl::vector<UILayoutIssue> UILayoutValidator::Validate(const UIElement& root, const UICanvasResolved& resolved, const UILayoutValidatorOptions& options) {
        eastl::vector<UILayoutIssue> issues;
        Walker(resolved, options, issues).VisitRoot(root);
        return issues;
    }

}  // namespace BECore
