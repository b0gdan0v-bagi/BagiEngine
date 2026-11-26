#pragma once

#include <BECore/Control/JsonTape.h>
#include <BECore/Reflection/FieldAttributes.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Types/Types.h>

namespace BECore {

    namespace RemoteSchemaLimits {
        /// Fields one type may declare: a walk looks each of them up in a value, so a peer must not make that quadratic
        inline constexpr size_t kMaxFields = 1024;
        /// How deep a value type nests in `element`, `key` and `alternatives`; deeper is `Unknown`
        inline constexpr size_t kMaxTypeDepth = 32;
        /// The longest parent chain, so types that name each other as parents end
        inline constexpr size_t kMaxParentChain = 32;
    }  // namespace RemoteSchemaLimits

    /// A value type of another program's `schema`: what `TypeDesc` is for a type of this one
    struct RemoteTypeDesc {
        /// `any` is `Opaque`; a kind this build does not know is `Unknown`
        TypeKind kind = TypeKind::Unknown;
        /// Object and Pointer: the class name, empty when the schema names none
        String object;
        /// Enum: the names of the values; `valueNames` views them, in the shape `IPropertyVisitor::VisitEnum` takes
        Vector<String> values;
        Vector<StringView> valueNames;
        UniquePtr<RemoteTypeDesc> element;
        UniquePtr<RemoteTypeDesc> key;
        Vector<RemoteTypeDesc> alternatives;
    };

    struct RemoteField {
        String name;
        RemoteTypeDesc type;
        String tooltip;
        /// `tooltip` of the attributes views the member above
        FieldAttributes attributes;
    };

    struct RemoteType {
        String name;
        /// Empty for a root type
        String parent;
        /// The type's own fields, in declaration order
        Vector<RemoteField> fields;
    };

    /// A type and its ancestors, the base first
    using RemoteTypeChain = FixedVector<const RemoteType*, RemoteSchemaLimits::kMaxParentChain>;

    /**
     * @brief The types of the attached program, read from the result of its `schema` method
     *
     * The text is a peer's: an entry of the wrong shape is left out. Of two types of one name the first counts. Nothing is interned.
     */
    class RemoteSchema {
    public:
        RemoteSchema() = default;
        RemoteSchema(const RemoteSchema&) = delete;
        RemoteSchema& operator=(const RemoteSchema&) = delete;

        /// Rebuilds from @p result when its text differs from the last one; true when it did. Pointers into the schema die with a rebuild
        bool Load(StringView result);

        void Clear();

        /// The type called @p name, or null
        [[nodiscard]] const RemoteType* FindType(StringView name) const;

        /// @p type and its ancestors as far as the schema knows them, cut where a type repeats
        [[nodiscard]] RemoteTypeChain CollectChain(const RemoteType& type) const;

        /// Whether an object of the declared type @p base can be a @p type: by the parent chain, or as a type the factory of @p base lists
        [[nodiscard]] bool IsKindOf(const RemoteType& type, StringView base) const;

    private:
        struct Factory {
            String base;
            Vector<String> derived;
        };

        String _text;
        JsonTape _tape;
        Vector<RemoteType> _types;
        /// Keys view the names in `_types`
        HashMap<StringView, size_t> _typesByName;
        Vector<Factory> _factories;
    };

}  // namespace BECore
