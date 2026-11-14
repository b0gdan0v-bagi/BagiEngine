#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UIPanel.h>
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIWebCompiler.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <RmlUi/Core.h>
#include <RmlUiHost/RmlUiDocumentComponent.h>
#include <RmlUiHost/RmlUiRuntime.h>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif

namespace BECore::Tests {

    namespace {

        // The same screen in the three authoring formats: 250 cards of 4 elements (a plate, two labels and a button) under one root, 1,001 elements,
        // placed with absolute offsets so that every format does the same layout work. A third of the source is the offsets.
        // All of it is on the screen (25 columns of 144 units and 10 rows of 84 fit 3840 x 2160): RmlUi does not draw what is outside the window and the engine's tree does, so a
        // screen that is partly off would compare the culling and not the formats
        constexpr int kCards = 250;
        constexpr int kColumns = 25;
        constexpr int kCardWidth = 140;
        constexpr int kCardHeight = 80;
        constexpr int kGap = 4;
        constexpr int kBenchRepeats = 5;
        constexpr float kOutputWidth = 3840.0f;
        constexpr float kOutputHeight = 2160.0f;

        struct Place {
            int x;
            int y;
        };

        Place CardPlace(int index) {
            return {(index % kColumns) * (kCardWidth + kGap), (index / kColumns) * (kCardHeight + kGap)};
        }

        std::string XmlScreen() {
            std::string text = "<ui><elements>\n";
            for (int i = 0; i < kCards; ++i) {
                const auto [x, y] = CardPlace(i);
                text += Format("<element type=\"UIPanel\" name=\"c{0}\" x=\"{1}\" y=\"{2}\" width=\"140\" height=\"80\" color=\"30,30,40,255\"/>\n"
                               "<element type=\"UIElement\" name=\"t{0}\" x=\"{3}\" y=\"{4}\" width=\"124\" height=\"22\" fontSize=\"18\"><components>"
                               "<component type=\"UITextComponent\" text=\"Card {0}\"/></components></element>\n"
                               "<element type=\"UIElement\" name=\"v{0}\" x=\"{3}\" y=\"{5}\" width=\"124\" height=\"20\" fontSize=\"14\"><components>"
                               "<component type=\"UITextComponent\" text=\"Price {0}\" textColor=\"190,190,190,255\"/></components></element>\n"
                               "<element type=\"UIElement\" name=\"b{0}\" x=\"{3}\" y=\"{6}\" width=\"56\" height=\"24\" fontSize=\"14\" color=\"40,90,60,255\"><components>"
                               "<component type=\"UITextComponent\" text=\"Buy\" align=\"Center\" verticalAlign=\"Center\" wrap=\"false\"/><component type=\"UIInputClicker\" "
                               "action=\"Buy\"/></components></element>\n",
                               i, x, y, x + 8, y + 6, y + 30, y + 52)
                            .c_str();
            }
            text += "</elements></ui>";
            return text;
        }

        std::string HtmlBody(const char* unit) {
            std::string text;
            for (int i = 0; i < kCards; ++i) {
                const auto [x, y] = CardPlace(i);
                text += Format("<div id=\"c{0}\" class=\"card\" style=\"left: {1}{7}; top: {2}{7}\"></div>\n"
                               "<span id=\"t{0}\" class=\"title\" style=\"left: {3}{7}; top: {4}{7}\">Card {0}</span>\n"
                               "<span id=\"v{0}\" class=\"price\" style=\"left: {3}{7}; top: {5}{7}\">Price {0}</span>\n"
                               "<button id=\"b{0}\" data-action=\"Buy\" style=\"left: {3}{7}; top: {6}{7}\">Buy</button>\n",
                               i, x, y, x + 8, y + 6, y + 30, y + 52, unit)
                            .c_str();
            }
            return text;
        }

        // B reads px and a `vertical-align`; C reads dp and a `line-height`
        constexpr const char* kWebCss = R"(
.card  { width: 140px; height: 80px; background: rgb(30, 30, 40); }
.title { width: 124px; height: 22px; font-size: 18px; }
.price { width: 124px; height: 20px; font-size: 14px; color: rgb(190, 190, 190); }
button { width: 56px; height: 24px; font-size: 14px; background: rgb(40, 90, 60); }
)";

