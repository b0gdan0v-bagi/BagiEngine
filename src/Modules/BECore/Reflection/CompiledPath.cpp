#include "CompiledPath.h"

namespace BECore {

    namespace {

        std::unexpected<PropertyError> Refuse(PropertyErrorCode code, StringView path, const String& reason) {
            return std::unexpected(PropertyError{code, Format("path '{}': {}", path, reason)});
        }

        /// A value the fixed part cannot enter: what follows it needs the live object
        bool NeedsTheLiveObject(TypeKind kind) {
            switch (kind) {
                case TypeKind::List:
                case TypeKind::Map:
                case TypeKind::Optional:
                case TypeKind::Variant:
                case TypeKind::Pair:
                case TypeKind::Pointer:
                    return true;
                default:
                    return false;
            }
        }

        template <typename T>
        T Load(const char* address) {
            T value;
            std::memcpy(&value, address, sizeof(T));
            return value;
        }

        /// A bool or a number at @p address, read by its kind; empty for every other kind
        Optional<PropertyValue> ReadByKind(TypeKind kind, const char* address) {
            switch (kind) {
                case TypeKind::Bool:
                    return PropertyValue(std::in_place_type<bool>, Load<bool>(address));
                case TypeKind::Int8:
                    return PropertyValue(std::in_place_type<int64_t>, Load<int8_t>(address));
                case TypeKind::Int16:
                    return PropertyValue(std::in_place_type<int64_t>, Load<int16_t>(address));
                case TypeKind::Int32:
                    return PropertyValue(std::in_place_type<int64_t>, Load<int32_t>(address));
                case TypeKind::Int64:
                    return PropertyValue(std::in_place_type<int64_t>, Load<int64_t>(address));
                case TypeKind::UInt8:
                    return PropertyValue(std::in_place_type<uint64_t>, Load<uint8_t>(address));
                case TypeKind::UInt16:
                    return PropertyValue(std::in_place_type<uint64_t>, Load<uint16_t>(address));
                case TypeKind::UInt32:
                    return PropertyValue(std::in_place_type<uint64_t>, Load<uint32_t>(address));
                case TypeKind::UInt64:
                    return PropertyValue(std::in_place_type<uint64_t>, Load<uint64_t>(address));
                case TypeKind::Float:
                    return PropertyValue(std::in_place_type<double>, static_cast<double>(Load<float>(address)));
                case TypeKind::Double:
                    return PropertyValue(std::in_place_type<double>, Load<double>(address));
                default:
                    return std::nullopt;
            }
        }

    }  // namespace

    std::expected<CompiledPath, PropertyError> CompilePath(const TypeInfo& root, StringView path, const TypeRegistry& registry) {
        const auto parsed = ParsePropertyPath(path);
        if (!parsed.has_value()) {
            return Refuse(parsed.error().code, path, parsed.error().message);
        }
        const Vector<PathSegment>& segments = parsed->segments;

        CompiledPath compiled;
        compiled.root = &root;
        compiled.owner = &root;

        const TypeInfo* owner = &root;
        size_t ownerFirst = 0;
        for (size_t i = 0; i < segments.size(); ++i) {
            const PathSegment& segment = segments[i];
            if (segment.kind != PathSegmentKind::Field) {
                return Refuse(PropertyErrorCode::not_found, path, Format("'{}' is a struct or a value, it has no elements", FormatPropertyPath(segments, i)));
            }
            // The first match is the one the visitor finds: the root's own fields come first
            const FieldRef* found = nullptr;
            const Vector<FieldRef> fields = registry.AllFields(*owner);
            for (const FieldRef& candidate : fields) {
                if (candidate.field->name == StringView(segment.text.data(), segment.text.size())) {
                    found = &candidate;
                    break;
                }
            }
            if (found == nullptr) {
                return Refuse(PropertyErrorCode::not_found, path, Format("unknown field: type '{}' has no field '{}'", owner->Name(), segment.text));
            }

            const FieldAttributes& attributes = found->field->attributes;
            compiled.offset = compiled.ownerOffset + found->offset;
            compiled.type = found->field->type;
            compiled.fixedSegments = i + 1;
            compiled.readOnly = compiled.readOnly || attributes.readOnly;
            compiled.hidden = compiled.hidden || attributes.hidden;
            compiled.leaf = attributes;
            if (i + 1 == segments.size()) {
                break;
            }

            const TypeKind kind = found->field->type->kind;
            if (kind == TypeKind::Object) {
                const TypeInfo* embedded = registry.Find(found->field->type->ObjectHash());
                if (embedded == nullptr) {
                    return Refuse(PropertyErrorCode::not_found, path, Format("the type of '{}' is not registered", FormatPropertyPath(segments, i + 1)));
                }
                owner = embedded;
                compiled.owner = embedded;
                compiled.ownerOffset = compiled.offset;
                ownerFirst = i + 1;
            } else if (NeedsTheLiveObject(kind)) {
                const String whole = FormatPropertyPath(segments, segments.size());
                compiled.rest = whole.substr(FormatPropertyPath(segments, i + 1).size());
                break;
            } else {
                return Refuse(PropertyErrorCode::not_found, path, Format("'{}' is a value and has no members", FormatPropertyPath(segments, i + 1)));
            }
        }

        for (size_t i = ownerFirst; i < segments.size(); ++i) {
            compiled.tail.segments.push_back(segments[i]);
        }
        return compiled;
    }

    std::expected<PropertyValue, PropertyError> ReadCompiled(const CompiledPath& path, TypedRef object, const TypeRegistry& registry) {
        const String text = FormatPropertyPath(path.tail.segments, path.tail.segments.size());
        if (!object || path.root == nullptr) {
            return Refuse(PropertyErrorCode::not_found, text, "there is no object");
        }
        const TypedRef base = object.type->Hash() == path.root->Hash() ? object : object.Base(*path.root, registry);
        if (!base) {
            return Refuse(PropertyErrorCode::type_mismatch, text, Format("the object is a '{}', the path was compiled for '{}'", object.type->Name(), path.root->Name()));
        }

        if (path.IsFixed()) {
            if (const Optional<PropertyValue> value = ReadByKind(path.type->kind, static_cast<const char*>(base.ConstAddress()) + path.offset)) {
                return *value;
            }
        }
        const TypedRef owner{static_cast<char*>(base.object) + path.ownerOffset, path.owner, base.isConst};
        return GetProperty(owner, path.tail);
    }

}  // namespace BECore
