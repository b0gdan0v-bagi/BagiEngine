#include "JsonViewOps.h"

#include <BECore/Control/JsonPointerSyntax.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        /// An Integer token as the exact number it names: signed when it fits int64, else unsigned when it fits uint64, else neither
        struct ExactInteger {
            bool isExact = false;
            bool isUnsigned = false;
            int64_t signedValue = 0;
            uint64_t unsignedValue = 0;
        };

        ExactInteger ReadExactInteger(JsonView value) {
            ExactInteger result;
            if (!value.IsInteger()) {
                return result;
            }
            if (const Optional<int64_t> signedValue = value.AsInt64()) {
                result.isExact = true;
                result.signedValue = *signedValue;
            } else if (const Optional<uint64_t> unsignedValue = value.AsUint64()) {
                result.isExact = true;
                result.isUnsigned = true;
                result.unsignedValue = *unsignedValue;
            }
            return result;
        }

        bool NumbersEqual(JsonView a, JsonView b) {
            const ExactInteger left = ReadExactInteger(a);
            const ExactInteger right = ReadExactInteger(b);
            if (left.isExact && right.isExact) {
                if (left.isUnsigned != right.isUnsigned) {
                    return false;  // an unsigned past int64 is above every signed one
                }
                return left.isUnsigned ? left.unsignedValue == right.unsignedValue : left.signedValue == right.signedValue;
            }
            // A float on one side, or an integer beyond 64 bits: the DOM compared those as doubles too
            const Optional<double> x = a.AsDouble();
            const Optional<double> y = b.AsDouble();
            return x && y && *x == *y;
        }

        bool StringsEqual(JsonView a, JsonView b) {
            const Optional<StringView> left = a.Text();
            const Optional<StringView> right = b.Text();
            if (left && right) {
                return *left == *right;
            }
            // Decode the one that holds an escape; the other is compared piece by piece
            String decoded;
            if (left) {
                b.ReadText(decoded);
                return a.TextEquals(StringView(decoded.c_str(), decoded.size()));
            }
            a.ReadText(decoded);
            return b.TextEquals(StringView(decoded.c_str(), decoded.size()));
        }

        bool ObjectsEqual(JsonView a, JsonView b) {
            if (a.Size() != b.Size()) {
                return false;
            }
            for (const JsonMember member : a.Members()) {
                String key;
                const Optional<StringView> plain = member.key.Text();
                if (!plain) {
                    member.key.ReadText(key);
                }
                const StringView name = plain ? *plain : StringView(key.c_str(), key.size());
                const Optional<JsonView> other = b.Find(name);
                if (!other || !JsonEquals(member.value, *other)) {
                    return false;
                }
            }
            return true;
        }

        bool ArraysEqual(JsonView a, JsonView b) {
            if (a.Size() != b.Size()) {
                return false;
            }
            auto right = b.Items().begin();
            for (const JsonView item : a.Items()) {
                if (!JsonEquals(item, *right)) {
                    return false;
                }
                ++right;
            }
            return true;
        }

        /// Hands the decoded text of the string @p text to @p use; a string with no escape is not copied
        template <typename TUse>
        void WithText(JsonView text, TUse&& use) {
            if (const Optional<StringView> plain = text.Text()) {
                use(*plain);
                return;
            }
            String decoded;
            text.ReadText(decoded);
            use(StringView(decoded.c_str(), decoded.size()));
        }

        void WriteNumber(JsonWriter& writer, JsonView value) {
            const ExactInteger integer = ReadExactInteger(value);
            if (integer.isExact) {
                if (integer.isUnsigned) {
                    writer.Value(integer.unsignedValue);
                } else {
                    writer.Value(integer.signedValue);
                }
                return;
            }
            // AsDouble fails only for a number text over the buffer of its fallback, which no document of the tape's limits is
            writer.Value(value.AsDouble().value_or(0.0));
        }

    }  // namespace

    bool JsonEquals(JsonView a, JsonView b) {
        if (!a.IsValid() || !b.IsValid()) {
            return a.IsValid() == b.IsValid();
        }
        if (a.IsNumber() && b.IsNumber()) {
            return NumbersEqual(a, b);
        }
        if (a.Kind() != b.Kind()) {
            return false;
        }
        switch (a.Kind()) {
            case JsonKind::String:
                return StringsEqual(a, b);
            case JsonKind::Bool:
                return a.AsBool() == b.AsBool();
            case JsonKind::Array:
                return ArraysEqual(a, b);
            case JsonKind::Object:
                return ObjectsEqual(a, b);
            default:
                return true;  // null
        }
    }

    namespace {

        /// A member of an object whose members are written by key: the decoded key, the value and the place in the document (so equal keys keep their order)
        struct SortedMember {
            String key;
            JsonView value;
            size_t order = 0;
        };

        /// Copies @p value into @p writer; with kSorted the members of every object come out in byte order of the decoded key, the first of equal keys alone
        template <bool kSorted>
        void WriteValue(JsonWriter& writer, JsonView value) {
            switch (value.Kind()) {
                case JsonKind::Bool:
                    writer.Value(value.AsBool().value_or(false));
                    return;
                case JsonKind::Integer:
                case JsonKind::Number:
                    WriteNumber(writer, value);
                    return;
                case JsonKind::String:
                    WithText(value, [&](StringView text) { writer.Value(text); });
                    return;
                case JsonKind::Array:
                    if (writer.GetDepth() >= JsonWriter::kMaxDepth) {
                        writer.Null();
                        return;
                    }
                    writer.BeginArray();
                    for (const JsonView item : value.Items()) {
                        WriteValue<kSorted>(writer, item);
                    }
                    writer.EndArray();
                    return;
                case JsonKind::Object:
                    if (writer.GetDepth() >= JsonWriter::kMaxDepth) {
                        writer.Null();
                        return;
                    }
                    writer.BeginObject();
                    if constexpr (kSorted) {
                        // For files and typed commands, never a hot path: one copy of the keys per object
                        Vector<SortedMember> members;
                        members.reserve(value.Size());
                        for (const JsonMember member : value.Members()) {
                            SortedMember& entry = members.emplace_back();
                            member.key.ReadText(entry.key);
                            entry.value = member.value;
                            entry.order = members.size();
                        }
                        eastl::sort(members.begin(), members.end(), [](const SortedMember& a, const SortedMember& b) {
                            const int byKey = StringView(a.key.c_str(), a.key.size()).compare(StringView(b.key.c_str(), b.key.size()));
                            return byKey != 0 ? byKey < 0 : a.order < b.order;
                        });
                        for (size_t i = 0; i < members.size(); ++i) {
                            if (i > 0 && members[i].key == members[i - 1].key) {
                                continue;
                            }
                            writer.Key(StringView(members[i].key.c_str(), members[i].key.size()));
                            WriteValue<kSorted>(writer, members[i].value);
                        }
                    } else {
                        for (const JsonMember member : value.Members()) {
                            WithText(member.key, [&](StringView name) { writer.Key(name); });
                            WriteValue<kSorted>(writer, member.value);
                        }
                    }
                    writer.EndObject();
                    return;
                default:
                    writer.Null();  // null, and a view that is not valid
                    return;
            }
        }

    }  // namespace

    void WriteJson(JsonWriter& writer, JsonView value) {
        WriteValue<false>(writer, value);
    }

    void WriteJsonSorted(JsonWriter& writer, JsonView value) {
        WriteValue<true>(writer, value);
    }

    std::expected<Optional<JsonView>, ControlError> FindJsonPointer(JsonView document, StringView pointer) {
        if (pointer.empty()) {
            return document.IsValid() ? Optional<JsonView>(document) : std::nullopt;
        }
        if (pointer[0] != Detail::kPointerSeparator) {
            return std::unexpected(Detail::BadPointer("it must be empty or start with `/`"));
        }
        if (const auto bad = Detail::CheckPointerEscapes(pointer)) {
            return std::unexpected(*bad);
        }

        Optional<JsonView> current = document.IsValid() ? Optional<JsonView>(document) : std::nullopt;
        String unescaped;
        size_t start = 1;
        while (start <= pointer.size()) {
            size_t end = pointer.find(Detail::kPointerSeparator, start);
            if (end == StringView::npos) {
                end = pointer.size();
            }
            const StringView token = pointer.substr(start, end - start);
            start = end + 1;

            // Once the path left the document the escapes are all that is still checked, and that is done
            if (!current || (!current->IsObject() && !current->IsArray())) {
                current = std::nullopt;
            } else if (current->IsObject()) {
                current = current->Find(Detail::UnescapePointerToken(token, unescaped));
            } else {
                const auto index = Detail::ParseArrayIndex(token, current->Size());
                if (!index) {
                    return std::unexpected(index.error());
                }
                if (!index->has_value()) {
                    current = std::nullopt;
                    continue;
                }
                auto item = current->Items().begin();
                for (size_t skipped = 0; skipped < **index; ++skipped) {
                    ++item;
                }
                current = *item;
            }
        }
        return current;
    }

    std::expected<void, ControlError> CheckJsonPointer(StringView pointer) {
        if (pointer.empty()) {
            return {};
        }
        if (pointer[0] != Detail::kPointerSeparator) {
            return std::unexpected(Detail::BadPointer("it must be empty or start with `/`"));
        }
        if (const auto bad = Detail::CheckPointerEscapes(pointer)) {
            return std::unexpected(*bad);
        }
        return {};
    }

}  // namespace BECore
