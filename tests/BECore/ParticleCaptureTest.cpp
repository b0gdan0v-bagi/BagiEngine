#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/Components/ParticleEmitterComponent.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreSDL/SDLRendererBackend.h>
#include <CoreSDL/SDLScreenshot.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kWindowConfig = "config/SDLWindowConfig.xml";
        constexpr const char* kParticlesDoc = "src/Modules/BECore/Particles/CLAUDE.md";
        constexpr const char* kSparkSprite = "ParticleCaptureSpark";
        constexpr double kFrame = 1.0 / 32.0;  // a power of two, so the sums of frames are exact
        constexpr double kMaxFrame = 0.25;
        constexpr int kFramesToHalfASecond = 16;
        constexpr int kFramesToOutliveTheEffect = 5;  // of kMaxFrame, past the second the sparks live
        constexpr uint32_t kTextureSize = 16;
        constexpr size_t kChannels = 4;
        constexpr int kBlendTolerance = 6;
        constexpr int kEdgeTolerance = 1;  // pixels: the software rasterizer rounds the edges of a triangle
        constexpr int kSpark = 100;        // the color of a particle, per channel (Add puts this on the background)

        // The example effect of Particles/CLAUDE.md: the doc quotes this text, and a test below checks that it still does. Every range has min = max and the cone
        // has no width, so every particle of a burst flies the same way and the picture follows from the closed form of the motion:
        // from the point of the burst, 100 units a second to the right, gravity 80 down, a square of 20 with the color (100, 100, 100), the right half of a 16 by 16 texture.
        constexpr const char* kExampleEffect = R"(<effect burstCount="1" maxParticles="16" lifetimeMin="1" lifetimeMax="1" speedMin="100" speedMax="100"
        direction="0" spread="0" gravityY="80" spriteId="ParticleCaptureSpark" blend="Add" frameClass="FiniteEffect">
  <size from="20" to="20"/>
  <color>
    <from r="100" g="100" b="100" a="255"/>
    <to r="100" g="100" b="100" a="255"/>
  </color>
