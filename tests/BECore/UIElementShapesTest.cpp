#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIElementShapes.h>
#include <BECore/UI/UIVariant.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;
        constexpr float kLiveWidth = 1280.0f;
        constexpr float kLiveHeight = 720.0f;
        constexpr float kCornerWidth = 100.0f;
        constexpr float kPortraitCornerWidth = 200.0f;
        constexpr float kTransitionSeconds = 1.0f;

        void ExpectRect(const Rect& actual, const Rect& expected) {
            EXPECT_NEAR(actual.x, expected.x, kTolerance);
            EXPECT_NEAR(actual.y, expected.y, kTolerance);
            EXPECT_NEAR(actual.w, expected.w, kTolerance);
            EXPECT_NEAR(actual.h, expected.h, kTolerance);
        }

        UILayoutContext Output(float width, float height) {
            UILayoutContext context;
            context.outputWidth = width;
            context.outputHeight = height;
            return context;
        }

        const PreviewDevice& DeviceWithCutout() {
            for (const PreviewDevice& device : UIPreview::GetDevices()) {
                if (device.cutout > 0.0f) {
                    return device;
                }
            }
            return UIPreview::GetDevices().front();
        }

        const PreviewDevice& PlainDevice() {
            for (const PreviewDevice& device : UIPreview::GetDevices()) {
                if (device.cutout == 0.0f && device.homeBar == 0.0f) {
                    return device;
                }
            }
            return UIPreview::GetDevices().front();
        }

        IntrusivePtr<UIElement> AddBox(UIElement& parent, const char* name, float x, float y, float width, float height) {
            auto box = parent.AddChild(PoolString::Intern(name));
            box->style.rect.x = UILength::Px(x);
            box->style.rect.y = UILength::Px(y);
            box->style.rect.width = UILength::Px(width);
            box->style.rect.height = UILength::Px(height);
            return box;
        }

        struct ScreenOptions {
            bool portraitVariant = false;  ///< `Corner` is wider while the canvas is a portrait
            bool transition = false;       ///< `Fader` is shown in landscape only, with a transition of a second
            CanvasScaleMode mode = CanvasScaleMode::ScaleWithScreen;
        };

        /// A HUD of every kind of element the shapes tell apart: a corner button, a bar between two anchors, a percent panel, an element under a panel and one that is hidden
        struct Screen {
            UICanvasLayout canvas;
            IntrusivePtr<UIElement> corner;
            IntrusivePtr<UIElement> bar;
            IntrusivePtr<UIElement> percent;
            IntrusivePtr<UIElement> inner;
            IntrusivePtr<UIElement> hidden;
            IntrusivePtr<UIElement> fader;

            explicit Screen(const ScreenOptions& options = {}) {
                canvas.params.scaleMode = options.mode;
                canvas.params.useSafeArea = true;
                corner = AddBox(*canvas.root, "Corner", -10.0f, -10.0f, kCornerWidth, 50.0f);
                corner->style.rect.anchor = UIAnchor::BottomRight;
                bar = AddBox(*canvas.root, "Bar", 0.0f, 0.0f, 0.0f, 40.0f);
                bar->style.rect.anchorMin = Vec2(0.1f, 1.0f);
                bar->style.rect.anchorMax = Vec2(0.9f, 1.0f);
                bar->style.rect.pivot = Vec2(0.5f, 1.0f);
                percent = AddBox(*canvas.root, "Percent", 0.0f, 0.0f, 0.0f, 0.0f);
                percent->style.rect.x = UILength::Percent(10.0f);
                percent->style.rect.y = UILength::Percent(10.0f);
                percent->style.rect.width = UILength::Percent(50.0f);
                percent->style.rect.height = UILength::Percent(20.0f);
                inner = AddBox(*percent, "Inner", 5.0f, 5.0f, 40.0f, 20.0f);
                hidden = AddBox(*canvas.root, "Hidden", 50.0f, 50.0f, 40.0f, 40.0f);
                hidden->style.visible = false;
                fader = AddBox(*canvas.root, "Fader", 300.0f, 10.0f, 60.0f, 30.0f);
                if (options.portraitVariant) {
                    UIVariant variant;
                    variant.when.orientation = UIOrientation::Portrait;
                    UIElementOverride wide;
                    wide.element = "Corner"_intern;
                    wide.width = UILength::Px(kPortraitCornerWidth);
                    variant.overrides.push_back(wide);
                    canvas.variants.push_back(variant);
                }
                if (options.transition) {
                    UIVariant variant;
                    variant.when.orientation = UIOrientation::Portrait;
                    UIElementOverride away;
                    away.element = "Fader"_intern;
                    away.visible = false;
                    variant.overrides.push_back(away);
                    canvas.variants.push_back(variant);
                    UITransitionComponent& transition = fader->AddComponent<UITransitionComponent>();
                    transition.duration = kTransitionSeconds;
                }
            }
        };

        /// Where @p name is in a canvas that was laid out for @p context and nothing else, in output pixels
        Optional<Rect> FreshRect(const ScreenOptions& options, const UILayoutContext& context, const char* name) {
            Screen fresh(options);
            fresh.canvas.Update(context);
            for (const ElementMark& mark : UIPreviewOverlay::ElementMarks(fresh.canvas)) {
                if (mark.name == PoolString::Intern(name)) {
                    return mark.rect;
                }
            }
            return {};
        }

        struct ElementState {
            Rect rect;
            UIRectSpec active;
            bool visible = false;
            bool inputLocked = false;
        };

        void Snapshot(const UIElement& element, Vector<ElementState>& states) {
            states.push_back({element.rect, element.active.rect, element.active.visible, element.inputLocked});
            for (const auto& child : element.children) {
                Snapshot(*child, states);
            }
        }

        Vector<ElementState> Snapshot(const UIElement& root) {
            Vector<ElementState> states;
            Snapshot(root, states);
            return states;
        }

        void ExpectSameStates(const Vector<ElementState>& expected, const Vector<ElementState>& actual) {
            ASSERT_EQ(expected.size(), actual.size());
            for (size_t i = 0; i < expected.size(); ++i) {
                ExpectRect(actual[i].rect, expected[i].rect);
                EXPECT_TRUE(actual[i].active == expected[i].active) << "element " << i;
                EXPECT_EQ(actual[i].visible, expected[i].visible) << "element " << i;
                EXPECT_EQ(actual[i].inputLocked, expected[i].inputLocked) << "element " << i;
            }
        }

        Vector<UILayoutContext> Contexts(const UILayoutContext& live, bool landscape) {
            Vector<UILayoutContext> contexts;
            for (const PreviewDevice& device : UIPreview::GetDevices()) {
                contexts.push_back(UIElementShapes::ContextFor(live, device, landscape));
            }
            return contexts;
        }

    }  // namespace

    TEST(UIElementShapesTest, TheContextOfADeviceHasItsSizeItsSafeAreaAndTheLiveRest) {
        UILayoutContext live = Output(kLiveWidth, kLiveHeight);
        live.pixelDensity = 2.0f;
        live.displayScale = 1.25f;
        live.userScale = 1.5f;
        live.sceneViewWidth = 1920.0f;
        live.sceneViewHeight = 1080.0f;
        live.worldBox = {1.0f, 2.0f, 3.0f, 4.0f};
        const PreviewDevice& device = DeviceWithCutout();

        const UILayoutContext portrait = UIElementShapes::ContextFor(live, device, false);

        EXPECT_FLOAT_EQ(portrait.outputWidth, static_cast<float>(device.width));
        EXPECT_FLOAT_EQ(portrait.outputHeight, static_cast<float>(device.height));
        EXPECT_FLOAT_EQ(portrait.pixelDensity, 1.0f);
        ExpectRect(portrait.safeArea, UIPreview::SimulatedSafeArea(SafeAreaSimulation::Device, &device, portrait.outputWidth, portrait.outputHeight));
        EXPECT_GT(portrait.safeArea.y, 0.0f);
        EXPECT_FLOAT_EQ(portrait.displayScale, 1.25f);
        EXPECT_FLOAT_EQ(portrait.userScale, 1.5f);
        EXPECT_FLOAT_EQ(portrait.sceneViewWidth, 1920.0f);
        EXPECT_FLOAT_EQ(portrait.sceneViewHeight, 1080.0f);
        ExpectRect(portrait.worldBox, live.worldBox);
    }

    TEST(UIElementShapesTest, LandscapeSwapsTheSidesAndTheInsets) {
        const UILayoutContext live = Output(kLiveWidth, kLiveHeight);
        const PreviewDevice& device = DeviceWithCutout();

        const UILayoutContext landscape = UIElementShapes::ContextFor(live, device, true);

        EXPECT_FLOAT_EQ(landscape.outputWidth, static_cast<float>(device.height));
        EXPECT_FLOAT_EQ(landscape.outputHeight, static_cast<float>(device.width));
        EXPECT_FLOAT_EQ(landscape.safeArea.x, device.cutout);
        EXPECT_FLOAT_EQ(landscape.safeArea.y, 0.0f);
    }

    TEST(UIElementShapesTest, TheRectOfEveryKindOfElementEqualsThatOfAFreshCanvasInThatShape) {
        for (const CanvasScaleMode mode : {CanvasScaleMode::ScaleWithScreen, CanvasScaleMode::ConstantPixelSize}) {
            for (const bool landscape : {false, true}) {
                ScreenOptions options;
                options.mode = mode;
                Screen screen(options);
                const UILayoutContext live = Output(kLiveWidth, kLiveHeight);
                screen.canvas.Update(live);
                const Vector<UILayoutContext> contexts = Contexts(live, landscape);
                const Array<const UIElement*, 4> probed = {screen.corner.Get(), screen.bar.Get(), screen.percent.Get(), screen.inner.Get()};
                const Array<const char*, 4> names = {"Corner", "Bar", "Percent", "Inner"};

                for (size_t which = 0; which < probed.size(); ++which) {
                    UIElementShapesResult result;
                    UIElementShapes::Collect(screen.canvas, *probed[which], contexts, result);

                    ASSERT_EQ(result.rects.size(), contexts.size());
                    for (size_t i = 0; i < contexts.size(); ++i) {
                        const Optional<Rect> expected = FreshRect(options, contexts[i], names[which]);
                        ASSERT_TRUE(expected.has_value());
                        ASSERT_TRUE(result.rects[i].has_value()) << names[which] << " in shape " << i;
                        ExpectRect(*result.rects[i], *expected);
                    }
                }
            }
        }
    }

    TEST(UIElementShapesTest, ADeviceWithACutOutMovesTheCanvasContentAndAPlainOneDoesNot) {
        Screen screen;
        const UILayoutContext live = Output(kLiveWidth, kLiveHeight);
        screen.canvas.Update(live);
        const Array<UILayoutContext, 2> contexts = {UIElementShapes::ContextFor(live, DeviceWithCutout(), false), UIElementShapes::ContextFor(live, PlainDevice(), false)};

        UIElementShapesResult result;
        UIElementShapes::Collect(screen.canvas, *screen.bar, contexts, result);

        ASSERT_TRUE(result.rects[0].has_value());
        ASSERT_TRUE(result.rects[1].has_value());
        const Optional<Rect> cutOut = FreshRect({}, contexts[0], "Bar");
        const Optional<Rect> plain = FreshRect({}, contexts[1], "Bar");
        ExpectRect(*result.rects[0], *cutOut);
        ExpectRect(*result.rects[1], *plain);
    }

    TEST(UIElementShapesTest, AVariantThatMatchesOnlyOneShapeIsChosenForThatShape) {
        ScreenOptions options;
        options.portraitVariant = true;
        Screen screen(options);
        const UILayoutContext live = Output(kLiveWidth, kLiveHeight);
        screen.canvas.Update(live);
        const PreviewDevice& device = PlainDevice();
        const Array<UILayoutContext, 2> contexts = {UIElementShapes::ContextFor(live, device, false), UIElementShapes::ContextFor(live, device, true)};

        UIElementShapesResult result;
        UIElementShapes::Collect(screen.canvas, *screen.corner, contexts, result);

        ASSERT_TRUE(result.rects[0].has_value());
        ASSERT_TRUE(result.rects[1].has_value());
        const float portraitScale = UICanvasScaler::Resolve(screen.canvas.params, contexts[0]).scale;
        const float landscapeScale = UICanvasScaler::Resolve(screen.canvas.params, contexts[1]).scale;
        EXPECT_NEAR(result.rects[0]->w, kPortraitCornerWidth * portraitScale, kTolerance);
        EXPECT_NEAR(result.rects[1]->w, kCornerWidth * landscapeScale, kTolerance);
        ExpectRect(*result.rects[0], *FreshRect(options, contexts[0], "Corner"));
        ExpectRect(*result.rects[1], *FreshRect(options, contexts[1], "Corner"));
    }

    TEST(UIElementShapesTest, TheLiveCanvasIsTheSameAfterTheProbeAndLaysNothingOutAgain) {
        ScreenOptions options;
        options.portraitVariant = true;
        options.transition = true;
        Screen screen(options);
        const UILayoutContext live = Output(kLiveWidth, kLiveHeight);
        screen.canvas.Update(live);
        const Vector<ElementState> before = Snapshot(*screen.canvas.root);
        const Vector<UILayoutContext> contexts = Contexts(live, false);

        UIElementShapesResult result;
        UIElementShapes::Collect(screen.canvas, *screen.inner, contexts, result);

        ExpectSameStates(before, Snapshot(*screen.canvas.root));
        EXPECT_TRUE(screen.canvas.GetContext() == live);
        const uint32_t layoutRuns = screen.canvas.GetLayoutRuns();
        const uint32_t variantRuns = screen.canvas.GetVariantRuns();
        EXPECT_FALSE(screen.canvas.Update(live));
        EXPECT_EQ(screen.canvas.GetLayoutRuns(), layoutRuns);
        EXPECT_EQ(screen.canvas.GetVariantRuns(), variantRuns);
    }

    TEST(UIElementShapesTest, AShapeThatHidesAnElementWithATransitionStartsNoTransitionOfTheLiveOne) {
        ScreenOptions options;
        options.transition = true;
        Screen screen(options);
        const UILayoutContext live = Output(kLiveWidth, kLiveHeight);
        screen.canvas.Update(live);
        UITransitionComponent& transition = *screen.fader->GetComponent<UITransitionComponent>();
        ASSERT_TRUE(transition.run.TargetVisible());
        const Rect liveRect = screen.fader->rect;
        const Array<UILayoutContext, 2> contexts = {UIElementShapes::ContextFor(live, PlainDevice(), false), UIElementShapes::ContextFor(live, PlainDevice(), true)};

        UIElementShapesResult result;
        UIElementShapes::Collect(screen.canvas, *screen.fader, contexts, result);

        EXPECT_FALSE(result.rects[0].has_value());  // portrait: the variant hides it, and it is not "leaving"
        EXPECT_TRUE(result.rects[1].has_value());
        EXPECT_TRUE(transition.run.TargetVisible());
        EXPECT_FALSE(transition.run.IsTransitioning(screen.canvas.time));
        EXPECT_FALSE(transition.held);
        EXPECT_TRUE(screen.fader->active.visible);
        ExpectRect(screen.fader->rect, liveRect);
    }

    TEST(UIElementShapesTest, AHiddenElementAndOneThatIsNotInTheTreeGiveNone) {
        Screen screen;
        const UILayoutContext live = Output(kLiveWidth, kLiveHeight);
        screen.canvas.Update(live);
        const Vector<UILayoutContext> contexts = Contexts(live, false);
        const uint32_t layoutRuns = screen.canvas.GetLayoutRuns();
        UIElementShapesResult result;

        UIElementShapes::Collect(screen.canvas, *screen.hidden, contexts, result);
        ASSERT_EQ(result.rects.size(), contexts.size());
        for (const Optional<Rect>& rect : result.rects) {
            EXPECT_FALSE(rect.has_value());
        }

        const auto stranger = New<UIElement>();
        UIElementShapes::Collect(screen.canvas, *stranger, contexts, result);
        for (const Optional<Rect>& rect : result.rects) {
            EXPECT_FALSE(rect.has_value());
        }
        EXPECT_EQ(screen.canvas.GetLayoutRuns(), layoutRuns + static_cast<uint32_t>(contexts.size()) + 1);  // the first probe only: the stranger never touched the canvas
    }

    TEST(UIElementShapesTest, ACanvasThatHasNoLayoutOrIsNotAScreenIsNotProbed) {
        Screen unlaidOut;
        Screen world;
        world.canvas.params.space = CanvasSpace::World;
        const UILayoutContext live = Output(kLiveWidth, kLiveHeight);
        world.canvas.Update(live);
        const uint32_t layoutRuns = world.canvas.GetLayoutRuns();
        const Vector<UILayoutContext> contexts = Contexts(live, false);
        UIElementShapesResult result;

        UIElementShapes::Collect(unlaidOut.canvas, *unlaidOut.corner, contexts, result);
        ASSERT_EQ(result.rects.size(), contexts.size());
        EXPECT_FALSE(result.rects[0].has_value());
        EXPECT_FALSE(unlaidOut.canvas.HasLayout());

        UIElementShapes::Collect(world.canvas, *world.corner, contexts, result);
        ASSERT_EQ(result.rects.size(), contexts.size());
        EXPECT_FALSE(result.rects[0].has_value());
        EXPECT_EQ(world.canvas.GetLayoutRuns(), layoutRuns);
    }

    TEST(UIElementShapesTest, AnElementUnderARegionIsApproximateAndOthersAreNot) {
        Screen screen;
        screen.canvas.regions.Set("Zone"_intern, {100.0f, 100.0f, 400.0f, 300.0f});
        screen.percent->style.rect.region = "Zone"_intern;
        const UILayoutContext live = Output(kLiveWidth, kLiveHeight);
        screen.canvas.Update(live);
        const Array<UILayoutContext, 1> contexts = {UIElementShapes::ContextFor(live, PlainDevice(), false)};
        UIElementShapesResult result;

        UIElementShapes::Collect(screen.canvas, *screen.percent, contexts, result);
        EXPECT_TRUE(result.approximate);
        UIElementShapes::Collect(screen.canvas, *screen.inner, contexts, result);
        EXPECT_TRUE(result.approximate);
        UIElementShapes::Collect(screen.canvas, *screen.corner, contexts, result);
        EXPECT_FALSE(result.approximate);
    }

    TEST(UIElementShapesTest, TheCacheProbesOnceForASteadyKeyAndAgainWhenTheKeyMoves) {
        Screen screen;
        const UILayoutContext live = Output(kLiveWidth, kLiveHeight);
        screen.canvas.Update(live);
        const Vector<UILayoutContext> contexts = Contexts(live, false);
        UIElementShapesCache cache;

        EXPECT_TRUE(cache.Get(screen.canvas, *screen.corner, {}).rects.empty());
        EXPECT_EQ(cache.GetProbeRuns(), 0u);  // no shape checked: nothing probed

        const size_t count = cache.Get(screen.canvas, *screen.corner, contexts).rects.size();
        EXPECT_EQ(count, contexts.size());
        EXPECT_EQ(cache.GetProbeRuns(), 1u);
        screen.canvas.Update(live);  // a steady frame
        cache.Get(screen.canvas, *screen.corner, contexts);
        EXPECT_EQ(cache.GetProbeRuns(), 1u);

        screen.corner->style.rect.width = UILength::Px(kCornerWidth * 2.0f);  // an edit of the element
        screen.canvas.Invalidate();
        screen.canvas.Update(live);
        const UIElementShapesResult& edited = cache.Get(screen.canvas, *screen.corner, contexts);
        EXPECT_EQ(cache.GetProbeRuns(), 2u);
        EXPECT_NEAR(edited.rects[0]->w, FreshRect({}, contexts[0], "Corner")->w * 2.0f, kTolerance);  // the fresh canvas has the old width

        cache.Get(screen.canvas, *screen.corner, Span<const UILayoutContext>(contexts.data(), contexts.size() - 1));  // another set of shapes
        EXPECT_EQ(cache.GetProbeRuns(), 3u);
        cache.Get(screen.canvas, *screen.bar, Span<const UILayoutContext>(contexts.data(), contexts.size() - 1));  // another element
        EXPECT_EQ(cache.GetProbeRuns(), 4u);
        cache.Get(screen.canvas, *screen.bar, Span<const UILayoutContext>(contexts.data(), contexts.size() - 1));
        EXPECT_EQ(cache.GetProbeRuns(), 4u);

        cache.Reset();
        cache.Get(screen.canvas, *screen.bar, Span<const UILayoutContext>(contexts.data(), contexts.size() - 1));
        EXPECT_EQ(cache.GetProbeRuns(), 5u);
    }

    TEST(UIElementShapesTest, ARectOfAPhoneIsDrawnAsTheSameFractionsOfTheImage) {
        const UILayoutContext phone = Output(390.0f, 844.0f);
        const Optional<Rect> fitted = UIElementShapes::FitToImage({39.0f, 84.4f, 195.0f, 422.0f}, phone, 1920.0f, 1080.0f);

        ASSERT_TRUE(fitted.has_value());
        ExpectRect(*fitted, {192.0f, 108.0f, 960.0f, 540.0f});
    }

    TEST(UIElementShapesTest, AShapeOrAnImageWithoutAreaHasNoFittedRect) {
        const Rect rect{1.0f, 2.0f, 3.0f, 4.0f};

        EXPECT_FALSE(UIElementShapes::FitToImage(rect, Output(0.0f, 844.0f), 1920.0f, 1080.0f).has_value());
        EXPECT_FALSE(UIElementShapes::FitToImage(rect, Output(390.0f, 0.0f), 1920.0f, 1080.0f).has_value());
        EXPECT_FALSE(UIElementShapes::FitToImage(rect, Output(390.0f, 844.0f), 0.0f, 1080.0f).has_value());
        EXPECT_FALSE(UIElementShapes::FitToImage(rect, Output(390.0f, 844.0f), 1920.0f, 0.0f).has_value());
    }

}  // namespace BECore::Tests
