#pragma once

#include <Core/RefCounted/RefCounted.h>

namespace Core {

    /**
     * @brief Interface for all engine tests
     *
     * Tests should inherit from this interface and implement
     * the Run() method with their test logic.
     */
    class ITest : public RefCounted {
    public:
        ITest() = default;
        ~ITest() override = default;

        /**
         * @brief Execute the test
         * @return true if test passed, false otherwise
         */
        virtual bool Run() = 0;

        /**
         * @brief Get test name for logging
         * @return Test name string
         */
        virtual const char* GetName() const = 0;
    };

}  // namespace Core
