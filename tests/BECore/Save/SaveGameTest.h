#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    // =========================================================================
    // Test data of SaveGameTest.cpp, marked for reflection (the .gen.hpp is generated from this header)
    // =========================================================================

    namespace SaveTestData {

        CORE_ENUM(Difficulty, uint8_t, Easy, Normal, Hard)

        /// A nested reflected struct
        struct Point {
            BE_CLASS(Point)

            BE_REFLECT_FIELD int32_t x = 0;
            BE_REFLECT_FIELD int32_t y = 0;
        };

        /// An element of a vector of structs
        struct Item {
            BE_CLASS(Item)

            BE_REFLECT_FIELD int32_t id = 0;
            BE_REFLECT_FIELD float weight = 0.0f;
            BE_REFLECT_FIELD String label;
        };

        /// A state with every kind of field a game state uses
        struct GameState {
            BE_CLASS(GameState)

            BE_REFLECT_FIELD int32_t score = 0;
            BE_REFLECT_FIELD uint32_t seed = 1;
            BE_REFLECT_FIELD int64_t ticks = 0;
            BE_REFLECT_FIELD float speed = 1.5f;
            BE_REFLECT_FIELD bool hardcore = false;
            BE_REFLECT_FIELD String title;
            BE_REFLECT_FIELD PoolString hero;
            BE_REFLECT_FIELD Difficulty difficulty = Difficulty::Normal;
            BE_REFLECT_FIELD Point position;
            BE_REFLECT_FIELD Vector<Item> items;
        };

        /// The same state in a later version of the game: one more field, which an old file does not have
        struct GameStateWithExtra {
            BE_CLASS(GameStateWithExtra)

            BE_REFLECT_FIELD int32_t score = 0;
            BE_REFLECT_FIELD uint32_t seed = 1;
            BE_REFLECT_FIELD int64_t ticks = 0;
            BE_REFLECT_FIELD float speed = 1.5f;
            BE_REFLECT_FIELD bool hardcore = false;
            BE_REFLECT_FIELD String title;
            BE_REFLECT_FIELD PoolString hero;
            BE_REFLECT_FIELD Difficulty difficulty = Difficulty::Normal;
            BE_REFLECT_FIELD Point position;
            BE_REFLECT_FIELD Vector<Item> items;
            BE_REFLECT_FIELD int32_t lives = 3;
        };

        /// `score` is a float here, an int32 in GameState: the type of the field does not match the file
        struct ScoreAsFloatState {
            BE_CLASS(ScoreAsFloatState)

            BE_REFLECT_FIELD float score = -1.0f;
        };

    }  // namespace SaveTestData

}  // namespace BECore
