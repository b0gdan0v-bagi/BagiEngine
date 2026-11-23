#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <Fuzz/FuzzBinaryWalk.h>
#include <Fuzz/FuzzChecks.h>
#include <Fuzz/FuzzTargets.h>

namespace BECore::Fuzz {

    namespace {

        using namespace BinaryWalk;

        void CheckBuffer(const uint8_t* data, size_t size) {
            BinaryDeserializer reader;
            if (!reader.LoadFromBuffer(data, size)) {
                FUZZ_CHECK(reader.HasErrors());
                ExpectNothingReadable(reader);
                // A refusal is not a state: the same bytes are refused again by a fresh reader
                BinaryDeserializer again;
                FUZZ_CHECK(!again.LoadFromBuffer(data, size));

                // The view reads the caller's memory and refuses the same bytes
                BinaryDeserializer view;
                FUZZ_CHECK(!view.LoadFromView(data, size));
                FUZZ_CHECK(view.HasErrors());
                ExpectNothingReadable(view);
                return;
            }

            reader.ClearErrors();
            // The stamp of a content file is a question about any accepted document: an answer either way, never an error on the reader
            const std::expected<ContentStamp, String> stamp = ReadContentStamp(reader);
            FUZZ_CHECK(!reader.HasErrors());
            FUZZ_CHECK(stamp.has_value() || !stamp.error().empty());
            const uint64_t first = WalkDocument(reader, data, size);

            // The same buffer through a fresh reader and through the reader that has just been walked: no hidden state
            BinaryDeserializer fresh;
            FUZZ_CHECK(fresh.LoadFromBuffer(data, size));
            FUZZ_CHECK(WalkDocument(fresh, data, size) == first);

            // The view is the input itself, not a copy: the same document and the same digest
            BinaryDeserializer view;
            FUZZ_CHECK(view.LoadFromView(data, size));
            FUZZ_CHECK(WalkDocument(view, data, size) == first);

            reader.ClearErrors();
            FUZZ_CHECK(reader.LoadFromBuffer(data, size));
            FUZZ_CHECK(WalkDocument(reader, data, size) == first);

            // Loading something else on top of a good document drops it
            FUZZ_CHECK(!reader.LoadFromBuffer(nullptr, 0));
            ExpectNothingReadable(reader);
            FUZZ_CHECK(!view.LoadFromView(nullptr, 0));
            ExpectNothingReadable(view);
        }

    }  // namespace

    void FuzzBinaryDeserializer(const uint8_t* data, size_t size) {
        CheckBuffer(data, size);

        const Vector<uint8_t> repaired = WithTrueBinaryHeader(data, size);
        if (!repaired.empty()) {
            CheckBuffer(repaired.data(), repaired.size());
        }
    }

}  // namespace BECore::Fuzz
