#pragma once

#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief A 64-bit hash of the reflected layout of a type: what a file written from it has to agree on with the program that reads it
     *
     * Over the fields of the type and of its ancestors in the order `Serialize` writes them: each field's name and type, down to the
     * nested objects. A type is described by kind (`TypeKindName`, never the ordinal), `resizable`, a fixed `capacity`, `interns`
     * (a `PoolString` is a table index in the file, a `String` inline text), the element, the key, the alternatives of a Variant and
     * the names and the width of an enum. A pointer field contributes its pointee and every type derived from it (recursively, by
     * name): a new field of a widget or a component, or a new component type, moves the hash of the file that lists them.
     *
     * **Left out on purpose:**
     * - offsets and `elementSize`: they depend on the platform, the layout of a file does not;
     * - `opaqueSize`, `opaqueHash`, `opaqueName`: they spell the type as the compiler does, and a file cooked on one compiler has to
     *   load on another. An `Opaque` field contributes its kind and its name only, so a `Vec2` changed to a `Vec3` does not move the
     *   hash (that is the price of the portability, the equivalence test of the cooked content covers it); a `Value<T>` contributes
     *   its kind and `T`;
     * - field attributes (`range`, `default`, `hidden`...): they do not change a byte of the file;
     * - the class name of the root and of a nested object held by value: neither is written in a file (a derived type of a pointer is, so
     *   its name is in). The stamp stores the name of the root beside the hash.
     *
     * A type seen again (a recursive type: the UI tree; a type used by two fields) contributes its name only: its layout is in the
     * hash already, at the first visit, so the hash is deterministic and terminates. The same on every call, platform and compiler:
     * a canonical byte stream hashed with FNV-1a, nothing of an address. Not a promise that two hashes are different for different
     * layouts, only that a change of the above moves the hash.
     */
    uint64_t ComputeLayoutHash(const TypeRegistry& registry, const TypeInfo& root);

    /// As above for the type of this name; none for a name no type of @p registry has
    [[nodiscard]] Optional<uint64_t> ComputeLayoutHash(const TypeRegistry& registry, StringView rootTypeName);

    /// As above over the types of the program (`TypeRegistry::GetInstance`)
    uint64_t ComputeLayoutHash(const TypeInfo& root);

    /// As above over the types of the program; none for an unknown name
    [[nodiscard]] Optional<uint64_t> ComputeLayoutHash(StringView rootTypeName);

}  // namespace BECore
