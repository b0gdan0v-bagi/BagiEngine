#pragma once

namespace BECore {
    struct AssertEvent;
}  // namespace BECore

namespace BECore::Tests {

    /**
     * @brief Turns engine ASSERT / EXPECT / FATALERROR failures into gtest failures.
     *
     * Without it a failed engine assertion inside tested code does nothing in the test executable:
     * the engine only emits AssertEvent and nobody listens. Every kind is reported as a non-fatal gtest failure
     * at the file and line of the failed engine assertion.
     *
     * Only active when the engine macros are (ENGINE_ENABLE_ASSERTS / ENGINE_ENABLE_EXPECTS), i.e. not in Release presets.
     */
    class AssertBridge : public SubscriptionHolder {
    public:
        void Install();

    private:
        void OnAssert(const AssertEvent& event);
    };

}  // namespace BECore::Tests
