#pragma once

#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Types/Types.h>

namespace BECore::Tests {

    /// The @p index-th element of an array view; a view that is not valid (and reads as nothing) when there is none
    [[nodiscard]] inline JsonView ItemAt(JsonView array, size_t index) {
        size_t position = 0;
        for (const JsonView item : array.Items()) {
            if (position++ == index) {
                return item;
            }
        }
        return JsonView();
    }

    /// A JSON text with its own tape, so a test can hand a `JsonView` of a literal to the code under test. It keeps a copy of the text (the tape
    /// borrows it), so it is neither copied nor moved; a temporary lives until the end of the full expression that uses its view.
    class JsonDoc {
    public:
        explicit JsonDoc(StringView source) : _text(source.data(), source.size()), _parsed(_tape.Parse(StringView(_text.c_str(), _text.size())).has_value()) {}

        JsonDoc(const JsonDoc&) = delete;
        JsonDoc& operator=(const JsonDoc&) = delete;

        /// The root value; not valid when the text is not JSON
        [[nodiscard]] JsonView Root() const {
            return JsonView(_tape, 0);
        }

        /// So a literal can be passed where a view is asked: `ControlledAction(game, JsonDoc(R"({})"))`
        operator JsonView() const {  // NOLINT(google-explicit-constructor): the conversion is the point
            return Root();
        }

        [[nodiscard]] bool IsParsed() const {
            return _parsed;
        }

        [[nodiscard]] StringView Text() const {
            return StringView(_text.c_str(), _text.size());
        }

    private:
        String _text;
        JsonTape _tape;
        bool _parsed;
    };

}  // namespace BECore::Tests
