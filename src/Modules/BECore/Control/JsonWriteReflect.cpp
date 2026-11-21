#include "JsonWriteReflect.h"

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonSerializer.h>

namespace BECore::JsonWrite {

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
