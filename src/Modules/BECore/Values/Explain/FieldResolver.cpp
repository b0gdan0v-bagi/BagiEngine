#include "FieldResolver.h"

#include <BECore/Values/Programs/Components.h>

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
                case TypeKind::Value:
                case TypeKind::List:
                    return true;
                default:
                    return false;
            }
        }

        /// A math field whose components are fields of their own at `k * stride`: the type, its components and the descriptor of one component
        struct MathField {
            ProgramType type = ProgramType::Float;
            ComponentSet components;
            const TypeDesc* componentType = nullptr;
        };

        Optional<MathField> MathFieldOf(const TypeDesc& desc) {
            if (desc.kind != TypeKind::Opaque) {
                return std::nullopt;
            }
#define BE_RESOLVE_MATH_FIELD(Unused, Name, Cpp)                                                                                                                                                       \
    if (SameType(desc, DescribeType<Cpp>())) {                                                                                                                                                         \
        using Traits = ComponentTraits<Cpp>;                                                                                                                                                           \
        /* A `Color` channel is a byte the program widens: it is read through `Extract`, never as a field of its own */                                                                                \
        if constexpr (std::is_same_v<typename Traits::Storage, typename Traits::Value>) {                                                                                                              \
            return MathField{ProgramType::Name, MakeComponentSet<Cpp>(), &DescribeType<typename Traits::Value>()};                                                                                     \
        } else {                                                                                                                                                                                       \
            return std::nullopt;                                                                                                                                                                       \
        }                                                                                                                                                                                              \
    }
            BE_PROGRAM_TYPES_MATH(BE_RESOLVE_MATH_FIELD, Unused)
#undef BE_RESOLVE_MATH_FIELD
            return std::nullopt;
        }

        /// The component of the math field @p math that starts @p relative bytes into it, named after the field's @p path
        Optional<ResolvedField> ComponentAt(const MathField& math, const String& path, size_t relative, Optional<ProgramType> readAs) {
            const ComponentSet& set = math.components;
            if (relative % set.stride != 0 || relative / set.stride >= set.count) {
                return std::nullopt;
            }
            const size_t index = relative / set.stride;
            // The first component sits where the field does: the type the program reads tells the two apart
            if (index == 0 && (!readAs.has_value() || *readAs != set.type)) {
                return std::nullopt;
            }
            return ResolvedField{Format("{}.{}", path, set.names[index]), math.componentType};
        }

        /// The field of @p type (its parents' too) that starts at @p offset from the object, an embedded object entered; @p prefix is the path of the object
        Optional<ResolvedField> Find(const TypeInfo& type, size_t offset, const String& prefix, Optional<ProgramType> readAs, const TypeRegistry& registry) {
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
                    if (Optional<ResolvedField> found = Find(*inner, offset - ref.offset, path, readAs, registry)) {
                        return found;
                    }
                    continue;
                }
                if (const Optional<MathField> math = MathFieldOf(desc)) {
                    if (offset < ref.offset + desc.opaqueSize) {
                        // A component the program reads instead of the field wins at the field's own offset, so the field is returned only when that is not what is asked
                        if (Optional<ResolvedField> component = ComponentAt(*math, path, offset - ref.offset, readAs)) {
                            return component;
                        }
                    }
                }
                if (offset == ref.offset && IsReadableAsOne(desc.kind)) {
                    return ResolvedField{path, &desc};
                }
            }
            return std::nullopt;
        }

    }  // namespace

    Optional<ResolvedField> ResolveField(const ContextDecl& declaration, LevelIndex level, uint32_t offset, Optional<ProgramType> readAs, const TypeRegistry& registry) {
        if (level >= declaration.Size()) {
            return std::nullopt;
        }
        const ContextLevel& owner = declaration.Levels()[level];
        if (owner.type == nullptr) {
            return std::nullopt;
        }
        return Find(*owner.type, offset, owner.name, readAs, registry);
    }

    Optional<ResolvedField> ResolveElementField(const TypeInfo& element, uint32_t offset, Optional<ProgramType> readAs, const TypeRegistry& registry) {
        return Find(element, offset, String(), readAs, registry);
    }

}  // namespace BECore
