#pragma once

#include <BECore/Control/JsonStreamSerializer.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>
#include <Fuzz/FuzzTargets.h>

namespace BECore::Fuzz {

    /// The JSON document @p value saves through `Serialize`: the members in the order they are written (the target's property is that this text reads back)
    template <typename T>
    [[nodiscard]] String SavedText(const T& value) {
        String text;
        JsonWriter writer(text);
        JsonStreamSerializer serializer(writer);
        value.Serialize(serializer);
        serializer.Finish();
        FUZZ_CHECK(writer.IsComplete());
        return text;
    }

    /// @p document written again with the keys of every object in byte order: the seeds that were written by a sorting DOM keep their bytes
    [[nodiscard]] inline String SortedText(StringView document) {
        JsonTape tape;
        FUZZ_CHECK(tape.Parse(document).has_value());
        String text;
        JsonWriter writer(text);
        WriteJsonSorted(writer, JsonView(tape, 0));
        FUZZ_CHECK(writer.IsComplete());
        return text;
    }

}  // namespace BECore::Fuzz
