#pragma once

#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <initializer_list>

namespace BECore {

    /**
     * @brief An object as a list of keys and the text of each value, sorted by key: the owned form of an object whose members this build does not
     * know and writes back, or that is made from `key=value` words.
     *
     * A value is kept as the text of one JSON value, so a nested document costs no tree and survives a round trip untouched in meaning; Write()
     * writes it again with its objects sorted, the bytes `Json::dump()` gave. For files and typed commands, never a hot path: every call may allocate.
     *
     * Copyable and movable (it holds only strings).
     */
    class JsonMemberMap {
    public:
        void Clear() {
            _members.clear();
        }

        [[nodiscard]] bool Empty() const {
            return _members.empty();
        }

        [[nodiscard]] size_t Size() const {
            return _members.size();
        }

        /// Sets @p key to the JSON text @p valueText, replacing a value of that key. The text is kept as given: one that is not a JSON value is written as `null`
        void Set(StringView key, StringView valueText);

        /// Sets @p key to what @p write writes into a compact writer. A write that does not complete one value stores nothing and leaves the map as it was
        void SetWith(StringView key, FunctionRef<void(JsonWriter&)> write);

        /// The text of the value of @p key, nullptr when there is none; valid until the map changes
        [[nodiscard]] const String* Find(StringView key) const;

        [[nodiscard]] bool Contains(StringView key) const {
            return Find(key) != nullptr;
        }

        void Erase(StringView key);

        /// Replaces the content with the members of @p object whose decoded key is not in @p known (the first of equal keys, the value as its raw text);
        /// a view that is not an object leaves the map empty
        void Assign(JsonView object, std::initializer_list<StringView> known);

        /// Writes the map as one object: the members sorted by key, each value through WriteJsonSorted, so an indented writer indents every level
        void Write(JsonWriter& writer) const;

    private:
        struct Member {
            String key;
            String text;
        };

        /// The first member that is not before @p key
        [[nodiscard]] Vector<Member>::const_iterator LowerBound(StringView key) const;
        Member& Slot(StringView key);

        Vector<Member> _members;
    };

}  // namespace BECore
