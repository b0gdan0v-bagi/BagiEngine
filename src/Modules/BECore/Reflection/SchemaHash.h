#pragma once

#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Types/Types.h>
#include <cstdint>
#include <expected>

namespace BECore {

    class ISerializer;
    class IDeserializer;

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

    /// What a cooked content file says of itself: the type its root is read as and the hash of that type's layout when the file was written
    struct ContentStamp {
        String rootType;
        uint64_t layout = 0;
    };

    /**
     * @brief Writes the stamp of a content file: the Object `BinaryFormat::ContentStampName` with `root` (the type name, a `String`) and
     *        `layout` (`ComputeLayoutHash` of it, a `UInt64`); false for a type the program does not have, and nothing is written
     *
     * Call it first in the root scope, before the root's own fields: `CheckContentStamp` reads it before anything else. The stamp is an
     * ordinary entry, so the byte layout and the version of the format are the same with or without it, and a reader that does not
     * look for it reads the root as before. A save never has one. Neither a name nor a value of it is interned.
     */
    [[nodiscard]] bool WriteContentStamp(ISerializer& writer, StringView rootTypeName);

    /**
     * @brief Reads the stamp from the root scope of @p reader, which must be positioned there (a fresh reader is)
     *
     * Silent: it records no error on a reader that had none (a missing stamp, a stamp of the wrong shape, either is the error string
     * of the result), and it interns nothing. Needs no more than the `IDeserializer` interface, so a reader of another format that has
     * the entry works too.
     */
    [[nodiscard]] std::expected<ContentStamp, String> ReadContentStamp(IDeserializer& reader);

    /**
     * @brief Reads the stamp and checks it against this program: the file must have one, name a type the program has, and the layout
     *        of that type must hash to the recorded value
     *
     * Every refusal is a message that names @p sourceName (the file), says what is wrong, gives both hashes in hex where it has them
     * and says to cook the content again with this build. The hash is the layout of the type in the program that reads the file, so
     * the file has to be cooked by a program that links the same types (a factory base's derived types included). Silent as
     * `ReadContentStamp`.
     */
    [[nodiscard]] std::expected<ContentStamp, String> CheckContentStamp(IDeserializer& reader, StringView sourceName);

}  // namespace BECore
