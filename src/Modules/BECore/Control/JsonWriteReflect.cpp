#include "JsonWriteReflect.h"

#include <BECore/Control/JsonStreamSerializer.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonViewOps.h>

namespace BECore::JsonWrite {

    bool MemberMerge::Before(JsonWriter& w, StringView limit) {
        while (_next < _count && _keys[_next].compare(limit) < 0) {
            WriteNext(w);
        }
        const bool outerFree = _outer == nullptr || _outer->Before(w, limit);
        if (_next < _count && _keys[_next] == limit) {
            const bool ownsTie = _tie == MergeTie::ThisLevel;
            if (outerFree && ownsTie) {
                _write(w, _next);
            }
            ++_next;
            return ownsTie ? false : outerFree;
        }
        return outerFree;
    }

    void MemberMerge::Finish(JsonWriter& w) {
        while (_next < _count) {
            WriteNext(w);
        }
        if (_outer != nullptr) {
            _outer->Finish(w);
        }
    }

    void MemberMerge::WriteNext(JsonWriter& w) {
        if (_outer == nullptr || _outer->Before(w, _keys[_next])) {
            _write(w, _next);
        }
        ++_next;
    }

    namespace {

        /// Runs @p save into a `JsonStreamSerializer` over @p text and tapes the document; false (and the failure of @p w) when it does not tape.
        /// The stream keeps the members in write order and the first of a repeated name; `WriteJsonSorted` then puts them in byte order.
        bool SaveAndTape(JsonWriter& w, FunctionRef<void(ISerializer&)> save, String& text, JsonTape& tape) {
            JsonWriter scratch(text);
            JsonStreamSerializer serializer(scratch);
            save(serializer);
            serializer.Finish();
            if (scratch.IsComplete() && tape.Parse(text).has_value()) {
                return true;
            }
            w.Fail(MakeControlError(ControlErrors::kInternalError, "A serialized value did not make a JSON document"));
            return false;
        }

    }  // namespace

    // The bridge for what has no native writer: a scratch String and a tape are made on every call, so it is never a hot path (the native
    // writers are); a type that has to be fast gets a `WriteJson` of its own.
    void WriteSerializedMember(JsonWriter& w, StringView key, FunctionRef<void(ISerializer&)> save) {
        if (!w.IsValid()) {
            return;
        }
        String text;
        JsonTape tape;
        if (!SaveAndTape(w, save, text, tape)) {
            return;
        }
        const Optional<JsonView> member = JsonView(tape, 0).Find(key);
        if (!member.has_value()) {
            return;
        }
        w.Key(key);
        WriteJsonSorted(w, *member);
    }

    void WriteSerializedObject(JsonWriter& w, FunctionRef<void(ISerializer&)> serialize) {
        if (!w.IsValid()) {
            return;
        }
        String text;
        JsonTape tape;
        if (!SaveAndTape(w, serialize, text, tape)) {
            return;
        }
        WriteJsonSorted(w, JsonView(tape, 0));
    }

}  // namespace BECore::JsonWrite
