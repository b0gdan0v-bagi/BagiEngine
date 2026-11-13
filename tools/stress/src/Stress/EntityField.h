#pragma once

#include <Stress/SpriteField.h>

namespace BECore::Stress {

    inline constexpr float kOrbitMinRadius = 20.0f;
    inline constexpr float kOrbitMaxRadius = 120.0f;
    /// The seconds one turn of an orbit takes
    inline constexpr float kOrbitMinSeconds = 2.0f;
    inline constexpr float kOrbitMaxSeconds = 6.0f;
    /// Every this many nodes the scene also writes a rotation each frame (a cost a game has besides the tweens)
    inline constexpr uint32_t kSpinEvery = 16;
    /// The turn of such a node, degrees per second
    inline constexpr float kSpinDegreesPerSecond = 90.0f;

    /// The circle a node of the `entities` scene moves on
    struct EntityOrbit {
        float centerX = 0.0f;
        float centerY = 0.0f;
        float radius = 0.0f;
        float phase = 0.0f;    // radians: where on the circle the node is when the tween is at 0
        float seconds = 0.0f;  // one turn, the duration of a full tween
        /// Where in its turn (0..1) the node's first tween starts: hashed, so the first ends are spread over the first turn like the later ones,
        /// not all after a full duration (every restart after the first is a full turn from 0)
        float startTurn = 0.0f;
    };

    /// The orbit of node @p index, the same on every machine (StressHash.h); the whole circle is inside @p view
    EntityOrbit OrbitOf(uint32_t index, const FieldView& view);

    /// The point of @p orbit after @p turn of a full turn (0..1): the node's position
    struct OrbitPoint {
        float x = 0.0f;
        float y = 0.0f;
    };
    OrbitPoint PointOn(const EntityOrbit& orbit, float turn);

}  // namespace BECore::Stress
