#include "JsonWriteReflect.h"

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonSerializer.h>

namespace BECore::JsonWrite {

    bool MemberMerge::Before(JsonWriter& w, StringView limit) {
        while (_next < _count && _keys[_next].compare(limit) < 0) {
            WriteNext(w);
        }
        const bool outerFree = _outer == nullptr || _outer->Before(w, limit);
        if (_next < _count && _keys[_next] == limit) {
            if (outerFree) {
                _write(w, _next);
            }
            ++_next;
            return false;
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

    void WriteSerializedMember(JsonWriter& w, StringView key, FunctionRef<void(ISerializer&)> save) {
        if (!w.IsValid()) {
            return;
        }
        JsonSerializer serializer;
        save(serializer);
        const Json root = serializer.Take();
        const Json* member = FindJsonMember(root, key);
        if (member == nullptr) {
            return;
        }
        w.Key(key);
        WriteJson(w, *member);
    }

    void WriteSerializedObject(JsonWriter& w, FunctionRef<void(ISerializer&)> serialize) {
        if (!w.IsValid()) {
            return;
        }
        JsonSerializer serializer;
        serialize(serializer);
        WriteJson(w, serializer.Take());
    }

}  // namespace BECore::JsonWrite
