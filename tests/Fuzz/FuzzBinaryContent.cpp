#include <BECore/Reflection/BinaryContent.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <Fuzz/FuzzBinaryWalk.h>
#include <Fuzz/FuzzChecks.h>
#include <Fuzz/FuzzTargets.h>

namespace BECore::Fuzz {

    namespace {

        using namespace BinaryWalk;

        /// The input under a true header whose version-3 pool section also has the checksum of its entries as they are: without it a mutated
        /// section stops at the checksum and never reaches the structural walk and the adoption. Empty when the head does not parse.
        Vector<uint8_t> WithTrueSectionChecksum(const Vector<uint8_t>& repaired) {
            if (repaired.size() < BinaryFormat::HeaderSize || BinaryFormat::Load<uint16_t>(repaired.data() + sizeof(BinaryFormat::Magic)) != BinaryFormat::PooledVersion) {
                return {};
            }
            BinaryFormat::StringTable table;
            size_t tableEnd = 0;
            BinaryFormat::PoolSection section;
            if (!BinaryFormat::ParseStringTable(repaired.data(), BinaryFormat::HeaderSize, repaired.size(), table, tableEnd) ||
                !BinaryFormat::ParsePoolSectionHead(repaired.data(), tableEnd, repaired.size(), section)) {
                return {};
            }
            Vector<uint8_t> fixed = repaired;
            const uint64_t checksum = BinaryFormat::PoolSectionChecksum(fixed.data() + section.entriesOffset, section.bytes);
            BinaryFormat::Store<uint64_t>(fixed.data() + section.headOffset + 4 * sizeof(uint32_t), checksum);
            return fixed;
        }

        /// Whether the entries of the pool section of a version 3 stream have the checksum of its head (an empty section always does); empty when the name table or the head does not parse
        Optional<bool> HasTrueChecksum(const uint8_t* data, size_t size) {
            BinaryFormat::StringTable table;
            size_t tableEnd = 0;
            BinaryFormat::PoolSection section;
            if (!BinaryFormat::ParseStringTable(data, BinaryFormat::HeaderSize, size, table, tableEnd) || !BinaryFormat::ParsePoolSectionHead(data, tableEnd, size, section)) {
                return std::nullopt;
            }
            // A section without entries has nothing for the checksum to guard, and the loader reads no block for it
            return section.bytes == 0 || BinaryFormat::PoolSectionChecksum(data + section.entriesOffset, section.bytes) == section.checksum;
        }

        /// What an accepted content says about the strings of the stream it was read from: one adopted string per entry, the entry's text,
        /// and (when the hashes were checked, or are right) the pool's own entry for that text
        void CheckStrings(const BinaryContent& content, const uint8_t* data, bool hashesWereChecked) {
            FUZZ_CHECK(content.strings.size() == content.section.count);
            size_t at = content.section.entriesOffset;
            for (const PoolString& string : content.strings) {
                const size_t textBytes = BinaryFormat::Load<uint32_t>(data + at + offsetof(Details::PoolStringEntry, size));
                const StringView stored(reinterpret_cast<const char*>(data + at + offsetof(Details::PoolStringEntry, data)), textBytes);
                FUZZ_CHECK(string.ToStringView() == stored);
                at += Details::PoolEntryBytes(textBytes);

                const bool rightHash = string.HashValue() == StringUtils::GetHash(stored);
                FUZZ_CHECK(rightHash || !hashesWereChecked);
                if (rightHash) {
                    const Optional<PoolString> found = PoolString::Find(stored);
                    FUZZ_CHECK(found.has_value() && *found == string);
                }
            }
            FUZZ_CHECK(at == content.section.entriesOffset + content.section.bytes);
        }

        /// A content that was read and the same stream through `LoadFromView`: one structural verdict and one document
        void CheckAgainstFullStream(const BinaryContent& content, const uint8_t* data, size_t size, bool fullStreamAccepted) {
            BinaryDeserializer reader;
            const bool contentAccepted = reader.LoadFromContent(content);
            FUZZ_CHECK(contentAccepted == fullStreamAccepted);
            if (!contentAccepted) {
                FUZZ_CHECK(reader.HasErrors());
                return;
            }

            BinaryDeserializer view;
            FUZZ_CHECK(view.LoadFromView(data, size));
            FUZZ_CHECK(WalkDocument(reader, data, size) == WalkDocument(view, data, size));
        }

        void CheckStream(const uint8_t* data, size_t size) {
            const Span<const uint8_t> bytes(data, size);
            const std::expected<BinaryContent, String> trusting = LoadContentBytes(bytes, BinaryContentOptions{.verifyHashes = false});
            const std::expected<BinaryContent, String> verifying = LoadContentBytes(bytes, BinaryContentOptions{.verifyHashes = true});

            // Checking more refuses more, never less
            FUZZ_CHECK(trusting.has_value() || !verifying.has_value());
            FUZZ_CHECK(trusting.has_value() || !trusting.error().empty());
            FUZZ_CHECK(verifying.has_value() || !verifying.error().empty());

            BinaryDeserializer view;
            const bool fullStreamAccepted = view.LoadFromView(data, size);
            const bool pooled = size >= BinaryFormat::HeaderSize && BinaryFormat::Load<uint16_t>(data + sizeof(BinaryFormat::Magic)) == BinaryFormat::PooledVersion;

            // The reader of the whole stream ignores the checksum, so the loaders refuse exactly what it accepts and the checksum does not
            // cover: a stream it takes is taken by the content loader unless its section's checksum is wrong, and a content is never
            // taken with a wrong checksum
            const Optional<bool> trueChecksum = pooled ? HasTrueChecksum(data, size) : Optional<bool>(true);
            if (fullStreamAccepted) {
                FUZZ_CHECK(trueChecksum.has_value());
                FUZZ_CHECK(trusting.has_value() == *trueChecksum);
            }
            if (trusting.has_value() && trueChecksum.has_value()) {
                FUZZ_CHECK(*trueChecksum);
            }

            if (trusting.has_value()) {
                if (pooled) {
                    CheckStrings(*trusting, data, false);
                } else {
                    FUZZ_CHECK(trusting->strings.empty());
                    FUZZ_CHECK(trusting->bytes.size() == size);
                }
                CheckAgainstFullStream(*trusting, data, size, fullStreamAccepted);
            }
            if (verifying.has_value()) {
                FUZZ_CHECK(pooled || verifying->strings.empty());
                if (pooled) {
                    CheckStrings(*verifying, data, true);
                }
            }
        }

    }  // namespace

    void FuzzBinaryContent(const uint8_t* data, size_t size) {
        CheckStream(data, size);

        const Vector<uint8_t> repaired = WithTrueBinaryHeader(data, size);
        if (repaired.empty()) {
            return;
        }
        CheckStream(repaired.data(), repaired.size());

        const Vector<uint8_t> checksummed = WithTrueSectionChecksum(repaired);
        if (!checksummed.empty()) {
            CheckStream(checksummed.data(), checksummed.size());
        }
    }

}  // namespace BECore::Fuzz
