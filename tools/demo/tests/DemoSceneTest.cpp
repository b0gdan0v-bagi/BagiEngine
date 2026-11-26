#include <BECore/Scene/Components/ParticleEmitterComponent.h>
#include <tests/DemoSceneFixture.h>

namespace BECore::Demo::Tests {

    namespace {

        template <typename TVisit>
        void ForEachNode(const SceneNode& node, const TVisit& visit) {
            for (const auto& child : node.GetChildren()) {
                visit(*child);
                ForEachNode(*child, visit);
            }
        }

    }  // namespace

    using DemoSceneTest = DemoSceneFixture;

    TEST_F(DemoSceneTest, TheShippedSceneDrawsItsSpritesTextAndParticles) {
        LoadScene();
        const Vector<uint8_t> pixels(kAtlasWidth * kAtlasHeight * kBytesPerPixel, 255);
        const auto atlas = _renderer->CreateTexture(pixels.data(), kAtlasWidth, kAtlasHeight);
        size_t sprites = 0;
        ForEachNode(GetScene().GetRootNode(), [&](const SceneNode& node) {
            if (auto sprite = node.GetComponent<SpriteRendererComponent>()) {
                sprite->SetTexture(atlas);
                ++sprites;
            }
        });
        ASSERT_GT(sprites, 0u);

        Update();
        Update();
        Draw();

        size_t spriteDraws = 0;
        size_t glyphDraws = 0;
        size_t geometryDraws = 0;
        for (const auto& call : _renderer->calls) {
            if (call.texture == atlas.Get()) {
                ++spriteDraws;  // a flipped sprite is geometry too, so the texture decides first
            } else if (call.kind == BECore::Tests::DrawKind::Geometry) {
                ++geometryDraws;
            } else if (call.texture != nullptr) {
                ++glyphDraws;
            }
        }
        EXPECT_EQ(spriteDraws, sprites);
        EXPECT_GT(glyphDraws, 0u);
        EXPECT_GE(geometryDraws, 2u) << "one batch per emitter";
        EXPECT_GT(Component<ParticleEmitterComponent>("Fountain").GetLiveCount(), 0u);
        EXPECT_GT(Component<ParticleEmitterComponent>("Sparks").GetLiveCount(), 0u);
    }

}  // namespace BECore::Demo::Tests
