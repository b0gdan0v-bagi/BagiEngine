#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIAmbient.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Derived/Derived.h>
#include <BECore/Values/Value.h>
#include <cstdint>

namespace BECore {

    class UICanvasComponent;

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }  // namespace SceneEvents

}  // namespace BECore

namespace BECore::Tests {

    /// The object the screen reads at its level `score`; its owner (the screen) bumps the counter on a change
    struct UIScreenScore {
        BE_CLASS(UIScreenScore)

        BE_REFLECT_FIELD int32_t points = 0;
        BE_REFLECT_FIELD int32_t lives = 3;
    };

    /// An object at a level that no derived value of the screen reads
    struct UIScreenMood {
        BE_CLASS(UIScreenMood)

        BE_REFLECT_FIELD int32_t calm = 0;
    };

    /// What a screen computes from the window size
    struct UIScreenLayout {
        float columnWidth = 0.0f;
        float rowHeight = 0.0f;

        [[nodiscard]] bool operator==(const UIScreenLayout&) const = default;
    };

    /**
     * @brief A screen the way a game writes one: a declaration and a `Context` made once, three `Derived` values, one `Get` of each per frame
     *
     * The title reads the score and the language, the rows read the score, the layout reads the window; the mood is in the context and
     * in nobody's reads. The frame hands out no key and rebuilds no context.
     */
    class UIDerivedScreen : public IComponent {
        BE_CLASS(UIDerivedScreen)
    public:
        UIDerivedScreen() = default;
        ~UIDerivedScreen() override = default;

        void OnAttached() override;

        void SetPoints(int32_t points);
        void SetLives(int32_t lives);
        void SetMood(int32_t calm);

        /// What the last frame's `Get` returned; null before the first frame
        [[nodiscard]] const String* Title() const {
            return _titleText;
        }
        [[nodiscard]] const Vector<int32_t>* Rows() const {
            return _rowValues;
        }
        [[nodiscard]] const UIScreenLayout* Layout() const {
            return _layoutValue;
        }

        [[nodiscard]] uint32_t TitleRebuilds() const {
            return _title.Rebuilds();
        }
        [[nodiscard]] uint32_t RowsRebuilds() const {
            return _rows.Rebuilds();
        }
        [[nodiscard]] uint32_t LayoutRebuilds() const {
            return _layout.Rebuilds();
        }

        /// Most rows a screen shows: the capacity its list reserves, so a rebuild within it allocates nothing
        static constexpr int32_t kMaxRows = 8;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);

        ContextDecl _declaration;
        Context _context;
        UIAmbientLevels _ambient;
        UIScreenScore _score;
        uint64_t _scoreVersion = 1;
        UIScreenMood _mood;
        uint64_t _moodVersion = 1;

        Value<PoolString> _language;
        Value<int32_t> _points;
        Value<int32_t> _livesLeft;
        Value<float> _outputWidth;
        Value<float> _outputHeight;

        Derived<String> _title{DerivedReads{}};
        Derived<Vector<int32_t>> _rows{DerivedReads{}};
        Derived<UIScreenLayout> _layout{DerivedReads{}};

        // The results live in the `Derived` members: a pointer, not a copy, so a frame allocates nothing
        const String* _titleText = nullptr;
        const Vector<int32_t>* _rowValues = nullptr;
        const UIScreenLayout* _layoutValue = nullptr;
    };

}  // namespace BECore::Tests
