#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <cstdint>

namespace BECore {

    // =========================================================================
    // Test data structures marked for reflection (the .gen.hpp is generated from this header)
    // =========================================================================

    namespace TestData {

        /**
         * @brief Example reflected class for testing
         */
        struct Player {
            BE_CLASS(Player)

            static constexpr int32_t kFullHealth = 100;
            static constexpr float kPercent = 100.0f;

            BE_REFLECT_FIELD int32_t health = kFullHealth;
            BE_REFLECT_FIELD float speed = 5.0f;
            BE_REFLECT_FIELD PoolString name;
            BE_REFLECT_FIELD bool isAlive = true;

            /// The health left
            BE_FUNCTION int32_t TakeDamage(int32_t amount);

            BE_FUNCTION void Heal(int32_t amount);

            BE_FUNCTION bool IsDead() const;

            /// Health as a percentage of kFullHealth; above 100 after a Heal past it
            BE_FUNCTION float GetHealthPercent() const;
        };

        /**
         * @brief Nested reflected class for testing
         */
        struct Inventory {
            BE_CLASS(Inventory)

            BE_REFLECT_FIELD int32_t gold = 0;
            BE_REFLECT_FIELD int32_t itemCount = 0;
        };

        /**
         * @brief Class with nested object for testing
         */
        struct SavedGameState {
            BE_CLASS(SavedGameState)

            BE_REFLECT_FIELD Player player;
            BE_REFLECT_FIELD Inventory inventory;
            BE_REFLECT_FIELD int32_t level = 1;
        };

    }  // namespace TestData

}  // namespace BECore
