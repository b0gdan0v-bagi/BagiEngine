#include "JsonStreamSerializer.h"

namespace BECore {

    namespace {

        constexpr StringView kElementValueKey = "value";

    }  // namespace

    JsonStreamSerializer::JsonStreamSerializer(JsonWriter& writer) {
        Restart(writer);
    }

    JsonStreamSerializer::~JsonStreamSerializer() = default;

    void JsonStreamSerializer::Restart(JsonWriter& writer) {
        _writer = &writer;
        _scopes.clear();
        _names.clear();
        _nameText.clear();
        _pending.clear();
        if (writer.GetDepth() >= JsonWriter::kMaxDepth) {
            return;
        }
        writer.BeginObject();
        _scopes.push_back(Scope{});
    }

    void JsonStreamSerializer::Finish() {
        while (_scopes.size() > 1) {
            switch (_scopes.back().kind) {
                case JsonStreamScopeKind::Object:
                    EndObject();
                    break;
                case JsonStreamScopeKind::Array:
                    EndArray();
                    break;
                case JsonStreamScopeKind::Element:
                    EndArrayElement();
                    break;
            }
        }
        if (_scopes.size() == 1) {
            _writer->EndObject();
            _scopes.clear();
        }
    }

    bool JsonStreamSerializer::FindName(const Scope& scope, StringView name) const {
        for (size_t i = scope.namesBegin; i < _names.size(); ++i) {
            const NameSpan& span = _names[i];
            if (StringView(_nameText.data() + span.offset, span.length) == name) {
                return true;
            }
        }
        return false;
    }

    void JsonStreamSerializer::RecordName(StringView name) {
        _names.push_back(NameSpan{static_cast<uint32_t>(_nameText.size()), static_cast<uint32_t>(name.size())});
        _nameText.append(name.data(), name.size());
    }

    void JsonStreamSerializer::OpenElement(Scope& scope) {
        _writer->BeginObject();
        scope.isOpen = true;
        if (scope.hasPending) {
            _writer->Key(kElementValueKey);
            _writer->Raw(_pending);
            scope.hasPending = false;
        }
    }

    bool JsonStreamSerializer::CanOpenContainer() const {
        const bool elementNotOpen = _scopes.back().kind == JsonStreamScopeKind::Element && !_scopes.back().isOpen;
        return _writer->GetDepth() + (elementNotOpen ? 2 : 1) <= JsonWriter::kMaxDepth;
    }

    void JsonStreamSerializer::PopScope() {
        _names.resize(_scopes.back().namesBegin);
        _nameText.resize(_scopes.back().textBegin);
        _scopes.pop_back();
    }

    JsonStreamPlace JsonStreamSerializer::Prepare(StringView name, bool isScalar) {
        Scope& top = _scopes.back();
        if (top.kind == JsonStreamScopeKind::Array) {
            return JsonStreamPlace::Skip;
        }
        if (FindName(top, name)) {
            // The one repeat that is not dropped: the held `value` is not written yet, so the later one replaces it
            return isScalar && top.hasPending && name == kElementValueKey ? JsonStreamPlace::Pending : JsonStreamPlace::Skip;
        }
        if (top.kind == JsonStreamScopeKind::Element && !top.isOpen) {
            if (isScalar && name == kElementValueKey && _names.size() == top.namesBegin) {
                RecordName(name);
                top.hasPending = true;
                return JsonStreamPlace::Pending;
            }
            OpenElement(top);
        }
        RecordName(name);
        _writer->Key(name);
        return JsonStreamPlace::Write;
    }

    template <typename T>
    void JsonStreamSerializer::PutScalar(StringView name, const T& value) {
        if (_scopes.empty()) {
            return;
        }
        switch (Prepare(name, true)) {
            case JsonStreamPlace::Skip:
                return;
            case JsonStreamPlace::Pending: {
                _pending.clear();
                JsonWriter held(_pending);
                held.Value(value);
                return;
            }
            case JsonStreamPlace::Write:
                _writer->Value(value);
                return;
        }
    }

    void JsonStreamSerializer::Write(StringView name, bool value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::Write(StringView name, int8_t value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::Write(StringView name, uint8_t value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::Write(StringView name, int16_t value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::Write(StringView name, uint16_t value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::Write(StringView name, int32_t value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::Write(StringView name, uint32_t value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::Write(StringView name, int64_t value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::Write(StringView name, uint64_t value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::Write(StringView name, float value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::Write(StringView name, double value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::Write(StringView name, StringView value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::Write(StringView name, const PoolString& value) {
        PutScalar(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, bool value) {
        Write(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, int8_t value) {
        Write(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, uint8_t value) {
        Write(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, int16_t value) {
        Write(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, uint16_t value) {
        Write(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, int32_t value) {
        Write(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, uint32_t value) {
        Write(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, int64_t value) {
        Write(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, uint64_t value) {
        Write(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, float value) {
        Write(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, double value) {
        Write(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, StringView value) {
        Write(name, value);
    }

    void JsonStreamSerializer::WriteAttribute(StringView name, const PoolString& value) {
        Write(name, value);
    }

    bool JsonStreamSerializer::BeginObject(StringView name) {
        if (_scopes.empty() || _scopes.back().kind == JsonStreamScopeKind::Array || !CanOpenContainer()) {
            return false;
        }
        if (Prepare(name, false) == JsonStreamPlace::Skip) {
            return false;
        }
        _writer->BeginObject();
        _scopes.push_back(Scope{JsonStreamScopeKind::Object, _names.size(), _nameText.size()});
        return true;
    }

    void JsonStreamSerializer::EndObject() {
        if (_scopes.size() > 1 && _scopes.back().kind == JsonStreamScopeKind::Object) {
            _writer->EndObject();
            PopScope();
        }
    }

    bool JsonStreamSerializer::BeginArray(StringView name, StringView /*elementName*/, size_t& /*count*/) {
        if (_scopes.empty() || _scopes.back().kind == JsonStreamScopeKind::Array || !CanOpenContainer()) {
            return false;
        }
        if (Prepare(name, false) == JsonStreamPlace::Skip) {
            return false;
        }
        _writer->BeginArray();
        _scopes.push_back(Scope{JsonStreamScopeKind::Array, _names.size(), _nameText.size()});
        return true;
    }

    void JsonStreamSerializer::EndArray() {
        if (_scopes.size() > 1 && _scopes.back().kind == JsonStreamScopeKind::Array) {
            _writer->EndArray();
            PopScope();
        }
    }

    bool JsonStreamSerializer::BeginArrayElement() {
        // The element's object is opened later, one level under the array: it has to fit
        if (_scopes.empty() || _scopes.back().kind != JsonStreamScopeKind::Array || _writer->GetDepth() >= JsonWriter::kMaxDepth) {
            return false;
        }
        Scope element{JsonStreamScopeKind::Element, _names.size(), _nameText.size()};
        element.isOpen = false;
        _scopes.push_back(element);
        return true;
    }

    void JsonStreamSerializer::EndArrayElement() {
        if (_scopes.size() < 2 || _scopes.back().kind != JsonStreamScopeKind::Element) {
            return;
        }
        Scope& element = _scopes.back();
        if (element.isOpen) {
            _writer->EndObject();
        } else if (element.hasPending) {
            // An element that holds only a scalar `value` is that scalar
            _writer->Raw(_pending);
        } else {
            _writer->BeginObject();
            _writer->EndObject();
        }
        PopScope();
    }

}  // namespace BECore
