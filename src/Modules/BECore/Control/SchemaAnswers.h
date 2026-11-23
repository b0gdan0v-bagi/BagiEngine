#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    // The answers of `list_types`, `describe_type` and `schema`: the registry as plain data, written by the generated `WriteJson` (`Reflection/CLAUDE.md`, "Schema export").
    // The names start with `TypeSchema` because the registry holds every reflected type of the program by its simple name. A member that does not apply is an `Optional`,
    // so it is absent from the text as the schema document has always left it out.

    /**
     * @brief A value type as `{"kind", ...}`: the members that do not apply to the kind are absent
     *
     * Reference counted because the type recurses (`element`, `key`, `alternatives`) and a member cannot hold its own type by value; a null pointer writes no key.
     * Every other `TypeSchema` struct holds a type through such a pointer too, which is never null in an answer the builders made.
     * The node is a heap object; its lists are on the scratch arena and its texts are views of the registry (names of the program), so it must die inside the scope of the answer (never cached).
     */
    struct TypeSchemaDesc : RefCounted {
        BE_CLASS(TypeSchemaDesc)

        BE_REFLECT_FIELD Optional<ArenaVector<IntrusivePtr<TypeSchemaDesc>>> alternatives;
        BE_REFLECT_FIELD Optional<uint64_t> capacity;
        BE_REFLECT_FIELD IntrusivePtr<TypeSchemaDesc> element;
        BE_REFLECT_FIELD IntrusivePtr<TypeSchemaDesc> key;
        BE_REFLECT_FIELD TextView kind;
        BE_REFLECT_FIELD Optional<TextView> object;
        BE_REFLECT_FIELD Optional<bool> resizable;
        BE_REFLECT_FIELD Optional<ArenaVector<TextView>> values;
    };

    /// The attributes a field sets; every member is absent when it is not set, so a field with none writes `{}`
    struct TypeSchemaAttributes {
        BE_CLASS(TypeSchemaAttributes)

        BE_REFLECT_FIELD Optional<bool> hidden;
        /// `[min, max]`
        BE_REFLECT_FIELD Optional<ArenaVector<double>> range;
        BE_REFLECT_FIELD Optional<bool> readonly;
        BE_REFLECT_FIELD Optional<bool> required;
        BE_REFLECT_FIELD Optional<double> step;
        BE_REFLECT_FIELD Optional<TextView> tooltip;
    };

    struct TypeSchemaField {
        BE_CLASS(TypeSchemaField)

        BE_REFLECT_FIELD TypeSchemaAttributes attributes;
        BE_REFLECT_FIELD TextView name;
        BE_REFLECT_FIELD IntrusivePtr<TypeSchemaDesc> type;
    };

    /// A field of `allFields` of `describe_type`: the field with the type that declares it
    struct TypeSchemaOwnedField : TypeSchemaField {
        BE_CLASS(TypeSchemaOwnedField)

        BE_REFLECT_FIELD TextView owner;
    };

    struct TypeSchemaParam {
        BE_CLASS(TypeSchemaParam)

        BE_REFLECT_FIELD TextView name;
        BE_REFLECT_FIELD IntrusivePtr<TypeSchemaDesc> type;
    };

    /// `isConst`, not `const`: `const` is a keyword and cannot name a member
    struct TypeSchemaMethod {
        BE_CLASS(TypeSchemaMethod)

        BE_REFLECT_FIELD bool callable = false;
        BE_REFLECT_FIELD bool isConst = false;
        BE_REFLECT_FIELD TextView name;
        BE_REFLECT_FIELD ArenaVector<TypeSchemaParam> params;
        BE_REFLECT_FIELD IntrusivePtr<TypeSchemaDesc> result;
    };

    /// The members the `list_types` row and the `schema` entry of a type share; `parent` is absent for a type with none
    struct TypeSchemaHead {
        BE_CLASS(TypeSchemaHead)

        BE_REFLECT_FIELD bool abstract = false;
        BE_REFLECT_FIELD bool creatable = false;
        BE_REFLECT_FIELD TextView icon;
        BE_REFLECT_FIELD TextView name;
        BE_REFLECT_FIELD Optional<TextView> parent;
        BE_REFLECT_FIELD TextView purpose;
    };

    /// A row of `list_types`: `fields` and `methods` are the numbers of the type's own
    struct TypeSchemaListed : TypeSchemaHead {
        BE_CLASS(TypeSchemaListed)

        BE_REFLECT_FIELD uint64_t fields = 0;
        BE_REFLECT_FIELD uint64_t methods = 0;
    };

    /// A type as `schema` lists it: its own fields and methods, in declaration order
    struct TypeSchemaEntry : TypeSchemaHead {
        BE_CLASS(TypeSchemaEntry)

        BE_REFLECT_FIELD Optional<TextView> factoryBase;
        BE_REFLECT_FIELD ArenaVector<TypeSchemaField> fields;
        BE_REFLECT_FIELD ArenaVector<TypeSchemaMethod> methods;
    };

    /// `describe_type`: the entry plus the chain's fields, the bases (nearest first) and the direct derived types
    struct TypeSchemaDescribed : TypeSchemaEntry {
        BE_CLASS(TypeSchemaDescribed)

        BE_REFLECT_FIELD ArenaVector<TypeSchemaOwnedField> allFields;
        BE_REFLECT_FIELD ArenaVector<TextView> bases;
        BE_REFLECT_FIELD ArenaVector<TextView> derived;
    };

    /// A factory base with its derived types; `elementName` is absent for a base no reflected field holds
    struct TypeSchemaFactory {
        BE_CLASS(TypeSchemaFactory)

        BE_REFLECT_FIELD TextView base;
        BE_REFLECT_FIELD ArenaVector<TextView> derived;
        BE_REFLECT_FIELD Optional<TextView> elementName;
    };

    /// A root of the control service; `type` is the type of the object it resolves to now, absent when it resolves to none
    struct TypeSchemaRoot {
        BE_CLASS(TypeSchemaRoot)

        BE_REFLECT_FIELD ArenaString name;  // a copy: the builder's list of names is a local of its own
        BE_REFLECT_FIELD Optional<TextView> type;
    };

    /// `list_types`
    struct ListTypesAnswer {
        BE_CLASS(ListTypesAnswer)

        BE_REFLECT_FIELD ArenaVector<TypeSchemaListed> types;
    };

    /// `schema`
    struct SchemaAnswer {
        BE_CLASS(SchemaAnswer)

        BE_REFLECT_FIELD ArenaVector<TypeSchemaFactory> factories;
        BE_REFLECT_FIELD ArenaVector<TypeSchemaRoot> roots;
        BE_REFLECT_FIELD ArenaVector<TypeSchemaEntry> types;
        BE_REFLECT_FIELD int32_t version = 0;
    };

}  // namespace BECore
