#pragma once

#include <cstddef>

namespace BECore::ReaderLimits {

    // =========================================================================
    // What the deserializers take from a file, a save or a network buffer. Each limit is far above real content, so a real file never
    // meets one; a file that does is refused with an error before it is allocated or walked. A game with content beyond them raises
    // the constant, it does not work around it.
    // =========================================================================

    /// An XML document (a file or a string), in bytes. pugixml spends about fifty bytes per element, so this is also the bound of the tree
    inline constexpr size_t kMaxXmlDocumentBytes = 4 * 1024 * 1024;

    /// A binary stream or a save file, in bytes
    inline constexpr size_t kMaxBinaryStreamBytes = 16 * 1024 * 1024;

    /// Elements of one array, in either format: a reflected list is sized by this count before its elements are read
    inline constexpr size_t kMaxArrayElements = 65536;

    /// Entries on the stack of open XML objects, arrays and array elements (a recursive type such as a UI tree recurses once per two entries)
    inline constexpr size_t kMaxXmlNesting = 256;

    /// A JSON text, in bytes; the tokenizer keeps offsets as `uint32_t`
    inline constexpr size_t kMaxJsonDocumentBytes = 64 * 1024 * 1024;

    /// Open JSON arrays and objects at once, the depth the reader's scopes allow anyway; a deeper text is refused by the tokenizer
    inline constexpr size_t kMaxJsonNesting = 256;

    /// Errors one deserializer keeps; the last place of the list says that the rest were dropped
    inline constexpr size_t kMaxRecordedErrors = 64;

    /// Bytes of the path and of the message of one recorded error (they reach a log and a reply)
    inline constexpr size_t kMaxErrorTextBytes = 256;

    /// Bytes of one value read into a PoolString field: a name, an id or a key, never prose. The pool never frees, so the reader
    /// refuses a longer value with an error before it interns it (a plain String field has no such limit)
    inline constexpr size_t kMaxPoolStringBytes = 4096;

    /// Bytes of a rejected value that an error message quotes
    inline constexpr size_t kMaxQuotedValueBytes = 32;

}  // namespace BECore::ReaderLimits
