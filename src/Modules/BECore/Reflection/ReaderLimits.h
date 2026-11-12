#pragma once

namespace BECore::ReaderLimits {

    // =========================================================================
    // What the deserializers take from a file, a save or a network buffer. Each limit is far above anything the content has (the largest
    // config of the engine and the games is about 66 KB, a save about 1 KB), so a real file never meets one; a file that does is refused
    // with an error before it is allocated or walked. A game with content beyond them raises the constant, it does not work around it.
    // =========================================================================

    /// An XML document (a file or a string), in bytes. pugixml spends about fifty bytes per element, so this is also the bound of the tree
    inline constexpr size_t kMaxXmlDocumentBytes = 4 * 1024 * 1024;

    /// A binary stream or a save file, in bytes
    inline constexpr size_t kMaxBinaryStreamBytes = 16 * 1024 * 1024;

    /// Elements of one array, in either format: a reflected list is sized by this count before its elements are read
    inline constexpr size_t kMaxArrayElements = 65536;

    /// Entries on the stack of open XML objects, arrays and array elements (a recursive type such as a UI tree recurses once per two entries)
    inline constexpr size_t kMaxXmlNesting = 256;

    /// Errors one deserializer keeps; the last place of the list says that the rest were dropped
    inline constexpr size_t kMaxRecordedErrors = 64;

    /// Bytes of the path and of the message of one recorded error (both go into the never-freed PoolString table)
    inline constexpr size_t kMaxErrorTextBytes = 256;

    /// Bytes of a rejected value that an error message quotes
    inline constexpr size_t kMaxQuotedValueBytes = 32;

}  // namespace BECore::ReaderLimits
