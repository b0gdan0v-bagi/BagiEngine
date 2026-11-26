#include "RemoteFieldWalker.h"

#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <EASTL/algorithm.h>
#include <cmath>

namespace BECore {

    namespace {

        /// The attributes outside every field
        constexpr FieldAttributes kNoAttributes{};

        /// Room for the label of a list element, `[i]`
        constexpr size_t kLabelCapacity = 32;

        /// The member of a map entry that holds the key, and the one a scalar value (or the only member of an object) is under
        constexpr StringView kKeyMember = "key";
        constexpr StringView kValueMember = "value";

        /// The alternative of a `PropertyValue` a field of type @p T is written as
        template <typename T>
        using WireType = std::conditional_t<std::is_same_v<T, bool>, bool, std::conditional_t<std::is_floating_point_v<T>, double, std::conditional_t<std::is_signed_v<T>, int64_t, uint64_t>>>;

        /// @p value as a field of type @p T takes it; nullopt when it is of another kind or does not fit
        template <typename T>
        Optional<T> ReadNumber(JsonView value) {
            if constexpr (std::is_same_v<T, bool>) {
                return value.AsBool();
            } else {
                Optional<WireType<T>> read;
                if constexpr (std::is_floating_point_v<T>) {
                    read = value.AsDouble();
                    if (read && !(std::abs(*read) <= std::numeric_limits<T>::max())) {
                        return {};
                    }
                } else {
                    if constexpr (std::is_signed_v<T>) {
                        read = value.AsInt64();
                    } else {
                        read = value.AsUint64();
                    }
                    if (read && (*read < std::numeric_limits<T>::min() || *read > std::numeric_limits<T>::max())) {
                        return {};
                    }
                }
                if (!read) {
                    return {};
                }
                return static_cast<T>(*read);
            }
        }

        /// An object a math type or a colour writes: the schema names no type for it, so its members are known by their names
        struct NumberGroup {
            /// In the order the type's `VisitProperties` visits them
            Span<const StringView> names;
            TypeKind kind;
        };

        constexpr StringView kColorNames[] = {"r", "g", "b", "a"};
        constexpr StringView kRectNames[] = {"x", "y", "w", "h"};
        constexpr StringView kVec4Names[] = {"x", "y", "z", "w"};
        constexpr StringView kVec3Names[] = {"x", "y", "z"};
        constexpr StringView kVec2Names[] = {"x", "y"};

        /// The first group that fits counts: an integer vector is told from a float one by its numbers, written with no fraction
        const NumberGroup kNumberGroups[] = {{kColorNames, TypeKind::UInt8}, {kRectNames, TypeKind::Float}, {kVec4Names, TypeKind::Float},
                                             {kVec3Names, TypeKind::Float},  {kVec2Names, TypeKind::Int32}, {kVec2Names, TypeKind::Float}};

        bool Fits(TypeKind kind, JsonView value) {
            switch (kind) {
                case TypeKind::UInt8:
                    return ReadNumber<uint8_t>(value).has_value();
                case TypeKind::Int32:
                    return ReadNumber<int32_t>(value).has_value();
                default:
                    return ReadNumber<float>(value).has_value();
            }
        }

        /// Fields the generated visitor passes over by a stub of their type (`UI/UIVariant.h`), which a schema cannot say
        bool IsPassedOver(const RemoteType& type, const RemoteField& field) {
            return type.name == "UICanvasComponent" && field.name == "variants";
        }

        /// Appends `["key"]` as a property path quotes a text key
        void AppendTextKey(String& path, StringView key) {
            path.append("[\"");
            for (const char c : key) {
                if (c == '"' || c == '\\') {
                    path.push_back('\\');
                }
                path.push_back(c);
            }
            path.append("\"]");
        }

    }  // namespace

    bool RemoteFieldWalker::Walk(const RemoteSchema& schema, const RemoteType& type, JsonView fields, const RemoteWriteTarget& component, RemotePendingWrites& writes, IPropertyVisitor& visitor) {
        _schema = &schema;
        _writes = &writes;
        _visitor = &visitor;
        _component = component;
        _attributes = &kNoAttributes;
        _locked = false;
        _changed = false;
        _path.clear();
        if (fields.IsObject()) {
            WalkMembers(type, fields, 0);
        }
        return _changed;
    }

