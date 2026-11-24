#include "JsonMemberMap.h"

#include <BECore/Control/JsonViewOps.h>
#include <EASTL/algorithm.h>

namespace BECore {

    namespace {

        StringView AsView(const String& text) {
            return StringView(text.c_str(), text.size());
        }

    }  // namespace

    Vector<JsonMemberMap::Member>::const_iterator JsonMemberMap::LowerBound(StringView key) const {
        return eastl::lower_bound(_members.begin(), _members.end(), key, [](const Member& member, StringView name) { return AsView(member.key).compare(name) < 0; });
    }

    JsonMemberMap::Member& JsonMemberMap::Slot(StringView key) {
        const auto position = LowerBound(key);
        if (position != _members.end() && AsView(position->key) == key) {
            return _members[static_cast<size_t>(position - _members.begin())];
        }
        const auto inserted = _members.insert(position, Member{String(key.data(), key.size()), String()});
        return *inserted;
    }

    void JsonMemberMap::Set(StringView key, StringView valueText) {
        Slot(key).text.assign(valueText.data(), valueText.size());
    }

    void JsonMemberMap::SetWith(StringView key, FunctionRef<void(JsonWriter&)> write) {
        String text;
        JsonWriter writer(text);
        write(writer);
        if (!writer.IsComplete()) {
            return;
        }
        Slot(key).text = eastl::move(text);
    }

    const String* JsonMemberMap::Find(StringView key) const {
        const auto position = LowerBound(key);
        return position != _members.end() && AsView(position->key) == key ? &position->text : nullptr;
    }

    void JsonMemberMap::Erase(StringView key) {
        const auto position = LowerBound(key);
        if (position != _members.end() && AsView(position->key) == key) {
            _members.erase(position);
        }
    }

    void JsonMemberMap::Assign(JsonView object, std::initializer_list<StringView> known) {
        Clear();
        String key;
        for (const JsonMember member : object.Members()) {
            member.key.ReadText(key);
            const StringView name = AsView(key);
            bool isKnown = false;
            for (const StringView knownKey : known) {
                isKnown = isKnown || knownKey == name;
            }
            if (!isKnown && !Contains(name)) {
                Set(name, member.value.Raw());
            }
        }
    }

    void JsonMemberMap::Write(JsonWriter& writer) const {
        writer.BeginObject();
        JsonTape tape;
        for (const Member& member : _members) {
            writer.Key(AsView(member.key));
            if (tape.Parse(AsView(member.text))) {
                WriteJsonSorted(writer, JsonView(tape, 0));
            } else {
                writer.Null();
            }
        }
        writer.EndObject();
    }

}  // namespace BECore
