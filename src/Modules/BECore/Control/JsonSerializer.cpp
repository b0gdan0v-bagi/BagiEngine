#include "JsonSerializer.h"

#include <BECore/Control/Json.h>
#include <cmath>

namespace BECore {

    namespace {

        /// Room for the shortest text of any float, with sign, digits and exponent
        constexpr size_t kFloatTextBytes = 32;

        constexpr StringView kElementValueKey = "value";

        /// A float as the shortest text that reads back to the same float: JSON numbers are doubles, and the double of `0.1f` would be written as
        /// 0.10000000149011612. A value that is not finite has no JSON form and is null.
        Json MakeFloatJson(float value) {
            if (!std::isfinite(value)) {
                return Json(nullptr);
            }
            Array<char, kFloatTextBytes> text{};
            const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
            double shortest = static_cast<double>(value);
            if (written.ec == std::errc()) {
                std::from_chars(text.data(), written.ptr, shortest);
            }
            return Json(shortest);
        }

        Json MakeDoubleJson(double value) {
            return std::isfinite(value) ? Json(value) : Json(nullptr);
        }

    }  // namespace

    JsonSerializer::JsonSerializer() {
        Reset();
    }

    JsonSerializer::~JsonSerializer() = default;

    void JsonSerializer::Reset() {
        _scopes.clear();
        _root = MakeUnique<Json>(Json::object());
        _scopes.push_back({_root.get(), ScopeKind::Object});
    }

    Json JsonSerializer::Take() {
        Json result = eastl::move(*_root);
        Reset();
        return result;
    }

    void JsonSerializer::Put(StringView name, Json&& value) {
        const Scope& top = _scopes.back();
        if (top.kind == ScopeKind::Array) {
            return;
        }
        (*top.node)[ToStdString(name)] = eastl::move(value);
    }

    void JsonSerializer::Write(StringView name, bool value) {
        Put(name, Json(value));
    }

    void JsonSerializer::Write(StringView name, int8_t value) {
        Put(name, Json(static_cast<int64_t>(value)));
    }

    void JsonSerializer::Write(StringView name, uint8_t value) {
        Put(name, Json(static_cast<uint64_t>(value)));
    }

    void JsonSerializer::Write(StringView name, int16_t value) {
        Put(name, Json(static_cast<int64_t>(value)));
    }

    void JsonSerializer::Write(StringView name, uint16_t value) {
        Put(name, Json(static_cast<uint64_t>(value)));
    }

    void JsonSerializer::Write(StringView name, int32_t value) {
        Put(name, Json(static_cast<int64_t>(value)));
    }

    void JsonSerializer::Write(StringView name, uint32_t value) {
        Put(name, Json(static_cast<uint64_t>(value)));
    }

    void JsonSerializer::Write(StringView name, int64_t value) {
        Put(name, Json(value));
    }

    void JsonSerializer::Write(StringView name, uint64_t value) {
        Put(name, Json(value));
    }

    void JsonSerializer::Write(StringView name, float value) {
        Put(name, MakeFloatJson(value));
    }

    void JsonSerializer::Write(StringView name, double value) {
        Put(name, MakeDoubleJson(value));
    }

    void JsonSerializer::Write(StringView name, StringView value) {
        Put(name, MakeJsonString(value));
    }

    void JsonSerializer::Write(StringView name, const PoolString& value) {
        Put(name, MakeJsonString(value.ToStringView()));
    }

    void JsonSerializer::WriteAttribute(StringView name, bool value) {
        Write(name, value);
    }

    void JsonSerializer::WriteAttribute(StringView name, int8_t value) {
        Write(name, value);
    }

    void JsonSerializer::WriteAttribute(StringView name, uint8_t value) {
        Write(name, value);
    }

    void JsonSerializer::WriteAttribute(StringView name, int16_t value) {
        Write(name, value);
    }

    void JsonSerializer::WriteAttribute(StringView name, uint16_t value) {
        Write(name, value);
    }

    void JsonSerializer::WriteAttribute(StringView name, int32_t value) {
        Write(name, value);
    }

    void JsonSerializer::WriteAttribute(StringView name, uint32_t value) {
        Write(name, value);
    }

    void JsonSerializer::WriteAttribute(StringView name, int64_t value) {
        Write(name, value);
    }

    void JsonSerializer::WriteAttribute(StringView name, uint64_t value) {
        Write(name, value);
    }

    void JsonSerializer::WriteAttribute(StringView name, float value) {
        Write(name, value);
    }

    void JsonSerializer::WriteAttribute(StringView name, double value) {
        Write(name, value);
    }

    void JsonSerializer::WriteAttribute(StringView name, StringView value) {
        Write(name, value);
    }

    void JsonSerializer::WriteAttribute(StringView name, const PoolString& value) {
        Write(name, value);
    }

    bool JsonSerializer::BeginObject(StringView name) {
        const Scope& top = _scopes.back();
        if (top.kind == ScopeKind::Array) {
            return false;
        }
        Json& member = (*top.node)[ToStdString(name)];
        if (!member.is_object()) {
            member = Json::object();
        }
        _scopes.push_back({&member, ScopeKind::Object});
        return true;
    }

    void JsonSerializer::EndObject() {
        if (_scopes.size() > 1 && _scopes.back().kind == ScopeKind::Object) {
            _scopes.pop_back();
        }
    }

    bool JsonSerializer::BeginArray(StringView name, StringView /*elementName*/, size_t& /*count*/) {
        const Scope& top = _scopes.back();
        if (top.kind == ScopeKind::Array) {
            return false;
        }
        Json& member = (*top.node)[ToStdString(name)];
        member = Json::array();
        _scopes.push_back({&member, ScopeKind::Array});
        return true;
    }

    void JsonSerializer::EndArray() {
        if (_scopes.back().kind == ScopeKind::Array) {
            _scopes.pop_back();
        }
    }

    bool JsonSerializer::BeginArrayElement() {
        const Scope& top = _scopes.back();
        if (top.kind != ScopeKind::Array) {
            return false;
        }
        Json* const array = top.node;
        array->push_back(Json::object());
        _scopes.push_back({&array->back(), ScopeKind::Element});
        return true;
    }

    void JsonSerializer::EndArrayElement() {
        if (_scopes.back().kind != ScopeKind::Element) {
            return;
        }
        // An element that holds only a scalar `value` is that scalar: a list of numbers reads [1, 2, 3]
        Json& element = *_scopes.back().node;
        if (element.size() == 1) {
            const auto member = element.find(ToStdString(kElementValueKey));
            if (member != element.end() && !member->is_object() && !member->is_array()) {
                Json scalar = eastl::move(*member);
                element = eastl::move(scalar);
            }
        }
        _scopes.pop_back();
    }

}  // namespace BECore
