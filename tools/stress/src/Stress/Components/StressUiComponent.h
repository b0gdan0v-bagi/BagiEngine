#pragma once

#include <BECore/Scene/IComponent.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UITextComponent.h>
#include <Stress/StressSettings.h>
#include <Stress/UiField.h>

namespace BECore::SceneEvents {
    struct SceneUpdateEvent;
}

namespace BECore::Stress {

    /**
     * @brief The scene `StressUi`: `StressState::GetCount()` text labels on the `UICanvasComponent` of this node, laid out on the grid of `UiField`,
     *        in English and Russian.
     *
     * The labels are `UIElement` + `UITextComponent` children of the canvas's root, made when `StressState::Version()` moved: a bigger count appends,
     * a smaller one removes from the tail, and the first labels are kept (their cells are set again, because the grid changes with the count). The
     * canvas is invalidated once after the change. A language change calls `UIStrings::SetLanguage`: the strings' own version rewords the
     * `{#key}` labels at the next draw, no label is touched. The variables the `Variable` labels name (`count`, `last`) are set with the count.
     * A steady frame does nothing here. Does nothing while the game is not the active `StressGame`.
     */
    class StressUiComponent : public IComponent {
        BE_CLASS(StressUiComponent)
    public:
        StressUiComponent() = default;
        ~StressUiComponent() override = default;

        void OnAttached() override;

        /// The labels made so far (a test counts them)
        size_t GetLabelCount() const {
            return _labels.size();
        }

    private:
        static constexpr uint32_t kNothingBuilt = std::numeric_limits<uint32_t>::max();  // above kMaxCount: the first update always builds

        struct Label {
            IntrusivePtr<UIElement> element;
            IntrusivePtr<UITextComponent> text;
        };

        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);

        /// Brings the labels to @p count (appends or removes from the tail) and places all of them on the grid
        void Rebuild(UICanvasComponent& canvas, uint32_t count);
        void Place(Label& label, uint32_t index, const UiGrid& grid) const;
        /// Makes @p language the language of the string table, once per change
        void ApplyLanguage(StressLanguage language);

        Vector<Label> _labels;
        IntrusivePtr<UICanvasComponent> _canvas;
        uint64_t _builtVersion = 0;  // the StressState version the labels were last brought to; 0 is never a version
        uint32_t _builtCount = kNothingBuilt;
        bool _languageApplied = false;
        StressLanguage _builtLanguage = StressLanguage::En;
    };

}  // namespace BECore::Stress
