#pragma once

#include <BECore/Control/JsonFwd.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Types/Types.h>

namespace BECore {

    class ControlRoots;
    class TypeRegistry;

    /// Which types `ListTypesJson` lists
    struct ListTypesFilter {
        /// Only the types derived from this one (the type itself is not listed); null for every type
        const TypeInfo* base = nullptr;
        /// With @c base: the whole subtree, not only the direct children
        bool recursive = true;
        /// Only the types a client can make by name (not abstract, registered in a factory)
        bool creatableOnly = false;
    };

    /**
     * @brief The reflection registry as JSON: what `list_types`, `describe_type` and `schema` answer, and what `tools/schema` turns into XSD
     *
     * Pure over a registry, so a test builds its own and no control service is needed. Every list is sorted by name (fields and
     * parameters keep their declaration order), so the same registry gives the same text whatever order its types were registered in.
     * A hash is never written: a JSON number above 2^53 is not safe in a client. The shapes are in Control/CLAUDE.md, "Schema export".
     */

    /// The value type @p desc as `{"kind", ...}`; a member that does not apply to the kind is absent
    [[nodiscard]] Json TypeDescJson(const TypeDesc& desc);

    /// The type as the `schema` lists it: name, parent, flags, own fields and methods
    [[nodiscard]] Json TypeEntryJson(const TypeInfo& info, const TypeRegistry& registry);

    /// `describe_type`: the entry of @p info plus `bases` (nearest first), `derived` (direct, by name) and `allFields` (the chain's, each with its `owner`)
    [[nodiscard]] Json DescribeTypeJson(const TypeInfo& info, const TypeRegistry& registry);

    /// `list_types`: `{"types": [{name, parent, abstract, creatable, purpose, icon, fields, methods}]}`, the last two the number of own fields and methods
    [[nodiscard]] Json ListTypesJson(const TypeRegistry& registry, const ListTypesFilter& filter);

    /// `schema`: `{"version", "types", "factories", "roots"}`; a root with no object now has no `type`
    [[nodiscard]] Json BuildSchemaJson(const TypeRegistry& registry, const ControlRoots& roots);

    /// Version of the schema document; change it when a reader of the old shape would be wrong
    inline constexpr int kSchemaVersion = 1;

}  // namespace BECore