    void RemoteFieldWalker::WalkMembers(const RemoteType& type, JsonView object, size_t depth) {
        uint32_t cursor = 0;
        for (const RemoteType* part : _schema->CollectChain(type)) {
            for (const RemoteField& field : part->fields) {
                if (!IsPassedOver(*part, field)) {
                    WalkField(field, object.Find(field.name, cursor).value_or(JsonView{}), depth);
                }
            }
        }
    }

    void RemoteFieldWalker::WalkField(const RemoteField& field, JsonView value, size_t depth) {
        // An empty Optional writes no member, so only that field is there without one
        if (!value.IsValid() && field.type.kind != TypeKind::Optional) {
            return;
        }
        const size_t length = _path.size();
        const FieldAttributes* const outer = _attributes;
        const bool wasLocked = _locked;
        AppendMember(field.name);
        _attributes = &field.attributes;
        _locked = wasLocked || field.attributes.readOnly || field.attributes.hidden;
        PropVisit::VisitField(*_visitor, field.attributes, [&] {
            WalkValue(field.type, value, field.name, depth);
            return false;
        });
        _attributes = outer;
        _locked = wasLocked;
        _path.resize(length);
    }

    void RemoteFieldWalker::WalkValue(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth) {
        if (depth >= kMaxDepth || (!value.IsValid() && type.kind != TypeKind::Optional) || WalkScalar(type.kind, value, name)) {
            return;
        }
        switch (type.kind) {
            case TypeKind::String:
                WalkString(value, name);
                break;
            case TypeKind::Enum:
                WalkEnum(type, value, name);
                break;
            case TypeKind::Object:
            case TypeKind::Pointer:
                WalkObject(type, value, name, depth);
                break;
            case TypeKind::List:
                WalkList(type, value, name, depth);
                break;
            case TypeKind::Map:
                WalkMap(type, value, name, depth);
                break;
            case TypeKind::Pair:
                WalkPair(type, value, name, depth);
                break;
            case TypeKind::Optional:
                WalkOptional(type, value, name, depth);
                break;
            case TypeKind::Variant:
                WalkVariant(type, value, name, depth);
                break;
            case TypeKind::Opaque:
                if (!WalkNumberGroup(value, name)) {
                    ShowText(value, name);
                }
                break;
            case TypeKind::Value:
            case TypeKind::Unknown:
                ShowText(value, name);
                break;
            default:
                break;
        }
    }

    bool RemoteFieldWalker::WalkScalar(TypeKind kind, JsonView value, StringView name) {
        switch (kind) {
            case TypeKind::Bool:
                WalkNumber<bool>(value, name);
                return true;
            case TypeKind::Int8:
                WalkNumber<int8_t>(value, name);
                return true;
            case TypeKind::Int16:
                WalkNumber<int16_t>(value, name);
                return true;
            case TypeKind::Int32:
                WalkNumber<int32_t>(value, name);
                return true;
            case TypeKind::Int64:
                WalkNumber<int64_t>(value, name);
                return true;
            case TypeKind::UInt8:
                WalkNumber<uint8_t>(value, name);
                return true;
            case TypeKind::UInt16:
                WalkNumber<uint16_t>(value, name);
                return true;
            case TypeKind::UInt32:
                WalkNumber<uint32_t>(value, name);
                return true;
            case TypeKind::UInt64:
                WalkNumber<uint64_t>(value, name);
                return true;
            case TypeKind::Float:
                WalkNumber<float>(value, name);
                return true;
            case TypeKind::Double:
                WalkNumber<double>(value, name);
                return true;
            default:
                return false;
        }
    }

    template <typename T>
    void RemoteFieldWalker::WalkNumber(JsonView value, StringView name) {
        Optional<T> read = ReadNumber<T>(value);
        if (!read) {
            return;
        }
        T edited = *read;
        if (const PropertyValue* pending = FindOverride()) {
            if (const auto* held = std::get_if<WireType<T>>(pending)) {
                edited = static_cast<T>(*held);
            }
        }
        if (_visitor->Visit(name, edited)) {
            Emit(PropertyValue(std::in_place_type<WireType<T>>, edited));
        }
    }

    void RemoteFieldWalker::WalkString(JsonView value, StringView name) {
        if (!value.ReadText(_text)) {
            return;
        }
        if (const PropertyValue* pending = FindOverride()) {
            if (const String* held = std::get_if<String>(pending)) {
                _text = *held;
            }
        }
        if (_visitor->Visit(name, _text)) {
            Emit(PropertyValue(std::in_place_type<String>, _text), RemoteEditKind::Text);
        }
    }

