// UIDerivedScreen: the pattern of a screen on Derived values (one Context kept as a member, a Get of each value per frame, no key of its own)
// rebuilds exactly when an input of a value moved and allocates nothing in a steady frame.

#include "UIDerivedScreenTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UIVars.h>
#include <Common/AllocCheck.h>
#include <Common/UITestSupport.h>
#include <Events/SceneEvents.h>
#include <Generated/UIDerivedScreenTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kLayoutColumns = 4.0f;
        constexpr float kLayoutRows = 10.0f;

        constexpr const char* kScreenXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
      <component type="UIDerivedScreen"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr float kWideWidth = 800.0f;
        constexpr float kWideHeight = 600.0f;
        constexpr float kNarrowWidth = 400.0f;

        constexpr size_t kTitleReserve = 64;

    }  // namespace

    void UIDerivedScreen::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &UIDerivedScreen::OnUpdate>(this);

        const IntrusivePtr<UICanvasComponent> canvas = GetNode().GetComponent<UICanvasComponent>();
        ASSERT(canvas);

        const auto ambient = UIAmbient::Declare(_declaration);
        ASSERT(ambient.has_value());
        _ambient = ambient.value_or(UIAmbientLevels{});
        const auto score = _declaration.Add<UIScreenScore>("score");
        const auto mood = _declaration.Add<UIScreenMood>("mood");
        ASSERT(score.has_value() && mood.has_value());

        const bool linked = _language.Link(_declaration, "language.language").has_value() && _points.Link(_declaration, "score.points").has_value() &&
                            _livesLeft.Link(_declaration, "score.lives").has_value() && _outputWidth.Link(_declaration, "window.outputWidth").has_value() &&
                            _outputHeight.Link(_declaration, "window.outputHeight").has_value();
        ASSERT(linked);

        // The context is filled here and again only when an object is replaced; the counters move on their own
        _context = Context(_declaration);
        UIAmbient::Fill(_context, _ambient, CoreManager::GetUIStrings(), canvas->GetWindowView(), canvas->GetWindowVersion());
        _context.Set(*score, _score, &_scoreVersion);
        _context.Set(*mood, _mood, &_moodVersion);

        DerivedReads titleReads;
        titleReads.Add(_language);
        titleReads.Add(_points);
        _title.Reset(titleReads);
        DerivedReads rowsReads;
        rowsReads.Add(_points);
        rowsReads.Add(_livesLeft);
        _rows.Reset(rowsReads);
        DerivedReads layoutReads;
        layoutReads.Add(_outputWidth);
        layoutReads.Add(_outputHeight);
        _layout.Reset(layoutReads);
    }

    void UIDerivedScreen::SetPoints(int32_t points) {
        _score.points = points;
        ++_scoreVersion;
    }

    void UIDerivedScreen::SetLives(int32_t lives) {
        _score.lives = lives;
        ++_scoreVersion;
    }

    void UIDerivedScreen::SetMood(int32_t calm) {
        _mood.calm = calm;
        ++_moodVersion;
    }

    void UIDerivedScreen::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        _titleText = &_title.Get(_context, [this](String& out, const Context& view) {
            out.reserve(kTitleReserve);
            out.clear();
            const StringView language = _language.Get(view).ToStringView();
            AppendFormat(out, "{}: {}", language, _points.Get(view));
        });
        _rowValues = &_rows.Get(_context, [this](Vector<int32_t>& out, const Context& view) {
            out.reserve(kMaxRows);
            out.clear();
            const int32_t points = _points.Get(view);
            const int32_t lives = _livesLeft.Get(view);
            for (int32_t row = 0; row < lives && row < kMaxRows; ++row) {
                out.push_back(points * (row + 1));
            }
        });
        _layoutValue = &_layout.Get(_context, [this](UIScreenLayout& out, const Context& view) {
            out.columnWidth = _outputWidth.Get(view) / kLayoutColumns;
            out.rowHeight = _outputHeight.Get(view) / kLayoutRows;
        });
    }

    class UIDerivedScreenTest : public UISceneFixture {
    protected:
        void SetUp() override {
            UISceneFixture::SetUp();
            ASSERT_NO_FATAL_FAILURE(LoadScene(kScreenXml));
            ASSERT_TRUE(_canvas);
            UIStrings& strings = CoreManager::GetUIStrings();
            strings.Set("en"_intern, "menu.start", "Start");
            strings.Set("ru"_intern, "menu.start", "Start (ru)");
            strings.SetLanguage("en"_intern);
            _canvas->UpdateLayout(kWideWidth, kWideHeight);
            _screen = _canvas->GetNode().GetComponent<UIDerivedScreen>();
            ASSERT_TRUE(_screen);
        }

        void TearDown() override {
            _screen.Reset();
            UISceneFixture::TearDown();
        }

        void Frame() {
            _manager->UpdateAll();
        }

        /// The rebuild counts of the title, the rows and the layout, in that order
        struct Counts {
            uint32_t title = 0;
            uint32_t rows = 0;
            uint32_t layout = 0;

            [[nodiscard]] bool operator==(const Counts&) const = default;
        };

        [[nodiscard]] Counts Rebuilds() const {
            return {_screen->TitleRebuilds(), _screen->RowsRebuilds(), _screen->LayoutRebuilds()};
        }

        IntrusivePtr<UIDerivedScreen> _screen;
    };

    TEST_F(UIDerivedScreenTest, TheFirstFrameBuildsEachValueOnceFromTheInputs) {
        Frame();

        EXPECT_EQ(Rebuilds(), (Counts{1, 1, 1}));
        ASSERT_NE(_screen->Title(), nullptr);
        EXPECT_EQ(*_screen->Title(), "en: 0");
        EXPECT_EQ(*_screen->Rows(), (Vector<int32_t>{0, 0, 0})) << "three lives, no points";
        EXPECT_FLOAT_EQ(_screen->Layout()->columnWidth, kWideWidth / kLayoutColumns);
        EXPECT_FLOAT_EQ(_screen->Layout()->rowHeight, kWideHeight / kLayoutRows);
    }

    TEST_F(UIDerivedScreenTest, EachValueRebuildsExactlyWhenAnInputOfItsOwnMoved) {
        Frame();
        ASSERT_EQ(Rebuilds(), (Counts{1, 1, 1}));

        Frame();
        EXPECT_EQ(Rebuilds(), (Counts{1, 1, 1})) << "a frame where nothing moved";

        _screen->SetPoints(30);
        Frame();
        EXPECT_EQ(Rebuilds(), (Counts{2, 2, 1})) << "the score is read by the title and the rows, not by the layout";
        EXPECT_EQ(*_screen->Title(), "en: 30");
        ASSERT_EQ(_screen->Rows()->size(), 3U);
        EXPECT_EQ((*_screen->Rows())[2], 90);

        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("ru"_intern));
        Frame();
        EXPECT_EQ(Rebuilds(), (Counts{3, 2, 1})) << "the language is read by the title only";
        EXPECT_EQ(*_screen->Title(), "ru: 30");

        _canvas->UpdateLayout(kNarrowWidth, kWideHeight);
        Frame();
        EXPECT_EQ(Rebuilds(), (Counts{3, 2, 2})) << "the window is read by the layout only";
        EXPECT_FLOAT_EQ(_screen->Layout()->columnWidth, kNarrowWidth / kLayoutColumns);

        _canvas->UpdateLayout(kNarrowWidth, kWideHeight);
        Frame();
        EXPECT_EQ(Rebuilds(), (Counts{3, 2, 2})) << "the same size laid out again is no change";

        _screen->SetMood(5);
        CoreManager::GetUIVars().Set("unrelated"_intern, 1);
        Frame();
        EXPECT_EQ(Rebuilds(), (Counts{3, 2, 2})) << "an object nobody reads and a variable are not inputs";

        _screen->SetLives(1);
        Frame();
        EXPECT_EQ(Rebuilds(), (Counts{4, 3, 2})) << "versions are per level: a field of the score moves every value that reads the score";
        ASSERT_EQ(_screen->Rows()->size(), 1U);
        EXPECT_EQ((*_screen->Rows())[0], 30);
    }

    TEST_F(UIDerivedScreenTest, ASteadyFrameThroughTheSceneAllocatesNothing) {
        _screen->SetPoints(12);
        Frame();
        ASSERT_EQ(Rebuilds(), (Counts{1, 1, 1}));

        ExpectNoAllocations([&] {
            for (int frame = 0; frame < 10; ++frame) {
                Frame();
            }
        });
        EXPECT_EQ(Rebuilds(), (Counts{1, 1, 1}));
    }

    TEST_F(UIDerivedScreenTest, ARebuildThatFitsTheReservedCapacityAllocatesNothing) {
        Frame();
        _screen->SetPoints(7);
        Frame();
        ASSERT_EQ(Rebuilds(), (Counts{2, 2, 1}));

        _screen->SetPoints(500);
        _screen->SetLives(UIDerivedScreen::kMaxRows);
        ExpectNoAllocations([&] { Frame(); });
        EXPECT_EQ(Rebuilds(), (Counts{3, 3, 1}));
        EXPECT_EQ(*_screen->Title(), "en: 500");
        EXPECT_EQ(_screen->Rows()->size(), static_cast<size_t>(UIDerivedScreen::kMaxRows));
    }

}  // namespace BECore::Tests
