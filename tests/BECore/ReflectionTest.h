#pragma once

#include <BECore/Reflection/TypeTraits.h>

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

            BE_REFLECT_FIELD int32_t health = 100;
            BE_REFLECT_FIELD float speed = 5.0f;
            BE_REFLECT_FIELD PoolString name;
            BE_REFLECT_FIELD bool isAlive = true;

            /**
             * @brief Take damage and update health
             * @param amount Damage amount
             * @return Remaining health
             */
            BE_FUNCTION int32_t TakeDamage(int32_t amount);

            /**
             * @brief Heal the player
             * @param amount Heal amount
             */
            BE_FUNCTION void Heal(int32_t amount);

            /**
             * @brief Check if player is dead
             * @return true if health <= 0
             */
            BE_FUNCTION bool IsDead() const;

            /**
             * @brief Get current health percentage
             * @return Health as percentage (0-100)
             */
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
        struct GameState {
            BE_CLASS(GameState)

            BE_REFLECT_FIELD Player player;
            BE_REFLECT_FIELD Inventory inventory;
            BE_REFLECT_FIELD int32_t level = 1;
        };

    }  // namespace TestData

}  // namespace BECore
