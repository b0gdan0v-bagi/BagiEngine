#pragma once

#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>
#include <Common/JsonDoc.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace Detail {

        enum class JsonEditKind { Set, Erase };

        /// @p path is a JSON pointer without escapes (`/jobs/0/accepted/kept`); the edit is made on a copy of @p value written compact, the rest of the
        /// document keeps its text and its order. A member that does not exist is added at the end of its object, an array index may be `-` to add an item.
        inline void WriteEdited(JsonWriter& writer, JsonView value, StringView path, JsonEditKind kind, StringView raw) {
            if (path.empty()) {
                writer.Raw(raw);
                return;
            }
            ASSERT_EQ(path.front(), '/') << "a pointer starts with /";
            path.remove_prefix(1);
            const size_t slash = path.find('/');
            const StringView step = path.substr(0, slash);
            const StringView rest = slash == StringView::npos ? StringView() : path.substr(slash);
            const bool last = rest.empty();

            if (value.IsObject()) {
                bool found = false;
                writer.BeginObject();
                for (const JsonMember member : value.Members()) {
                    const bool hit = !found && member.key.TextEquals(step);
                    found = found || hit;
                    if (hit && last && kind == JsonEditKind::Erase) {
                        continue;
                    }
                    writer.Key(member.key.Text().value_or(StringView()));
                    if (hit) {
                        WriteEdited(writer, member.value, rest, kind, raw);
                    } else {
                        writer.Raw(member.value.Raw());
                    }
                }
                if (!found && last && kind == JsonEditKind::Set) {
                    writer.Key(step);
                    writer.Raw(raw);
                } else {
                    EXPECT_TRUE(found) << "no member " << String(step).c_str() << " to edit";
                }
                writer.EndObject();
                return;
            }
            if (value.IsArray()) {
                size_t position = 0;
                bool found = false;
                writer.BeginArray();
                for (const JsonView item : value.Items()) {
                    const bool hit = !found && String(step) == Format("{}", position);
                    ++position;
                    if (!hit) {
                        writer.Raw(item.Raw());
                        continue;
                    }
                    found = true;
                    if (!(last && kind == JsonEditKind::Erase)) {
                        WriteEdited(writer, item, rest, kind, raw);
                    }
                }
                if (!found && step == "-" && last && kind == JsonEditKind::Set) {
                    writer.Raw(raw);
                } else {
                    EXPECT_TRUE(found) << "no item " << String(step).c_str() << " to edit";
                }
                writer.EndArray();
                return;
            }
            ADD_FAILURE() << "the path goes through a value that is no container";
            writer.Raw(value.Raw());
        }

    }  // namespace Detail

    /// The document @p doc with the value at @p pointer replaced by @p raw (JSON text), or added when an object has no such member: the way a test makes a
    /// document with one defect out of one a writer made
    inline String JsonWith(StringView doc, StringView pointer, StringView raw) {
        const JsonDoc parsed(doc);
        EXPECT_TRUE(parsed.IsParsed());
        String text;
        JsonWriter writer(text);
        Detail::WriteEdited(writer, parsed.Root(), pointer, Detail::JsonEditKind::Set, raw);
        return text;
    }

    /// The document @p doc without the member or item at @p pointer
    inline String JsonWithout(StringView doc, StringView pointer) {
        const JsonDoc parsed(doc);
        EXPECT_TRUE(parsed.IsParsed());
        String text;
        JsonWriter writer(text);
        Detail::WriteEdited(writer, parsed.Root(), pointer, Detail::JsonEditKind::Erase, StringView());
        return text;
    }

}  // namespace BECore::Tests
