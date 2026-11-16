#pragma once

#include <BECore/Utils/MoveOnlyFunction.h>
#include <Events/SubscriptionHolder.h>
#include <atomic>

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
        /**
         * @brief While it lives the bridge hands every AssertEvent to its callback instead of failing the test.
         *
         * For a test that fails an engine assertion on purpose. The bridge is subscribed after the engine's handlers, so it runs first and nothing outside it can
         * stop it from failing the test. One capture at a time. Make it before any thread that emits starts and let it go after they are joined: the callback runs on the
         * emitting thread.
         */
        class Capture {
        public:
            using Callback = MoveOnlyFunction<void(const AssertEvent&)>;

            explicit Capture(Callback callback = {});
            ~Capture();
            Capture(const Capture&) = delete;
            Capture& operator=(const Capture&) = delete;

        private:
            friend class AssertBridge;

            Callback _callback;
        };

        void Install();

    private:
        static std::atomic<Capture*> s_capture;

        void OnAssert(const AssertEvent& event);
    };

}  // namespace BECore::Tests
