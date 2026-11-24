#include <BECore/File/File.h>
#include <BECore/PoolString/PoolBlock.h>
#include <BECore/Reflection/BinaryContent.h>
#include <BECore/Reflection/ReaderLimits.h>

namespace BECore {

    namespace {

        /// Fills exactly `size` bytes at `destination` from the next bytes of the source, or says it cannot
        using ReadFn = FunctionRef<bool(void* destination, size_t size)>;

        constexpr size_t kNameTablePrefixBytes = 2 * sizeof(uint32_t);
        constexpr size_t kSmallestEntryBytes = Details::PoolEntryBytes(0);

        std::unexpected<String> Refuse(StringView message) {
            return std::unexpected(String(message));
        }

        /// A count the entries cannot have: more entries than their bytes hold, or entries without bytes (or the reverse)
        bool CountFitsBytes(uint32_t count, uint32_t bytes) {
            if (bytes == 0) {
                return count == 0;
            }
            return count != 0 && count <= bytes / kSmallestEntryBytes;
        }

        /// Entries that ParsePoolEntries has walked: sizes are where PoolEntryBytes says
        void RewriteHashes(uint8_t* entries, size_t bytes) {
            for (size_t offset = 0; offset < bytes;) {
                uint8_t* entry = entries + offset;
                const size_t size = BinaryFormat::Load<uint32_t>(entry + offsetof(Details::PoolStringEntry, size));
                const StringView text(reinterpret_cast<const char*>(entry + offsetof(Details::PoolStringEntry, data)), size);
                BinaryFormat::Store<uint64_t>(entry + offsetof(Details::PoolStringEntry, hash), StringUtils::GetHash(text));
                offset += Details::PoolEntryBytes(size);
            }
        }

