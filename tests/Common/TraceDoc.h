#pragma once

#include <BECore/Control/JsonView.h>
#include <BECore/Types/Types.h>
#include <Common/ControlTestSupport.h>
#include <Common/JsonDoc.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// A written Chrome trace read back through its own tape: it fails the test when the text is not JSON or has no `traceEvents`, and then
    /// reads as an empty trace. Movable (the tape borrows the text, which stays where it is).
    class TraceDoc {
    public:
        explicit TraceDoc(StringView text) : _doc(MakeUnique<JsonDoc>(text)) {
            EXPECT_TRUE(_doc->IsParsed()) << String(text.data(), text.size()).c_str();
            _events = MemberOf(_doc->Root(), "traceEvents");
            EXPECT_TRUE(_events.IsArray());
        }

        explicit TraceDoc(const String& text) : TraceDoc(StringView(text.c_str(), text.size())) {}

        /// The whole document
        [[nodiscard]] JsonView Root() const {
            return _doc->Root();
        }

        /// The `traceEvents` array
        [[nodiscard]] JsonView Events() const {
            return _events;
        }

        [[nodiscard]] size_t size() const {
            return _events.Size();
        }

        [[nodiscard]] auto begin() const {
            return _events.Items().begin();
        }

        [[nodiscard]] auto end() const {
            return _events.Items().end();
        }

    private:
        UniquePtr<JsonDoc> _doc;
        JsonView _events;
    };

}  // namespace BECore::Tests
