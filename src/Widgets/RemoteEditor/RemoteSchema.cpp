#include "RemoteSchema.h"

#include <BECore/Control/ControlParams.h>
#include <EASTL/algorithm.h>

namespace BECore {

    namespace {

        using namespace RemoteSchemaLimits;

        String ReadString(JsonView object, StringView key) {
            return GetParamString(object, key).value_or(String{});
        }

        /// The items of the member @p key; none when it is absent or no array
        Detail::JsonChildRange<JsonView, 0> ItemsOf(JsonView object, StringView key) {
            return FindParam(object, key).value_or(JsonView{}).Items();
        }

        Vector<String> ReadStrings(JsonView object, StringView key) {
            Vector<String> texts;
            for (const JsonView item : ItemsOf(object, key)) {
                if (String text; item.ReadText(text)) {
                    texts.push_back(std::move(text));
                }
            }
            return texts;
        }

        TypeKind ReadKind(JsonView type) {
            const Optional<JsonView> kind = FindParam(type, "kind");
            if (!kind) {
                return TypeKind::Unknown;
            }
            if (kind->TextEquals("any")) {
                return TypeKind::Opaque;
            }
            for (const TypeKind candidate : EnumUtils<TypeKind>::Values()) {
                if (kind->TextEquals(TypeKindName(candidate))) {
                    return candidate;
                }
            }
            return TypeKind::Unknown;
        }

        void ReadTypeDesc(JsonView source, size_t depth, RemoteTypeDesc& out) {
            if (depth >= kMaxTypeDepth || !source.IsObject()) {
                return;
            }
            out.kind = ReadKind(source);
            out.object = ReadString(source, "object");
            out.values = ReadStrings(source, "values");
            if (const Optional<JsonView> element = FindParam(source, "element")) {
                out.element = MakeUnique<RemoteTypeDesc>();
                ReadTypeDesc(*element, depth + 1, *out.element);
            }
            if (const Optional<JsonView> key = FindParam(source, "key")) {
                out.key = MakeUnique<RemoteTypeDesc>();
                ReadTypeDesc(*key, depth + 1, *out.key);
            }
            for (const JsonView alternative : ItemsOf(source, "alternatives")) {
                ReadTypeDesc(alternative, depth + 1, out.alternatives.emplace_back());
            }
        }

        FieldAttributes ReadAttributes(JsonView source) {
            FieldAttributes attributes;
            attributes.hidden = GetParamBool(source, "hidden").value_or(false);
            attributes.readOnly = GetParamBool(source, "readonly").value_or(false);
            attributes.step = GetParamNumber(source, "step").value_or(0.0);
            Vector<double> range;
            for (const JsonView bound : ItemsOf(source, "range")) {
                if (const Optional<double> number = bound.AsDouble()) {
                    range.push_back(*number);
                }
            }
            if (range.size() == 2 && range[0] <= range[1]) {
                attributes.hasRange = true;
                attributes.rangeMin = range[0];
                attributes.rangeMax = range[1];
            }
            return attributes;
        }

        /// Points the views of @p type at its own strings, which must not move afterwards
        void BindViews(RemoteTypeDesc& type) {
            type.valueNames.assign(type.values.begin(), type.values.end());
            if (type.element) {
                BindViews(*type.element);
            }
            if (type.key) {
                BindViews(*type.key);
            }
            for (RemoteTypeDesc& alternative : type.alternatives) {
                BindViews(alternative);
            }
        }

    }  // namespace

    bool RemoteSchema::Load(StringView result) {
        if (result == StringView(_text)) {
            return false;
        }
        Clear();
        _text.assign(result.data(), result.size());
        if (!_tape.Parse(_text)) {
            return true;
        }
        const JsonView root(_tape, 0);
        for (const JsonView source : ItemsOf(root, "types")) {
            RemoteType type;
            type.name = ReadString(source, "name");
            if (type.name.empty()) {
                continue;
            }
            type.parent = ReadString(source, "parent");
            for (const JsonView field : ItemsOf(source, "fields")) {
                if (type.fields.size() >= kMaxFields) {
                    break;
                }
                RemoteField read;
                read.name = ReadString(field, "name");
                const Optional<JsonView> valueType = FindParam(field, "type");
                if (read.name.empty() || !valueType) {
                    continue;
                }
                ReadTypeDesc(*valueType, 0, read.type);
                const JsonView attributes = FindParam(field, "attributes").value_or(JsonView{});
                read.attributes = ReadAttributes(attributes);
                read.tooltip = ReadString(attributes, "tooltip");
                type.fields.push_back(std::move(read));
            }
            _types.push_back(std::move(type));
        }
        for (const JsonView source : ItemsOf(root, "factories")) {
            _factories.push_back({ReadString(source, "base"), ReadStrings(source, "derived")});
        }
        // Nothing is added from here on, so a view of a string stays good
        for (size_t i = 0; i < _types.size(); ++i) {
            _typesByName.emplace(StringView(_types[i].name), i);
            for (RemoteField& field : _types[i].fields) {
                field.attributes.tooltip = field.tooltip;
                BindViews(field.type);
            }
        }
        return true;
    }

    void RemoteSchema::Clear() {
        _typesByName.clear();
        _types.clear();
        _factories.clear();
        _text.clear();
    }

    const RemoteType* RemoteSchema::FindType(StringView name) const {
        const auto found = _typesByName.find(name);
        return found != _typesByName.end() ? &_types[found->second] : nullptr;
    }

    RemoteTypeChain RemoteSchema::CollectChain(const RemoteType& type) const {
        RemoteTypeChain chain;
        for (const RemoteType* current = &type; current != nullptr && !chain.full(); current = FindType(current->parent)) {
            if (eastl::find(chain.begin(), chain.end(), current) != chain.end()) {
                break;
            }
            chain.push_back(current);
        }
        eastl::reverse(chain.begin(), chain.end());
        return chain;
    }

    bool RemoteSchema::IsKindOf(const RemoteType& type, StringView base) const {
        for (const RemoteType* ancestor : CollectChain(type)) {
            if (StringView(ancestor->name) == base) {
                return true;
            }
        }
        for (const Factory& factory : _factories) {
            if (StringView(factory.base) == base) {
                return eastl::find(factory.derived.begin(), factory.derived.end(), type.name) != factory.derived.end();
            }
        }
        return false;
    }

}  // namespace BECore