        /// The one loader, over a source that is read sequentially and whose size is known
        std::expected<BinaryContent, String> LoadContent(uint64_t total, ReadFn read, BinaryContentOptions options) {
            constexpr StringView kTruncated = "Binary content is truncated";
            if (total < BinaryFormat::HeaderSize) {
                return Refuse("Binary content is too small to hold a header");
            }
            if (total > ReaderLimits::kMaxBinaryStreamBytes) {
                return Refuse("Binary content is larger than the limit");
            }

            BinaryContent content;
            content.bytes.resize(BinaryFormat::HeaderSize);
            if (!read(content.bytes.data(), BinaryFormat::HeaderSize)) {
                return Refuse(kTruncated);
            }
            if (std::memcmp(content.bytes.data(), BinaryFormat::Magic, sizeof(BinaryFormat::Magic)) != 0) {
                return Refuse("Not a BagiEngine binary stream (bad magic)");
            }
            content.version = BinaryFormat::Load<uint16_t>(content.bytes.data() + sizeof(BinaryFormat::Magic));
            if (content.version != BinaryFormat::LegacyVersion && content.version != BinaryFormat::TabledVersion && content.version != BinaryFormat::PooledVersion) {
                return Refuse("Unsupported binary format version");
            }
            if (BinaryFormat::Load<uint32_t>(content.bytes.data() + BinaryFormat::PayloadSizeOffset) != total - BinaryFormat::HeaderSize) {
                return Refuse("Binary content is truncated or has trailing bytes");
            }

            const size_t fileSize = static_cast<size_t>(total);
            if (content.version != BinaryFormat::PooledVersion) {
                content.bytes.resize(fileSize);
                if (!read(content.bytes.data() + BinaryFormat::HeaderSize, fileSize - BinaryFormat::HeaderSize)) {
                    return Refuse(kTruncated);
                }
                return content;
            }

            // The name table's size comes from its own prefix; the table itself is validated by the reader
            constexpr size_t prefixEnd = BinaryFormat::HeaderSize + kNameTablePrefixBytes;
            if (fileSize < prefixEnd + BinaryFormat::PoolSectionHeadSize) {
                return Refuse(kTruncated);
            }
            content.bytes.resize(prefixEnd);
            if (!read(content.bytes.data() + BinaryFormat::HeaderSize, kNameTablePrefixBytes)) {
                return Refuse(kTruncated);
            }
            const uint64_t nameCount = BinaryFormat::Load<uint32_t>(content.bytes.data() + BinaryFormat::HeaderSize);
            const uint64_t nameChars = BinaryFormat::Load<uint32_t>(content.bytes.data() + BinaryFormat::HeaderSize + sizeof(uint32_t));
            const uint64_t tableEnd = prefixEnd + nameCount * sizeof(uint32_t) + nameChars;
            if (tableEnd > total || total - tableEnd < BinaryFormat::PoolSectionHeadSize) {
                return Refuse("Binary name table does not fit the content");
            }

            const size_t headEnd = static_cast<size_t>(tableEnd) + BinaryFormat::PoolSectionHeadSize;
            content.bytes.resize(headEnd);
            if (!read(content.bytes.data() + prefixEnd, headEnd - prefixEnd)) {
                return Refuse(kTruncated);
            }
            BinaryFormat::PoolSection& section = content.section;
            if (!BinaryFormat::ParsePoolSectionHead(content.bytes.data(), static_cast<size_t>(tableEnd), fileSize, section)) {
                return Refuse("Binary pool section head is truncated or corrupt");
            }
            if (!CountFitsBytes(section.count, section.bytes)) {
                return Refuse("Binary pool section count does not fit its size");
            }

            if (section.bytes != 0) {
                PoolBlock block = PoolString::AllocateBlock(section.bytes);
                if (!read(block.Data(), section.bytes)) {
                    return Refuse(kTruncated);
                }
                uint8_t* entries = reinterpret_cast<uint8_t*>(block.Data());
                if (BinaryFormat::PoolSectionChecksum(entries, section.bytes) != section.checksum) {
                    return Refuse("Binary pool section checksum is wrong: the file is damaged, cook the content again");
                }
                if (section.hashFunction != Details::PoolHashFunctionId()) {
                    // Another hash function (or another signedness of char): the file's hashes are not this program's, so they are made here
                    if (!BinaryFormat::ParsePoolEntries(entries, section.bytes, section.count)) {
                        return Refuse("Binary pool section is corrupt");
                    }
                    RewriteHashes(entries, section.bytes);
                }

                content.strings.resize(section.count);
                PoolAdoptOptions adoptOptions;
                adoptOptions.verifyHashes = options.verifyHashes;
                adoptOptions.maxTextBytes = ReaderLimits::kMaxPoolStringBytes;
                const auto adopted = PoolString::AdoptBlock(block, section.bytes, Span<PoolString>(content.strings.data(), content.strings.size()), adoptOptions);
                if (!adopted.has_value()) {
                    return std::unexpected(Format("Binary pool section refused: {}", EnumUtils<PoolAdoptError>::ToString(adopted.error())));
                }
            }

            const size_t compactSize = fileSize - section.bytes;
            content.bytes.resize(compactSize);
            if (!read(content.bytes.data() + headEnd, compactSize - headEnd)) {
                return Refuse(kTruncated);
            }
            return content;
        }

    }  // namespace

    std::expected<BinaryContent, String> LoadContentFile(const Path& path, BinaryContentOptions options) {
        const Optional<FileInfo> info = GetFileInfo(path);
        if (!info) {
            return Refuse("Failed to open content file");
        }
        if (info->kind != FileKind::File) {
            return Refuse("Content path is not a regular file");
        }
        if (info->size > ReaderLimits::kMaxBinaryStreamBytes) {
            return Refuse("Content file is larger than the limit");
        }

        std::expected<File, String> file = File::Open(path, FileMode::Read);
        if (!file) {
            return std::unexpected(file.error());
        }
        const std::expected<uint64_t, String> size = file->Size();
        if (!size) {
            return std::unexpected(size.error());
        }
        const auto readFile = [&file](void* destination, size_t bytes) {
            const std::expected<size_t, String> done = file->Read(destination, bytes);
            return done.has_value() && *done == bytes;
        };
        return LoadContent(*size, readFile, options);
    }

    std::expected<BinaryContent, String> LoadContentBytes(Span<const uint8_t> data, BinaryContentOptions options) {
        size_t position = 0;
        const auto readMemory = [&](void* destination, size_t bytes) {
            if (bytes > data.size() - position) {
                return false;
            }
            std::memcpy(destination, data.data() + position, bytes);
            position += bytes;
            return true;
        };
        return LoadContent(data.size(), readMemory, options);
    }

}  // namespace BECore
