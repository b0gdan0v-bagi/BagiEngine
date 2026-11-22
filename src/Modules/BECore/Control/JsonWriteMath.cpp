#include "JsonWriteMath.h"

#include <BECore/Math/IVec2.h>
#include <BECore/Math/Quat.h>
#include <BECore/Math/Vec2.h>
#include <BECore/Math/Vec3.h>
#include <BECore/Math/Vec4.h>

namespace BECore::JsonWrite {

    // The keys of every overload are in the order a `Json` object sorts them, which is the order JsonSerializer + DumpJson give.

    void WriteMath(JsonWriter& w, const Vec2& value) {
        w.BeginObject();
        w.Member("x", value.x);
        w.Member("y", value.y);
        w.EndObject();
    }

    void WriteMath(JsonWriter& w, const Vec3& value) {
        w.BeginObject();
        w.Member("x", value.x);
        w.Member("y", value.y);
        w.Member("z", value.z);
        w.EndObject();
    }

    void WriteMath(JsonWriter& w, const Vec4& value) {
        w.BeginObject();
        w.Member("w", value.w);
        w.Member("x", value.x);
        w.Member("y", value.y);
        w.Member("z", value.z);
        w.EndObject();
    }

    void WriteMath(JsonWriter& w, const IVec2& value) {
        w.BeginObject();
        w.Member("x", value.x);
        w.Member("y", value.y);
        w.EndObject();
    }

    void WriteMath(JsonWriter& w, const Quat& value) {
        w.BeginObject();
        w.Member("w", value.w);
        w.Member("x", value.x);
        w.Member("y", value.y);
        w.Member("z", value.z);
        w.EndObject();
    }

    void WriteMath(JsonWriter& w, const Color& value) {
        w.BeginObject();
        w.Member("a", value.a);
        w.Member("b", value.b);
        w.Member("g", value.g);
        w.Member("r", value.r);
        w.EndObject();
    }

    void WriteMath(JsonWriter& w, const Rect& value) {
        w.BeginObject();
        w.Member("h", value.h);
        w.Member("w", value.w);
        w.Member("x", value.x);
        w.Member("y", value.y);
        w.EndObject();
    }

}  // namespace BECore::JsonWrite
