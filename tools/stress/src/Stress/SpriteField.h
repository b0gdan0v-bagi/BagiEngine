#pragma once

#include <BECore/Renderer/BlendMode.h>
#include <Stress/StressSettings.h>

namespace BECore::Stress {

    /// The picture the sprites are placed in, in scene units (the view of the scene `StressSprites`)
    struct FieldView {
        float width = 1920.0f;
        float height = 1080.0f;
    };

    inline constexpr float kSpriteMinSize = 16.0f;
    inline constexpr float kSpriteMaxSize = 48.0f;
    /// The spin of a sprite in degrees per second, either way round
    inline constexpr float kSpriteMinSpin = 30.0f;
    inline constexpr float kSpriteMaxSpin = 270.0f;
    /// An atlas is `kAtlasCellsPerSide` x `kAtlasCellsPerSide` cells of `kAtlasCellSize` pixels
    inline constexpr uint32_t kAtlasCellsPerSide = 4;
    inline constexpr uint32_t kAtlasCellSize = 32;
    /// Consecutive sprites that share a blend mode when the sprites are in runs (the way a game sorts by blend mode)
    inline constexpr uint32_t kBlendRunLength = 256;

    /// Where and how one sprite is drawn. Everything but the atlas and the blend mode depends on the index alone, so growing the count keeps the first sprites in place.
    struct SpritePlacement {
        float x = 0.0f;  // the top-left corner of the sprite's rect
        float y = 0.0f;
        float size = 0.0f;   // the sprite is a square of this side
        float spin = 0.0f;   // degrees per second, clockwise when positive
        uint32_t atlas = 0;  // 0..settings.atlases - 1
        Rect srcRect;        // one cell of the atlas, in its pixels
        BlendMode blend = BlendMode::Alpha;
    };

    /**
     * @brief The placement of sprite @p index of @p count, the same on every machine (an own integer hash: the standard distributions are not
     *        the same in every standard library).
     *
     * The position keeps the whole sprite inside @p view. The atlas and the blend mode follow what a batcher sees: by default the sprites are in
     * runs, the way a game sorts by atlas: `count / atlases` consecutive sprites share an atlas and `kBlendRunLength` share a blend mode.
     * `settings.interleave` changes both on every sprite (the worst case).
     */
    SpritePlacement Place(uint32_t index, uint32_t count, const StressSettings& settings, const FieldView& view);

    /// How many consecutive sprites share an atlas when the sprites are in runs: `count / atlases`, at least 1
    uint32_t AtlasRunLength(uint32_t count, uint32_t atlases);

    /**
     * @brief The RGBA8 pixels (red first, rows from the top) of atlas @p atlasIndex: `cells` x `cells` cells of `cellSize` pixels.
     *
     * Every cell has a flat colour of its own, a marker inside it and translucent corners, so Alpha, Add and Multiply visibly differ and a
     * wrong `srcRect` shows. The atlases differ from each other.
     */
    Vector<uint8_t> MakeAtlasPixels(uint32_t atlasIndex, uint32_t cells, uint32_t cellSize);

}  // namespace BECore::Stress
