#include "RmlUiScreens.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreSDL/SDLRendererBackend.h>
#include <RmlUi/Core.h>
#include <RmlUiHost/RmlUiDocumentComponent.h>
#include <SDL3_image/SDL_image.h>
#include <array>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kWindowConfig = "config/SDLWindowConfig.xml";
        constexpr const char* kScreenshotDirVariable = "BAGI_TEST_SCREENSHOT_DIR";
        const Color kBackground{20, 20, 30, 255};
        constexpr int kBlendTolerance = 4;

        using Pixel = std::array<uint8_t, 4>;

        Pixel PixelAt(const PixelBuffer& image, int x, int y) {
            if (x < 0 || y < 0 || x >= static_cast<int>(image.width) || y >= static_cast<int>(image.height)) {
                return {};
            }
            const size_t offset = (static_cast<size_t>(y) * image.width + static_cast<size_t>(x)) * 4;
            return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2], image.rgba[offset + 3]};
        }

        bool IsNear(const Pixel& actual, const Pixel& expected, int tolerance) {
            for (size_t i = 0; i < actual.size(); ++i) {
                if (std::abs(static_cast<int>(actual[i]) - static_cast<int>(expected[i])) > tolerance) {
                    return false;
                }
            }
            return true;
        }

        /// The number of pixels in a rect of the image that are not the background color
        size_t CountInk(const PixelBuffer& image, const Rect& area) {
            size_t count = 0;
            for (int y = static_cast<int>(area.y); y < static_cast<int>(area.y + area.h); ++y) {
                for (int x = static_cast<int>(area.x); x < static_cast<int>(area.x + area.w); ++x) {
                    if (!IsNear(PixelAt(image, x, y), {kBackground.r, kBackground.g, kBackground.b, kBackground.a}, 8)) {
                        ++count;
                    }
                }
            }
            return count;
        }

        Rect BoxOf(const RmlUiDocumentComponent& doc, const char* id) {
            Rml::Element* element = doc.GetDocument()->GetElementById(id);
            if (!element) {
                ADD_FAILURE() << "no element '" << id << "'";
                return {};
            }
            const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
            return {doc.GetOriginX() + offset.x, doc.GetOriginY() + offset.y, size.x, size.y};
        }

        /// Writes the image as a PNG into the directory named by BAGI_TEST_SCREENSHOT_DIR, when it is set (for a look at what the test drew)
        void MaybeSavePng(const PixelBuffer& image, const char* name) {
            const char* directory = std::getenv(kScreenshotDirVariable);
            if (directory == nullptr || image.rgba.empty()) {
                return;
            }
            SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(image.width), static_cast<int>(image.height), SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t*>(image.rgba.data()),
                                                         static_cast<int>(image.width * 4));
            if (surface != nullptr) {
                const std::string path = std::string(directory) + "/" + name + ".png";
                IMG_SavePNG(surface, path.c_str());
                SDL_DestroySurface(surface);
            }
        }

    }  // namespace

    /// The documents drawn by the real SDL renderer (the software one on the dummy video driver): what the geometry, the premultiplied colors and the glyph textures come to in pixels
    class RmlUiSdlTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _window = New<SDLMainWindow>();
            _window->SetHeadless(true);
            if (!_window->Initialize(kWindowConfig)) {
                _window.Reset();
                GTEST_SKIP() << "no headless window: " << SDL_GetError();
            }
            _renderer = New<SDLRendererBackend>();
            if (!_renderer->Initialize(*_window)) {
                _renderer.Reset();
                GTEST_SKIP() << "no software renderer: " << SDL_GetError();
            }
            CoreManager::GetRendererManager().SetRenderer(_renderer);
        }

        void TearDown() override {
            // The textures of RmlUi belong to the renderer: the components go first
            _manager.Reset();
            CoreManager::GetUIVars().Clear();
            CoreManager::GetRendererManager().SetRenderer({});
            if (_renderer) {
                _renderer->Destroy();
            }
            if (_window) {
                _window->Destroy();
            }
        }

        IntrusivePtr<RmlUiDocumentComponent> Load(const RmlUiScreens::Screen& screen) {
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(RmlUiScreens::SceneXml(screen)));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            _manager->Initialize();
            _manager->SetActiveScene("S"_intern);
            auto doc = _manager->GetSceneByName("S"_intern)->GetRootNode().GetChildren()[0]->GetComponent<RmlUiDocumentComponent>();
            EXPECT_TRUE(doc);
            EXPECT_TRUE(doc && doc->SetMarkup(eastl::string(screen.rml)));
            return doc;
        }

        /// Draws the frame and presents it; returns what was on the back buffer
        PixelBuffer DrawFrame() {
            _renderer->Clear(kBackground);
            _manager->DrawAll();
            std::optional<CaptureResult> captured;
            _renderer->RequestCapture([&captured](CaptureResult result) { captured = std::move(result); });
            _renderer->Present();
            if (!captured || !captured->has_value()) {
                ADD_FAILURE() << "the frame was not captured";
                return {};
            }
            return std::move(**captured);
        }

        IntrusivePtr<SDLMainWindow> _window;
        IntrusivePtr<SDLRendererBackend> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(RmlUiSdlTest, TheMatch3MenuIsDrawnWithItsColorsAndText) {
        auto doc = Load(RmlUiScreens::kScreens[0]);

        const PixelBuffer image = DrawFrame();
        MaybeSavePng(image, "rmlui-match3-menu");

        ASSERT_FALSE(image.rgba.empty());
        const Rect play = BoxOf(*doc, "Play");
        const Rect quit = BoxOf(*doc, "Quit");
        const Rect title = BoxOf(*doc, "Title");
        EXPECT_TRUE(IsNear(PixelAt(image, static_cast<int>(play.x) + 6, static_cast<int>(play.y + play.h * 0.5f)), {40, 90, 60, 255}, kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(image, static_cast<int>(quit.x) + 6, static_cast<int>(quit.y + quit.h * 0.5f)), {90, 50, 50, 255}, kBlendTolerance));
        EXPECT_GT(CountInk(image, title), 200u) << "the title is drawn";
        // The text is white on the dark background: its brightest pixels are the glyph interiors
        size_t bright = 0;
        for (int y = static_cast<int>(title.y); y < static_cast<int>(title.y + title.h); ++y) {
            for (int x = static_cast<int>(title.x); x < static_cast<int>(title.x + title.w); ++x) {
                bright += PixelAt(image, x, y)[0] > 230 ? 1 : 0;
            }
        }
        EXPECT_GT(bright, 100u) << "full coverage of a glyph comes out white, so the premultiplied glyph texture was undone right";
    }

    TEST_F(RmlUiSdlTest, TheHudsAreDrawn) {
        for (size_t i = 1; i < std::size(RmlUiScreens::kScreens); ++i) {
            const RmlUiScreens::Screen& screen = RmlUiScreens::kScreens[i];
            auto doc = Load(screen);
            CoreManager::GetUIVars().Set("score"_intern, 1280);
            CoreManager::GetUIVars().Set("seed"_intern, 1337);
            CoreManager::GetUIVars().Set("turn"_intern, "White to move");
            CoreManager::GetUIVars().Set("status"_intern, "Check");

            const PixelBuffer image = DrawFrame();
            MaybeSavePng(image, i == 1 ? "rmlui-match3-hud" : "rmlui-chess-hud");

            ASSERT_FALSE(image.rgba.empty());
            const Rect reset = BoxOf(*doc, "Reset");
            EXPECT_TRUE(IsNear(PixelAt(image, static_cast<int>(reset.x) + 5, static_cast<int>(reset.y + reset.h * 0.5f)), {40, 90, 60, 255}, kBlendTolerance)) << screen.name;
            const Rect first = BoxOf(*doc, screen.ids[0]);
            EXPECT_GT(CountInk(image, first), 50u) << screen.name << " " << screen.ids[0] << ": the text is drawn from the variables";
            _manager.Reset();
        }
    }

    TEST_F(RmlUiSdlTest, ATranslucentBoxBlendsLikeAnEngineRect) {
        RmlUiScreens::Screen screen = RmlUiScreens::kScreens[1];
        screen.rml =
            R"(<rml><head><style>body { width: 100%; height: 100%; } #box { position: absolute; left: 100dp; top: 100dp; width: 100dp; height: 100dp; background-color: rgba(255, 255, 255, 128); }</style></head><body><div id="box"></div></body></rml>)";
        auto doc = Load(screen);

        const PixelBuffer image = DrawFrame();

        ASSERT_FALSE(image.rgba.empty());
        const Rect box = BoxOf(*doc, "box");
        // 128/255 of white over (20, 20, 30)
        EXPECT_TRUE(IsNear(PixelAt(image, static_cast<int>(box.x + box.w * 0.5f), static_cast<int>(box.y + box.h * 0.5f)), {138, 138, 143, 255}, kBlendTolerance));
    }

    TEST_F(RmlUiSdlTest, AnOverflowHiddenBoxClipsItsText) {
        RmlUiScreens::Screen screen = RmlUiScreens::kScreens[1];
        screen.rml = R"(<rml><head><style>body { width: 100%; height: 100%; font-family: "PT Sans"; color: white; font-size: 40dp; }
#clip { position: absolute; left: 50dp; top: 50dp; width: 200dp; height: 30dp; overflow: hidden; }
#long { white-space: nowrap; }</style></head><body><div id="clip"><div id="long">Wide wide wide wide wide wide wide wide wide wide</div></div></body></rml>)";
        auto doc = Load(screen);

        const PixelBuffer image = DrawFrame();

        ASSERT_FALSE(image.rgba.empty());
        const Rect clip = BoxOf(*doc, "clip");
        EXPECT_GT(CountInk(image, clip), 100u);
        // Right of the box nothing is drawn, though the text goes on
        EXPECT_EQ(CountInk(image, Rect{clip.x + clip.w + 2.0f, clip.y, 300.0f, clip.h}), 0u);
    }

}  // namespace BECore::Tests