    void RemoteFieldWalker::WalkEnum(const RemoteTypeDesc& type, JsonView value, StringView name) {
        if (!value.IsString()) {
            return;
        }
        const PropertyValue* pending = FindOverride();
        const String* held = pending != nullptr ? std::get_if<String>(pending) : nullptr;
        const auto isCurrent = [&](const String& candidate) { return held != nullptr ? *held == candidate : value.TextEquals(candidate); };
        const auto found = eastl::find_if(type.values.begin(), type.values.end(), isCurrent);
        if (found == type.values.end()) {
            return;
        }
        auto selected = static_cast<size_t>(found - type.values.begin());
        if (_visitor->VisitEnum(name, type.valueNames.data(), type.valueNames.size(), selected) && selected < type.values.size()) {
            Emit(PropertyValue(std::in_place_type<String>, type.values[selected]));
        }
    }

    void RemoteFieldWalker::WalkObject(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth) {
        const bool isPointer = type.kind == TypeKind::Pointer;
        // A null element of a list of pointers is written as an empty object or as null
        if (isPointer && (value.IsNull() || (value.IsObject() && value.Size() == 0))) {
            return;
        }
        const RemoteType* objectType = _schema->FindType(type.object);
        // An object a factory made names its own class
        if (const Optional<JsonView> typeName = value.Find("type"); isPointer && typeName && typeName->ReadText(_text)) {
            const RemoteType* named = _schema->FindType(_text);
            if (named != nullptr && (type.object.empty() || _schema->IsKindOf(*named, type.object))) {
                objectType = named;
            }
        }
        if (objectType == nullptr) {
            ShowText(value, name);
            return;
        }
        if (value.IsObject()) {
            if (_visitor->BeginCompound(name)) {
                WalkMembers(*objectType, value, depth + 1);
                _visitor->EndCompound();
            }
            return;
        }
        // In a list, an object that writes only its member `value` is that scalar
        for (const RemoteType* part : _schema->CollectChain(*objectType)) {
            for (const RemoteField& field : part->fields) {
                if (field.name == kValueMember && _visitor->BeginCompound(name)) {
                    WalkField(field, value, depth + 1);
                    _visitor->EndCompound();
                    return;
                }
            }
        }
    }

    void RemoteFieldWalker::WalkList(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth) {
        if (!value.IsArray() || !type.element) {
            return;
        }
        // The local dispatch shows a list of pointers as a compound, every other sequence as a list
        const bool pointers = type.element->kind == TypeKind::Pointer;
        if (!(pointers ? _visitor->BeginCompound(name) : _visitor->BeginList(name, ListInfo{value.Size(), false, false}))) {
            return;
        }
        ListRequest request;
        size_t position = 0;
        for (const JsonView item : value.Items()) {
            char label[kLabelCapacity];
            const FormatResult written = FormatToN(label, kLabelCapacity, "[{}]", position);
            const StringView segment(label, written.size);
            const size_t length = _path.size();
            _path.append(segment.data(), segment.size());
            WalkValue(*type.element, item, segment, depth + 1);
            _path.resize(length);
            if (!pointers) {
                _visitor->EndListElement(position, request);
            }
            ++position;
        }
        if (pointers) {
            _visitor->EndCompound();
        } else {
            _visitor->EndList(request);
        }
    }

    void RemoteFieldWalker::WalkMap(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth) {
        if (!value.IsArray() || !type.element || !_visitor->BeginList(name, ListInfo{value.Size(), false, false})) {
            return;
        }
        const TypeKind kind = type.element->kind;
        ListRequest request;
        size_t position = 0;
        for (const JsonView entry : value.Items()) {
            const JsonView key = entry.Find(kKeyMember).value_or(JsonView{});
            char buffer[kLabelCapacity];
            StringView label;
            const size_t length = _path.size();
            if (key.IsString()) {
                if (const Optional<StringView> plain = key.Text()) {
                    label = *plain;
                } else {
                    key.ReadText(_keys[depth]);
                    label = _keys[depth];
                }
                AppendTextKey(_path, label);
            } else if (key.IsInteger()) {
                label = key.Raw();
                AppendFormat(_path, "[{}]", label);
            } else {
                label = StringView(buffer, FormatToN(buffer, kLabelCapacity, "[{}]", position).size);
                _path.append(label.data(), label.size());
            }
            // A scalar value is the member `value`; the members of an object, a variant or a math type sit in the entry itself
            const Optional<JsonView> member = entry.Find(kValueMember);
            const bool inEntry = kind == TypeKind::Object || kind == TypeKind::Pointer || kind == TypeKind::Variant || !member;
            if (kind == TypeKind::Opaque && !member) {
                WalkNumberGroup(entry, label, kKeyMember);
            } else {
                WalkValue(*type.element, inEntry ? entry : *member, label, depth + 1);
            }
            _path.resize(length);
            _visitor->EndListElement(position, request);
            ++position;
        }
        _visitor->EndList(request);
    }

