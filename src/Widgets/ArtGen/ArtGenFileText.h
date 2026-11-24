#pragma once

#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// @p text without a leading UTF-8 byte order mark. A JsonTape refuses the mark, and a preset, a manifest or a provenance record saved by a
    /// Windows editor carries it, so every ArtGen reader of a file skips it before it tapes the text
    [[nodiscard]] inline StringView WithoutUtf8Bom(StringView text) {
        constexpr StringView kUtf8Bom = "\xEF\xBB\xBF";
        if (text.starts_with(kUtf8Bom)) {
            text.remove_prefix(kUtf8Bom.size());
        }
        return text;
    }

    /// The compact text of @p value, keys in byte order at every level (`WriteJsonSorted`): the form the session keeps and the manifest writes, so
    /// one document is one text whatever order the client wrote it in
    [[nodiscard]] inline String SortedTextOf(JsonView value) {
        String text;
        JsonWriter writer(text);
        WriteJsonSorted(writer, value);
        return text;
    }

    /// Whether two texts are the same JSON value, as JsonEquals judges them: the key order and the form of a number do not matter;
    /// a text that is not JSON equals no valid one. Tapes both texts, so it is not for a per-frame compare
    [[nodiscard]] inline bool SameJsonText(StringView a, StringView b) {
        JsonTape tapeA;
        JsonTape tapeB;
        static_cast<void>(tapeA.Parse(a));
        static_cast<void>(tapeB.Parse(b));
        return JsonEquals(JsonView(tapeA, 0), JsonView(tapeB, 0));
    }

}  // namespace BECore
