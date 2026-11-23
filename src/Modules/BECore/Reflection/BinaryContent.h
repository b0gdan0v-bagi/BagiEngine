#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Types/Types.h>
#include <expected>
#include <filesystem>

namespace BECore {

    /**
     * @brief A cooked content file (version 3) read straight into pool memory
     *
     * The one loader that trusts the hashes inside a file: the entries of the file's pool section are read into a block of the
     * pool and adopted without being copied or hashed again (`PoolString::AdoptBlock`). It is for the engine's own cooked
     * output (the cooked `.bin` configs), never for a save or any file a player can change: a save goes through `BinaryDeserializer::LoadFromBuffer`
     * or `LoadFromView`, which hash every text themselves.
     *
     * `bytes` is the **compact stream**: the file without the section's entries. The 24-byte head of the section stays, so the root
     * scope starts at `section.entriesOffset`, and the header's `payloadSize` still counts the whole file (the reader adds
     * `section.bytes` back to check it). `strings[i]` is the `PoolString` of entry i of the file's section, as `AdoptBlock` gives it
     * out (an older entry of the same text when the pool had it). A file of version 1 or 2 has no pool section: `bytes` is the
     * whole file, `strings` is empty and the reader interns as for a save.
     */
    struct BinaryContent {
        uint16_t version = 0;
        Vector<uint8_t> bytes;
        BinaryFormat::PoolSection section;  ///< The head's fields; the offsets are of `bytes`. Zeroed for version 1 and 2
        Vector<PoolString> strings;
    };

    struct BinaryContentOptions {
        /// Check the hash of every entry against its text (`PoolAdoptOptions::verifyHashes`). On in a build with asserts: a cook's output
        /// was verified when it was written, so a release build trusts it and a debug build proves it.
        bool verifyHashes = ENGINE_ENABLE_ASSERTS != 0;
    };

    /**
     * @brief Reads a cooked content file: the header, the name table, the head of the pool section and the tail into `bytes`, the section's entries straight into a pool block
     *
     * Checks, in this order, and refuses with a message at the first that fails: the path is a regular file within
     * ReaderLimits::kMaxBinaryStreamBytes; the header's magic, version and `payloadSize` agree with the file's size; the head of the
     * section fits the file and `count` fits `bytes`; the checksum of the entries (a damaged file: the message says to cook again);
     * the hash function (a file cooked under another one has its hashes recomputed before they are adopted); every entry
     * (`PoolString::AdoptBlock` with ReaderLimits::kMaxPoolStringBytes as the text limit). Nothing throws and nothing asserts on
     * what the file holds. The name table and the entries of the document are validated by `BinaryDeserializer::LoadFromContent`.
     *
     * A refused file leaves the pool as it was except for one block of the file's section: the pool never frees, so a block that was
     * read and not adopted stays allocated. A file the loader adopted and the reader (or the content stamp) then refuses leaves its
     * texts in the pool.
     */
    [[nodiscard]] std::expected<BinaryContent, String> LoadContentFile(const std::filesystem::path& path, BinaryContentOptions options = {});

    /// The same checks over memory, through the same code as LoadContentFile (the seam of the tests and of the fuzz target)
    [[nodiscard]] std::expected<BinaryContent, String> LoadContentBytes(Span<const uint8_t> data, BinaryContentOptions options = {});

}  // namespace BECore