    void RemoteFieldWalker::WalkPair(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth) {
        if (!value.IsObject() || !type.key || !type.element || !_visitor->BeginCompound(name)) {
            return;
        }
        const auto walkMember = [&](const RemoteTypeDesc& memberType, StringView member) {
            const size_t length = _path.size();
            AppendMember(member);
            WalkValue(memberType, value.Find(member).value_or(JsonView{}), member, depth + 1);
            _path.resize(length);
        };
        walkMember(*type.key, "first");
        walkMember(*type.element, "second");
        _visitor->EndCompound();
    }

    void RemoteFieldWalker::WalkOptional(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth) {
        if (!type.element) {
            return;
        }
        const bool present = value.IsValid() && !value.IsNull();
        ReadOnly([&] {
            bool shown = present;
            _visitor->VisitPresence(name, shown);
        });
        if (present) {
            WalkValue(*type.element, value, name, depth + 1);
        }
    }

    void RemoteFieldWalker::WalkVariant(const RemoteTypeDesc& type, JsonView value, StringView name, size_t depth) {
        const Optional<JsonView> index = value.Find("index");
        const Optional<uint64_t> active = index ? index->AsUint64() : Optional<uint64_t>{};
        if (!active || *active >= type.alternatives.size() || !_visitor->BeginCompound(name)) {
            return;
        }
        ReadOnly([&] {
            auto shown = static_cast<size_t>(*active);
            _visitor->VisitAlternative(name, type.alternatives.size(), shown);
        });
        const size_t length = _path.size();
        AppendMember(kValueMember);
        WalkValue(type.alternatives[*active], value.Find(kValueMember).value_or(JsonView{}), kValueMember, depth + 1);
        _path.resize(length);
        _visitor->EndCompound();
    }

    bool RemoteFieldWalker::WalkNumberGroup(JsonView value, StringView name, StringView ignored) {
        if (!value.IsObject()) {
            return false;
        }
        const size_t count = value.Size() - (!ignored.empty() && value.Find(ignored) ? 1 : 0);
        for (const NumberGroup& group : kNumberGroups) {
            const auto fits = [&](StringView member) {
                const Optional<JsonView> number = value.Find(member);
                return number && Fits(group.kind, *number);
            };
            if (group.names.size() != count || !eastl::all_of(group.names.begin(), group.names.end(), fits)) {
                continue;
            }
            if (_visitor->BeginCompound(name)) {
                for (const StringView member : group.names) {
                    const size_t length = _path.size();
                    AppendMember(member);
                    WalkScalar(group.kind, value.Find(member).value_or(JsonView{}), member);
                    _path.resize(length);
                }
                _visitor->EndCompound();
            }
            return true;
        }
        return false;
    }

    void RemoteFieldWalker::ShowText(JsonView value, StringView name) {
        if (!value.ReadText(_text)) {
            const StringView raw = value.Raw();
            _text.assign(raw.data(), raw.size());
        }
        ReadOnly([&] { _visitor->Visit(name, _text); });
    }

    template <typename Fn>
    void RemoteFieldWalker::ReadOnly(Fn&& visit) {
        FieldAttributes attributes = *_attributes;
        attributes.readOnly = true;
        _visitor->BeginField(attributes);
        visit();
        _visitor->EndField();
    }

    void RemoteFieldWalker::AppendMember(StringView member) {
        if (!_path.empty()) {
            _path.push_back('.');
        }
        _path.append(member.data(), member.size());
    }

    RemoteWriteTarget RemoteFieldWalker::Target() const {
        return {_component.node, _component.component, _component.index, _path};
    }

    const PropertyValue* RemoteFieldWalker::FindOverride() const {
        return _writes->Find(Target());
    }

    void RemoteFieldWalker::Emit(PropertyValue value, RemoteEditKind kind) {
        if (!_locked) {
            _writes->Edit(Target(), std::move(value), kind);
            _changed = true;
        }
    }

}  // namespace BECore
