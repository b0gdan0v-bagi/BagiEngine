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

    void AssertBridge::Install() {
        Subscribe<AssertEvent, &AssertBridge::OnAssert>(this);
    }

    void AssertBridge::OnAssert(const AssertEvent& event) {
        ADD_FAILURE_AT(event.file, event.line) << "Engine " << ToLabel(event.type) << " failed" << (event.expression != nullptr ? ": " : "") << (event.expression != nullptr ? event.expression : "")
                                               << (event.message != nullptr ? " - " : "") << (event.message != nullptr ? event.message : "");
    }

}  // namespace BECore::Tests
