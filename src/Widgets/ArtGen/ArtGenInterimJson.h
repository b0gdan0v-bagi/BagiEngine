#pragma once

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>

namespace BECore::Detail {

    // Interim: goes with the last caller of the `Json` wrappers of the ArtGen leaves (WP 7)

    /// The value a leaf writer wrote, as a DOM: what an old `...ToJson` returned
    template <typename TWrite>
    [[nodiscard]] Json ArtGenDomOf(TWrite&& write) {
        String text;
        JsonWriter writer(text);
        write(writer);
        return ParseJson(text).value_or(Json());
    }

    /// Reads @p json through a tape of its text: what an old `...FromJson` did over the new view reader
    template <typename TRead>
    [[nodiscard]] auto ArtGenReadDom(const Json& json, TRead&& read) {
        const String text = DumpJson(json);
        JsonTape tape;
        static_cast<void>(tape.Parse(text));
        return read(JsonView(tape, 0));
    }

    /// A draft the form wrote as text, as the DOM the session still takes (Interim: goes with the session's `Json` drafts, WP 4)
    [[nodiscard]] inline Json ArtGenDraftOf(StringView text) {
        return ParseJson(text).value_or(Json());
    }

}  // namespace BECore::Detail