        constexpr const char* kRmlCss = R"(
body { width: 100%; height: 100%; font-family: "PT Sans"; color: white; pointer-events: none; }
div, span, button { position: absolute; }
.card  { width: 140dp; height: 80dp; background-color: rgb(30, 30, 40); }
.title { width: 124dp; height: 22dp; font-size: 18dp; }
.price { width: 124dp; height: 20dp; font-size: 14dp; color: rgb(190, 190, 190); }
button { width: 56dp; height: 24dp; font-size: 14dp; background-color: rgb(40, 90, 60); pointer-events: auto; }
)";

        std::string WebScreen() {
            return std::string("<style>") + kWebCss + "</style>\n" + HtmlBody("px");
        }

        std::string RmlScreen() {
            return std::string("<rml><head><style>") + kRmlCss + "</style></head><body>\n" + HtmlBody("dp") + "</body></rml>";
        }

        using Clock = std::chrono::steady_clock;

        double MillisecondsSince(Clock::time_point start) {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        template <typename Fn>
        double BestOf(int repeats, Fn&& fn) {
            double best = 1e300;
            for (int i = 0; i < repeats; ++i) {
                const auto start = Clock::now();
                fn(i);
                best = std::min(best, MillisecondsSince(start));
            }
            return best;
        }

        /// Bytes of the heap in use, or 0 where the platform call is not there
        size_t HeapInUse() {
#if defined(__APPLE__)
            malloc_statistics_t stats{};
            malloc_zone_statistics(nullptr, &stats);
            return stats.size_in_use;
#else
            return 0;
#endif
        }

        void DrawElements(IRenderer& renderer, UIElement& element) {
            if (!element.active.visible) {
                return;
            }
            element.DrawSelf(renderer);
            for (const auto& child : element.children) {
                DrawElements(renderer, *child);
            }
        }

        struct Row {
            const char* what;
            double a;
            double b;
            double c;
        };

    }  // namespace

    /// Not an assertion of speed (a timing test would be flaky): it builds one screen three ways, measures each and prints the table that
    /// `src/Modules/BECore/UI/AuthoringDecision.md` quotes. The asserts only check that the three screens are the same size and drew something.
    class UIAuthoringBenchmarkTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(UIAuthoringBenchmarkTest, OneScreenOfAThousandElementsInThreeFormats) {
        auto renderer = New<RecordingRenderer>();
        renderer->width = kOutputWidth;
        renderer->height = kOutputHeight;
        CoreManager::GetRendererManager().SetRenderer(renderer);
        // Same fonts for all three: warm the default font before anything is timed, so the first draw of each does not pay for RmlUi's start-up or the file read twice
        EngineEnvironment::EnsureCoreServices();

        const std::string xml = XmlScreen();
        const std::string web = WebScreen();
        const std::string rml = RmlScreen();

        std::vector<Row> rows;
        rows.push_back({"source bytes", static_cast<double>(xml.size()), static_cast<double>(web.size()), static_cast<double>(rml.size())});

        // ---- A: the engine's XML ----
        UICanvasLayout layoutA;
        UIScreenContent contentA;
        const size_t heapBeforeA = HeapInUse();
        const auto startA = Clock::now();
        {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(FromStd(xml)));
            contentA.Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
        }
        const double loadA = MillisecondsSince(startA);
        layoutA.root->children = contentA.elements;
        layoutA.params.space = CanvasSpace::Screen;
        layoutA.params.scaleMode = CanvasScaleMode::ConstantPixelSize;
        layoutA.Invalidate();
        const size_t heapAfterA = HeapInUse();

        // ---- B: HTML + CSS compiled to the XML ----
        UIWebReport report;
        const auto startB = Clock::now();
        const eastl::string compiled = UIWebCompiler::Compile(FromStd(web), {}, report);
        const double compileB = MillisecondsSince(startB);
        UIScreenContent contentB;
        const auto startBLoad = Clock::now();
        {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(compiled));
            contentB.Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
        }
        const double loadB = compileB + MillisecondsSince(startBLoad);
        EXPECT_TRUE(report.issues.empty()) << "the HTML + CSS screen left something out";

        // ---- C: RmlUi ----
        _manager = New<SceneManager>();
        {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(R"(<SceneManager><scenes><scene type="Scene" name="S"><nodes><node name="Doc"><components>
<component type="RmlUiDocumentComponent" space="Screen" scaleMode="ConstantPixelSize"/></components></node></nodes></scene></scenes></SceneManager>)"));
            _manager->Deserialize(deserializer);
            _manager->Initialize();
            _manager->SetActiveScene("S"_intern);
        }
        auto doc = _manager->GetSceneByName("S"_intern)->GetRootNode().GetChildren()[0]->GetComponent<RmlUiDocumentComponent>();
        ASSERT_TRUE(doc);
        const size_t heapBeforeC = HeapInUse();
        const auto startC = Clock::now();
        ASSERT_TRUE(doc->SetMarkup(eastl::string(rml.data(), rml.size())));
        const double loadC = MillisecondsSince(startC);
        const double firstLayoutC = BestOf(1, [&](int) { doc->UpdateLayout(kOutputWidth, kOutputHeight); });
        const size_t heapAfterC = HeapInUse();

        Rml::ElementList all;
        doc->GetDocument()->QuerySelectorAll(all, "*");
        EXPECT_GE(all.size(), 1000u) << "the RML screen has the thousand elements";
        EXPECT_EQ(contentA.elements.size(), contentB.elements.size());

        rows.push_back({"load: parse and build (ms)", loadA, loadB, loadC});

        // ---- layout ----
        UILayoutContext contextA;
        contextA.outputWidth = kOutputWidth;
        contextA.outputHeight = kOutputHeight;
        const double firstLayoutA = BestOf(1, [&](int) { layoutA.Update(contextA); });
        UICanvasLayout layoutB;
        layoutB.root->children = contentB.elements;
        layoutB.params = layoutA.params;
        layoutB.Invalidate();
        const double firstLayoutB = BestOf(1, [&](int) { layoutB.Update(contextA); });
        rows.push_back({"layout, first (ms)", firstLayoutA, firstLayoutB, firstLayoutC});

        const double relayoutA = BestOf(kBenchRepeats, [&](int i) {
            UILayoutContext resized = contextA;
            resized.outputWidth = kOutputWidth + static_cast<float>(i + 1) * 17.0f;
            layoutA.Update(resized);
        });
        const double relayoutC = BestOf(kBenchRepeats, [&](int i) { doc->UpdateLayout(kOutputWidth + static_cast<float>(i + 1) * 17.0f, kOutputHeight); });
        rows.push_back({"layout, window resized (ms)", relayoutA, relayoutA, relayoutC});

        layoutA.Update(contextA);
        const double cachedA = BestOf(kBenchRepeats, [&](int) { layoutA.Update(contextA); });
        doc->UpdateLayout(kOutputWidth, kOutputHeight);
        const double cachedC = BestOf(kBenchRepeats, [&](int) { doc->UpdateLayout(kOutputWidth, kOutputHeight); });
        rows.push_back({"layout, nothing changed (ms)", cachedA, cachedA, cachedC});

        // ---- draw ----
        renderer->SetViewTransform(layoutA.GetResolved().transform);
        const double firstDrawA = BestOf(1, [&](int) {
            renderer->ClearCalls();
            DrawElements(*renderer, *layoutA.root);
        });
        const double drawA = BestOf(kBenchRepeats, [&](int) {
            renderer->ClearCalls();
            DrawElements(*renderer, *layoutA.root);
        });
        const size_t callsA = renderer->calls.size();
        renderer->SetViewTransform({});

        RmlUiRenderInterface& render = RmlUiRuntime::GetInstance().GetRenderInterface();
        const double firstDrawC = BestOf(1, [&](int) {
            renderer->ClearCalls();
            _manager->DrawAll();
        });
        render.ResetStats();
        const double drawC = BestOf(kBenchRepeats, [&](int) {
            renderer->ClearCalls();
            render.ResetStats();
            _manager->DrawAll();
        });
        const size_t callsC = renderer->calls.size();
        const uint32_t verticesC = render.GetStats().verticesDrawn;
        rows.push_back({"draw, first: glyphs are drawn (ms)", firstDrawA, firstDrawA, firstDrawC});
        rows.push_back({"draw, warm (ms)", drawA, drawA, drawC});
        rows.push_back({"draw calls", static_cast<double>(callsA), static_cast<double>(callsA), static_cast<double>(callsC)});
        rows.push_back({"heap of the screen (KB, B is A's tree)", static_cast<double>(heapAfterA - heapBeforeA) / 1024.0, static_cast<double>(heapAfterA - heapBeforeA) / 1024.0,
                        static_cast<double>(heapAfterC - heapBeforeC) / 1024.0});

#ifdef NDEBUG
        constexpr const char* kBuildType = "Release";
#else
        constexpr const char* kBuildType = "Debug";
#endif
        std::cout << Format("\nOne screen of 1,001 elements (250 cards x 4), {} build\n", kBuildType).c_str();
        std::cout << "                                       A: XML     B: HTML+CSS   C: RmlUi\n";
        for (const Row& row : rows) {
            std::cout << Format("  {:<38} {:>10.2f}  {:>10.2f}  {:>10.2f}\n", row.what, row.a, row.b, row.c).c_str();
        }
        std::cout << Format("  RmlUi vertices drawn per frame: {}, glyph and decorator textures: {}\n", verticesC, render.GetTextureCount()).c_str();

        EXPECT_GT(callsA, 0u);
        EXPECT_GT(callsC, 0u);
        static_cast<void>(verticesC);
        EXPECT_GT(callsC, 0u);
        static_cast<void>(firstLayoutC);
    }

}  // namespace BECore::Tests
