#pragma once

#include <BECore/Control/SchemaAnswers.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Types/Types.h>

namespace BECore {

    class ControlRoots;
    class TypeRegistry;

    /// Which types `MakeListTypesAnswer` lists
    struct ListTypesFilter {
        /// Only the types derived from this one (the type itself is not listed); null for every type
        const TypeInfo* base = nullptr;
        /// With @c base: the whole subtree, not only the direct children
        bool recursive = true;
        /// Only the types a client can make by name (not abstract, registered in a factory)
        bool creatableOnly = false;
    };

    // The reflection registry as the answer structs of `SchemaAnswers.h`: what `list_types`, `describe_type` and `schema` answer, and what `tools/schema` turns into XSD.
    //
    // Pure over a registry, so a test builds its own and no control service is needed. Every list is sorted by name (fields and
    // parameters keep their declaration order), so the same registry gives the same text whatever order its types were registered in.
    // The answers are reflected structs, so the generated writer writes them (`isConst`; a root type has no `parent`).
    // Their texts and lists are on the thread's scratch arena: the caller owns a `ScratchScope` that outlives the answer (the handlers have one).
    // A hash is never written: a JSON number above 2^53 is not safe in a client. The shapes are in Reflection/CLAUDE.md, "Schema export".

    /// The value type @p desc; a member that does not apply to the kind is absent
    [[nodiscard]] IntrusivePtr<TypeSchemaDesc> MakeTypeSchemaDesc(const TypeDesc& desc);

    /// The type as the `schema` lists it: name, parent, flags, own fields and methods
    [[nodiscard]] TypeSchemaEntry MakeTypeSchemaEntry(const TypeInfo& info, const TypeRegistry& registry);

    /// `describe_type`: the entry of @p info plus `bases` (nearest first), `derived` (direct, by name) and `allFields` (the chain's, each with its `owner`)
    [[nodiscard]] TypeSchemaDescribed DescribeTypeSchema(const TypeInfo& info, const TypeRegistry& registry);

    /// `list_types`: one row per type, `fields` and `methods` the number of own fields and methods
    [[nodiscard]] ListTypesAnswer MakeListTypesAnswer(const TypeRegistry& registry, const ListTypesFilter& filter);

    /// `schema`: `version`, `types`, `factories` and `roots`; a root with no object has no `type`
    [[nodiscard]] SchemaAnswer MakeSchemaAnswer(const TypeRegistry& registry, const ControlRoots& roots);

    /// Version of the schema document; change it when a reader of the old shape would be wrong
    inline constexpr int kSchemaVersion = 1;

}  // namespace BECore
