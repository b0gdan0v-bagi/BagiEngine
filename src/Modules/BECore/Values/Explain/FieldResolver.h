#pragma once

#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <cstdint>

namespace BECore {

    /// A field found by its place: the path a content file spells and the field's own type (static storage)
    struct ResolvedField {
        String path;
        const TypeDesc* type = nullptr;
    };

    /**
     * @brief The reverse of `LinkField`: the field that sits at @p offset of the object of @p level, as the path `caster.position` that links to that place
     *
     * What a `LoadSlot` read is turned back into a name by, so a trace says `caster.attack` and can give an enum its names. A level whose declared type has parents resolves their
     * fields as its own. The offset must be the field's first byte: an offset inside a field, or beyond the object, gives no path (the caller names the place by level and offset).
     *
     * A field that is a scalar, an enum, a string, an opaque value (`Vec3`, a game's registered type) or a list matches by itself; an embedded object is entered and never matches itself.
     * When an object and its first field share an offset the innermost field wins, which is the one a `LoadSlot` reads (`span` begins where `span.low` does, and `span.low` is the path).
     * A component of a `Vec3` is no field: a path never names one. Allocates; it runs only while a trace is built.
     */
    [[nodiscard]] Optional<ResolvedField> ResolveField(const ContextDecl& declaration, LevelIndex level, uint32_t offset, const TypeRegistry& registry = TypeRegistry::GetInstance());

    /// The same walk from a type, for the element of a list: the path is relative to the element (`rank`, `meld.size`), with no level in front
    [[nodiscard]] Optional<ResolvedField> ResolveElementField(const TypeInfo& element, uint32_t offset, const TypeRegistry& registry = TypeRegistry::GetInstance());

}  // namespace BECore
