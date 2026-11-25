#include "UIDrawList.h"

#include <BECore/Renderer/IRenderer.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UITransition.h>

namespace BECore {

    namespace {

        Vec2 CentreOf(const Rect& rect) {
            return {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
        }

    }  // namespace

    void UIDrawList::Rebuild(UIElement& root) {
        _entries.clear();
        _sections.clear();
        _mainEnd = 0;
        _maxDepth = 0;
        _ownerCount = 0;

        Append(root, -1, 1, false);
        _mainEnd = static_cast<uint32_t>(_entries.size());
        // The sections are not added to while they are filled: a popup inside a popup is no section (its owner is drawn, its children are not)
        for (PopupSection& section : _sections) {
            UIElement& owner = *_entries[section.owner].element;
            section.begin = static_cast<uint32_t>(_entries.size());
            for (const auto& child : owner.children) {
                Append(*child, -1, 1, true);
            }
            section.end = static_cast<uint32_t>(_entries.size());
        }
        _scopes.clear();
        _scopes.reserve(_maxDepth);
        _owners.resize(_ownerCount);
    }

    void UIDrawList::Draw(IRenderer& renderer, double now, UIThemes& themes) {
        const uint64_t themeVersion = themes.GetVersion();
        DrawRange(renderer, now, themes, themeVersion, 0, _mainEnd);
        for (const PopupSection& section : _sections) {
            if (_entries[section.owner].element->IsPopupOpen()) {
                DrawRange(renderer, now, themes, themeVersion, section.begin, section.end);
            }
        }
    }

    void UIDrawList::CloseScopes(IRenderer& renderer, uint32_t reached) {
        while (!_scopes.empty() && _scopes.back().end <= reached) {
            const Scope& scope = _scopes.back();
            if (scope.viewChanged) {
                renderer.SetViewTransform(scope.view);
            }
            renderer.SetOpacity(scope.opacity);
            _scopes.pop_back();
        }
    }

    void UIDrawList::DrawRange(IRenderer& renderer, double now, UIThemes& themes, uint64_t themeVersion, uint32_t begin, uint32_t end) {
        for (uint32_t index = begin; index < end; ++index) {
            CloseScopes(renderer, index);
            const Entry& entry = _entries[index];
            UIElement& element = *entry.element;

            const float factor = IRenderer::ClampOpacity(element.opacity);
            const bool transitioning = entry.transition && entry.transition->run.IsTransitioning(now);
            if (factor != 1.0f || transitioning) {
                Scope& scope = _scopes.emplace_back();
                scope.end = entry.subtreeEnd;
                scope.opacity = renderer.GetOpacity();
                renderer.SetOpacity(scope.opacity * factor);
                if (transitioning) {
                    const UITransition::Evaluated evaluated = UITransition::Evaluate(entry.transition->MakeSpec(), entry.transition->run.Progress(now));
                    scope.view = renderer.GetViewTransform();
                    scope.viewChanged = true;
                    renderer.SetViewTransform(UITransition::Compose(scope.view, CentreOf(element.rect), evaluated));
                    renderer.SetOpacity(renderer.GetOpacity() * IRenderer::ClampOpacity(evaluated.opacity));
                }
            }

            if (element.IsThemeStale(themeVersion)) {
                element.ResolveTheme(themes);
            }
            if (entry.inheritSlot >= 0) {
                const OwnerState& owner = _owners[static_cast<size_t>(entry.inheritSlot)];
                element.SetInheritedState(&owner.state, owner.duration);
            }
            if (entry.ownSlot >= 0) {
                _owners[static_cast<size_t>(entry.ownSlot)] = {element.GetVisualState(), element.GetStateDuration()};
            }
            element.DrawSelf(renderer);
            element.DrawComponents(renderer);
        }
        CloseScopes(renderer, end);
    }

    void UIDrawList::Append(UIElement& element, int32_t inheritSlot, uint32_t depth, bool inSection) {
        if (!element.active.visible) {
            return;
        }
        const uint32_t index = static_cast<uint32_t>(_entries.size());
        _maxDepth = std::max(_maxDepth, depth);
        {
            Entry& entry = _entries.emplace_back();
            entry.element = IntrusivePtr<UIElement>(&element);
            entry.transition = IntrusivePtr<UITransitionComponent>(element.GetComponent<UITransitionComponent>());
            entry.inheritSlot = inheritSlot;
            if (element.OwnsState()) {
                entry.ownSlot = static_cast<int32_t>(_ownerCount++);
            }
        }
        const int32_t ownSlot = _entries[index].ownSlot;
        if (inheritSlot < 0) {
            element.SetInheritedState(nullptr, 0.0f);  // nothing is handed down to it, so the per-frame call has nothing to say
        }
        if (element.HasPopupChildren()) {
            if (!inSection) {
                _sections.push_back({index, 0, 0});
            }
        } else {
            const int32_t childSlot = ownSlot >= 0 ? ownSlot : inheritSlot;
            for (const auto& child : element.children) {
                Append(*child, childSlot, depth + 1, inSection);
            }
        }
        _entries[index].subtreeEnd = static_cast<uint32_t>(_entries.size());
    }

}  // namespace BECore
