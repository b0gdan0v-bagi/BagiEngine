#include "FieldResolver.h"

namespace BECore {

    namespace {

        /// The kinds a slot or a list instruction reads at one offset; every other kind (a map, an optional, a pointer) is behind a path a context never links
        [[nodiscard]] bool IsReadableAsOne(TypeKind kind) {
            switch (kind) {
                case TypeKind::Bool:
                case TypeKind::Int8:
                case TypeKind::Int16:
                case TypeKind::Int32:
                case TypeKind::Int64:
                case TypeKind::UInt8:
                case TypeKind::UInt16:
                case TypeKind::UInt32:
                case TypeKind::UInt64:
                case TypeKind::Float:
                case TypeKind::Double:
                case TypeKind::String:
                case TypeKind::Enum:
                case TypeKind::Opaque:
                case TypeKind::List:
                    return true;
                default:
                    return false;
            }
        }

        /// The field of @p type (its parents' too) that starts at @p offset from the object, an embedded object entered; @p prefix is the path of the object
        Optional<ResolvedField> Find(const TypeInfo& type, size_t offset, const String& prefix, const TypeRegistry& registry) {
            for (const FieldRef& ref : registry.AllFields(type)) {
                if (ref.field == nullptr || ref.field->type == nullptr || offset < ref.offset) {
                    continue;
                }
                const TypeDesc& desc = *ref.field->type;
                const String path = prefix.empty() ? String(ref.field->name) : Format("{}.{}", prefix, ref.field->name);
                if (desc.kind == TypeKind::Object) {
                    const TypeInfo* inner = registry.Find(desc.ObjectHash());
                    if (inner == nullptr) {
                        continue;
                    }
                    if (Optional<ResolvedField> found = Find(*inner, offset - ref.offset, path, registry)) {
                        return found;
                    }
                    continue;
                }
                if (offset == ref.offset && IsReadableAsOne(desc.kind)) {
                    return ResolvedField{path, &desc};
                }
            }
            return std::nullopt;
        }

    }  // namespace

    Optional<ResolvedField> ResolveField(const ContextDecl& declaration, LevelIndex level, uint32_t offset, const TypeRegistry& registry) {
        if (level >= declaration.Size()) {
            return std::nullopt;
        }
        const ContextLevel& owner = declaration.Levels()[level];
        if (owner.type == nullptr) {
            return std::nullopt;
        }
        return Find(*owner.type, offset, owner.name, registry);
    }

    Optional<ResolvedField> ResolveElementField(const TypeInfo& element, uint32_t offset, const TypeRegistry& registry) {
        return Find(element, offset, String(), registry);
    }

}  // namespace BECore
