#include "AssertBridge.h"

#include <BECore/Assert/AssertEvent.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        const char* ToLabel(AssertType type) {
            switch (type) {
                case AssertType::Assert:
                    return "ASSERT";
                case AssertType::Expect:
                    return "EXPECT";
                case AssertType::FatalError:
                    return "FATALERROR";
            }
            return "ASSERT";
        }

    }  // namespace

    std::atomic<AssertBridge::Capture*> AssertBridge::s_capture{nullptr};

    AssertBridge::Capture::Capture(Callback callback) : _callback(std::move(callback)) {
        Capture* expected = nullptr;
        const bool installed = s_capture.compare_exchange_strong(expected, this);
        EXPECT_TRUE(installed) << "only one AssertBridge::Capture may live at a time";
    }

    AssertBridge::Capture::~Capture() {
        Capture* expected = this;
        s_capture.compare_exchange_strong(expected, nullptr);
    }

    void AssertBridge::Install() {
        Subscribe<AssertEvent, &AssertBridge::OnAssert>(this);
    }

    void AssertBridge::OnAssert(const AssertEvent& event) {
        if (Capture* capture = s_capture.load()) {
            if (capture->_callback) {
                capture->_callback(event);
            }
            return;
        }
        ADD_FAILURE_AT(event.file, event.line) << "Engine " << ToLabel(event.type) << " failed" << (event.expression != nullptr ? ": " : "") << (event.expression != nullptr ? event.expression : "")
                                               << (event.message != nullptr ? " - " : "") << (event.message != nullptr ? event.message : "");
    }

}  // namespace BECore::Tests
