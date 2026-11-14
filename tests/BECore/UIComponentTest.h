#pragma once

#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIComponent.h>
#include <BECore/UI/UIElement.h>

namespace BECore::Tests {

    /// What the test components did, in order: "<tag> <hook> <owner name>" (a click adds nothing else). Cleared by the fixture
    inline eastl::vector<String>& UIComponentLog() {
        static eastl::vector<String> log;
        return log;
    }

    /// Records every hook it gets, draws a 4 x 4 rect of `mark` at the corner of its owner, and answers the questions of the base from its fields
    class UITestProbe : public UIComponent {
        BE_CLASS(UITestProbe)
    public:
        UITestProbe() = default;
        ~UITestProbe() override = default;

        BE_REFLECT_FIELD PoolString tag;
        BE_REFLECT_FIELD UIColor mark;
        BE_REFLECT_FIELD bool takesClicks = false;
        BE_REFLECT_FIELD bool hasContent = false;
        BE_REFLECT_FIELD bool fits = true;
        BE_REFLECT_FIELD bool canShrink = false;
        BE_REFLECT_FIELD bool checked = false;  ///< what `IsChecked` answers

        void DrawSelf(IRenderer& renderer, UIElement& owner) override;
        [[nodiscard]] UIContentFit CheckContent(const UIElement& owner, float width, float height, float scale) const override;
        void OnPress(UIElement& owner, float x, float y) override;
        void OnDrag(UIElement& owner, float x, float y) override;
        void OnClick(UIElement& owner) override;
        [[nodiscard]] bool MakesInteractive() const override {
            return takesClicks;
        }
        [[nodiscard]] bool GivesStateLook() const override {
            return takesClicks;
        }
        [[nodiscard]] bool IsChecked(const UIElement& /*owner*/) const override {
            return checked;
        }
    };

    /// Another type of component, so that an element holds two of different types
    class UITestOther : public UIComponent {
        BE_CLASS(UITestOther)
    public:
        UITestOther() = default;
        ~UITestOther() override = default;

        BE_REFLECT_FIELD PoolString tag;
        BE_REFLECT_FIELD UIColor mark;
        BE_REFLECT_FIELD bool hasContent = false;
        BE_REFLECT_FIELD bool fits = true;
        BE_REFLECT_FIELD bool canShrink = false;

        void DrawSelf(IRenderer& renderer, UIElement& owner) override;
        [[nodiscard]] UIContentFit CheckContent(const UIElement& owner, float width, float height, float scale) const override;
        void OnClick(UIElement& owner) override;
    };

}  // namespace BECore::Tests
