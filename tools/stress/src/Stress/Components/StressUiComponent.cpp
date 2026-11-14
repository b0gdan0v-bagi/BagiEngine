#include "StressUiComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/SceneEvents.h>
#include <Generated/StressUiComponent.gen.hpp>
#include <Stress/StressGame.h>

namespace BECore::Stress {

    namespace {

        /// The one name of every label: a name is a PoolString, so a million labels share it
        PoolString LabelName() {
            return "Label"_intern;
        }

    }  // namespace

    void StressUiComponent::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &StressUiComponent::OnUpdate>(this);
    }

    void StressUiComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        const StressGame* game = StressGame::GetActive();
        if (game == nullptr) {
            return;
        }
        const StressState& state = game->GetState();
        if (state.Version() == _builtVersion) {
            return;
        }
        if (!_canvas) {
            _canvas = GetNode().GetComponent<UICanvasComponent>();
            if (!_canvas) {
                LOG_ERROR("StressUiComponent: its node has no UICanvasComponent; no labels are shown");
                return;
            }
        }
        _builtVersion = state.Version();
        ApplyLanguage(state.GetLanguage());
        if (state.GetCount() != _builtCount) {
            Rebuild(*_canvas, state.GetCount());
        }
    }

    void StressUiComponent::ApplyLanguage(StressLanguage language) {
        if (_languageApplied && language == _builtLanguage) {
            return;
        }
        _languageApplied = true;
        _builtLanguage = language;
        const PoolString id = PoolString::Intern(ToFlagValue(language));
        if (!CoreManager::GetUIStrings().SetLanguage(id)) {
            LOG_WARNING(Format("StressUiComponent: the string table has no language '{}'; the {{#key}} labels show their keys", ToFlagValue(language)));
        }
    }

    void StressUiComponent::Rebuild(UICanvasComponent& canvas, uint32_t count) {
        const Scene& scene = GetScene();
        const UiGrid grid = MakeGrid(count, FieldView{scene.GetViewWidth(), scene.GetViewHeight()});
        const size_t target = count;
        auto& rootChildren = canvas.GetRoot().children;

        // The labels are the last children of the root: a shrink takes them off its tail
        if (_labels.size() > target) {
            rootChildren.resize(rootChildren.size() - (_labels.size() - target));
            _labels.resize(target);
        }

        _labels.reserve(target);
        rootChildren.reserve(rootChildren.size() + (target - _labels.size()));
        while (_labels.size() < target) {
            Label label;
            label.element = canvas.GetRoot().AddChild(LabelName());
            label.text = New<UITextComponent>();
            label.text->wrap = false;
            label.text->verticalAlign = UIVerticalAlign::Center;
            label.element->components.push_back(label.text);
            const StringView text = TextOf(static_cast<uint32_t>(_labels.size()));
            label.text->text.assign(text.data(), text.size());
            _labels.push_back(std::move(label));
        }

        // The grid depends on the count, so every label gets its cell again (once, not per frame)
        for (size_t i = 0; i < _labels.size(); ++i) {
            Place(_labels[i], static_cast<uint32_t>(i), grid);
        }

        // The tree changed behind the layout's back: one Invalidate for the whole change
        canvas.Invalidate();

        // The variables of the `Variable` labels; an equal value changes nothing, so nothing re-expands then
        CoreManager::GetUIVars().Set("count"_intern, count);
        CoreManager::GetUIVars().Set("last"_intern, count == 0 ? 0u : count - 1);
        _builtCount = count;
    }

    void StressUiComponent::Place(Label& label, uint32_t index, const UiGrid& grid) const {
        const UiCell cell = CellOf(index, grid);
        UIElementStyle& style = label.element->style;
        style.rect.anchor = UIAnchor::TopLeft;
        style.rect.x = UILength::Px(cell.x);
        style.rect.y = UILength::Px(cell.y);
        style.rect.width = UILength::Px(cell.width);
        style.rect.height = UILength::Px(cell.height);
        style.fontSize = cell.fontSize;
    }

}  // namespace BECore::Stress