</effect>)";

        constexpr float kBurstX = 400.0f;
        constexpr float kBurstY = 300.0f;
        constexpr float kSize = 20.0f;
        constexpr float kHalfASecondX = 50.0f;  // 100 units a second for half a second
        constexpr float kHalfASecondY = 10.0f;  // 80 * 0.5^2 / 2

        const Color kBackground{10, 20, 30, 255};

        using Pixel = std::array<int, 3>;

        std::string SceneXml() {
            return std::string(R"(<SceneManager><scenes><scene type="Scene" name="Sparks"><nodes>
  <node name="Emitter"><components>
    <component type="TransformComponent" x="0" y="0" width="0" height="0"/>
    <component type="ParticleEmitterComponent" playOnStart="false">)") +
                   kExampleEffect + "</component></components></node></nodes></scene></scenes></SceneManager>";
        }

        Pixel PixelAt(const PixelBuffer& image, int x, int y) {
            const size_t offset = (static_cast<size_t>(y) * image.width + static_cast<size_t>(x)) * kChannels;
            return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2]};
        }

        bool IsNear(const Pixel& actual, const Pixel& expected, int tolerance) {
            for (size_t i = 0; i < actual.size(); ++i) {
                if (std::abs(actual[i] - expected[i]) > tolerance) {
                    return false;
                }
            }
            return true;
        }

        const Pixel kBackgroundPixel{kBackground.r, kBackground.g, kBackground.b};

        /// The box around every pixel that is not the background; empty (w == 0) when the picture is the background
        struct Bounds {
            int left = 0;
            int top = 0;
            int right = -1;
            int bottom = -1;
            [[nodiscard]] bool Empty() const {
                return right < left;
            }
        };

        Bounds LitBounds(const PixelBuffer& image) {
            Bounds bounds;
            bounds.left = static_cast<int>(image.width);
            bounds.top = static_cast<int>(image.height);
            for (uint32_t y = 0; y < image.height; ++y) {
                for (uint32_t x = 0; x < image.width; ++x) {
                    if (PixelAt(image, static_cast<int>(x), static_cast<int>(y)) != kBackgroundPixel) {
                        bounds.left = std::min(bounds.left, static_cast<int>(x));
                        bounds.right = std::max(bounds.right, static_cast<int>(x));
                        bounds.top = std::min(bounds.top, static_cast<int>(y));
                        bounds.bottom = std::max(bounds.bottom, static_cast<int>(y));
                    }
                }
            }
            if (bounds.right < bounds.left) {
                return {};
            }
            return bounds;
        }

        /// A 16 by 16 texture: the left half transparent black, the right half opaque white. A wrong UV shows as a missing particle
        Vector<uint8_t> HalfAndHalf() {
            Vector<uint8_t> pixels(static_cast<size_t>(kTextureSize) * kTextureSize * kChannels, 0);
            for (uint32_t y = 0; y < kTextureSize; ++y) {
                for (uint32_t x = kTextureSize / 2; x < kTextureSize; ++x) {
                    const size_t offset = (static_cast<size_t>(y) * kTextureSize + x) * kChannels;
                    std::fill_n(pixels.begin() + static_cast<ptrdiff_t>(offset), kChannels, uint8_t{255});
                }
            }
            return pixels;
        }

    }  // namespace

    /// A headless window with the SDL renderer on it and a scene with the example emitter. A machine without the dummy video driver skips the test.
    class ParticleCaptureTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            window = New<SDLMainWindow>();
            window->SetHeadless(true);
            if (!window->Initialize(kWindowConfig)) {
                window.Reset();
                GTEST_SKIP() << "no headless window: " << SDL_GetError();
            }
            renderer = New<SDLRendererBackend>();
            if (!renderer->Initialize(*window)) {
                renderer.Reset();
                GTEST_SKIP() << "no software renderer: " << SDL_GetError();
            }
            CoreManager::GetRendererManager().SetRenderer(renderer);

            SpriteEntry spark;
            spark.name = PoolString::Intern(kSparkSprite);
            spark.srcRect = Rect(kTextureSize / 2.0f, 0.0f, kTextureSize / 2.0f, static_cast<float>(kTextureSize));  // the right half
            CoreManager::GetTextureLibrary().AddOrUpdate(spark);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(SceneXml().c_str()));
            manager = New<SceneManager>();
            manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            manager->Initialize();
            emitter = manager->GetActiveScene()->GetRootNode().GetChildren().front()->GetComponent<ParticleEmitterComponent>();
            ASSERT_TRUE(emitter);

            const auto pixels = HalfAndHalf();
            emitter->SetTexture(renderer->CreateTexture(pixels.data(), kTextureSize, kTextureSize));
        }

        void TearDown() override {
            emitter.Reset();
            manager.Reset();
            CoreManager::GetTextureLibrary().Remove(PoolString::Intern(kSparkSprite));
            CoreManager::GetRendererManager().SetRenderer({});
            if (renderer) {
                renderer->Destroy();
            }
            if (window) {
                window->Destroy();
            }
        }

        /// One frame of @p seconds of game time
        void Step(double seconds) {
            CoreManager::GetTime().Advance(seconds);
            manager->UpdateAll();
        }

        /// Draws the scene on the background and returns what was on the back buffer
        PixelBuffer Render() {
            renderer->Clear(kBackground);
            manager->DrawAll();
            Optional<CaptureResult> captured;
            renderer->RequestCapture([&captured](CaptureResult result) { captured = std::move(result); });
            renderer->Present();
            if (!captured || !captured->has_value()) {
                ADD_FAILURE() << "the frame was not captured";
                return {};
            }
            return std::move(**captured);
        }

        /// A burst of @p count at the point (kBurstX, kBurstY) and half a second of game time in @p steps frames
        PixelBuffer HalfASecondAfterABurst(int count, int steps) {
            emitter->Burst(kBurstX, kBurstY, count);
            for (int i = 0; i < steps; ++i) {
                Step(0.5 / steps);
            }
            return Render();
        }

        IntrusivePtr<SDLMainWindow> window;
        IntrusivePtr<SDLRendererBackend> renderer;
        IntrusivePtrAtomic<SceneManager> manager;
        IntrusivePtr<ParticleEmitterComponent> emitter;
    };

    TEST_F(ParticleCaptureTest, TheExampleEffectIsTheOneTheDocQuotes) {
        const std::filesystem::path path = CoreManager::GetFileSystem().ResolvePath(kParticlesDoc);
        std::ifstream file(path, std::ios::binary);
        ASSERT_TRUE(!path.empty() && file.good()) << kParticlesDoc;
        std::stringstream text;
        text << file.rdbuf();
        EXPECT_NE(text.str().find(kExampleEffect), std::string::npos) << "Particles/CLAUDE.md must quote the example effect of ParticleCaptureTest verbatim";
    }

    TEST_F(ParticleCaptureTest, TheBackgroundIsAloneBeforeAnythingIsEmitted) {
        EXPECT_TRUE(LitBounds(Render()).Empty());
    }

    TEST_F(ParticleCaptureTest, AParticleLiesWhereTheClosedFormPutsItAndHasItsColor) {
        const PixelBuffer image = HalfASecondAfterABurst(1, kFramesToHalfASecond);
        ASSERT_FALSE(image.rgba.empty());

        const float centerX = kBurstX + kHalfASecondX;
        const float centerY = kBurstY + kHalfASecondY;
        const Bounds bounds = LitBounds(image);
        ASSERT_FALSE(bounds.Empty()) << "the picture is the background";
        EXPECT_NEAR(bounds.left, centerX - kSize / 2.0f, kEdgeTolerance);
        EXPECT_NEAR(bounds.right, centerX + kSize / 2.0f, kEdgeTolerance + 1);
        EXPECT_NEAR(bounds.top, centerY - kSize / 2.0f, kEdgeTolerance);
        EXPECT_NEAR(bounds.bottom, centerY + kSize / 2.0f, kEdgeTolerance + 1);

        // Add puts the color of the particle on the background: the texel is white, so the vertex color is all there is
        EXPECT_TRUE(IsNear(PixelAt(image, static_cast<int>(centerX), static_cast<int>(centerY)), {kBackground.r + kSpark, kBackground.g + kSpark, kBackground.b + kSpark}, kBlendTolerance));
    }

    TEST_F(ParticleCaptureTest, TwoOverlappingAddParticlesAreBrighterThanOne) {
        const PixelBuffer one = HalfASecondAfterABurst(1, kFramesToHalfASecond);
        emitter->Clear();
        Step(kMaxFrame);
        Step(kMaxFrame);
        Step(kMaxFrame);
        Step(kMaxFrame);  // a second on, the first burst is far gone
        const PixelBuffer two = HalfASecondAfterABurst(2, kFramesToHalfASecond);

        const int x = static_cast<int>(kBurstX + kHalfASecondX);
        const int y = static_cast<int>(kBurstY + kHalfASecondY);
        EXPECT_TRUE(IsNear(PixelAt(one, x, y), {kBackground.r + kSpark, kBackground.g + kSpark, kBackground.b + kSpark}, kBlendTolerance));
        EXPECT_TRUE(IsNear(PixelAt(two, x, y), {kBackground.r + 2 * kSpark, kBackground.g + 2 * kSpark, kBackground.b + 2 * kSpark}, kBlendTolerance));
    }

    TEST_F(ParticleCaptureTest, TheSamePictureComesFromAnyFrameStepAtTheSameGameTime) {
        const PixelBuffer fine = HalfASecondAfterABurst(1, kFramesToHalfASecond);
        ASSERT_FALSE(LitBounds(fine).Empty());
        emitter->Clear();
        for (int i = 0; i < kFramesToOutliveTheEffect; ++i) {
            Step(kMaxFrame);
        }
        const PixelBuffer coarse = HalfASecondAfterABurst(1, 2);  // two steps of 0.25 s

        ASSERT_EQ(coarse.rgba.size(), fine.rgba.size());
        EXPECT_TRUE(std::equal(fine.rgba.begin(), fine.rgba.end(), coarse.rgba.begin())) << "the same game time must give the same pixels";
    }

    TEST_F(ParticleCaptureTest, WhenTheEffectHasEndedTheSceneIsTheBackgroundAgain) {
        ASSERT_FALSE(LitBounds(HalfASecondAfterABurst(1, kFramesToHalfASecond)).Empty());
        for (int i = 0; i < kFramesToOutliveTheEffect; ++i) {
            Step(kMaxFrame);
        }
        EXPECT_EQ(emitter->GetLiveCount(), 0u);
        EXPECT_FALSE(emitter->NeedsFrames());
        EXPECT_TRUE(LitBounds(Render()).Empty());
    }

}  // namespace BECore::Tests
