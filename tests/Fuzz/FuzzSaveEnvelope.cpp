#include "FuzzProbeAccess.h"
#include "FuzzTargets.h"

#include <BECore/Save/SaveGame.h>
#include <random>

namespace BECore::Fuzz {

    namespace {

        constexpr StringView kSlot = "fuzz";

        /// The game the loads below belong to; a seed of another game is a refusal
        constexpr StringView kGame = "FuzzGame";
        constexpr SaveSchema kSchema{kGame, 3, 2};

        /// The slot folder of the process, made on the first input and removed when the process ends normally
        class SlotWorkspace {
        public:
            SlotWorkspace() : _service(_fileSystem) {
                const String tag = Format("{:08x}", std::random_device{}());
                _directory = std::filesystem::temp_directory_path() / ToStd("BagiFuzzSave_" + tag);
                std::error_code ignored;
                std::filesystem::remove_all(_directory, ignored);
                FUZZ_CHECK(_service.Initialize(_directory));
            }

            ~SlotWorkspace() {
                std::error_code ignored;
                std::filesystem::remove_all(_directory, ignored);
            }

            SlotWorkspace(const SlotWorkspace&) = delete;
            SlotWorkspace& operator=(const SlotWorkspace&) = delete;

            SaveGameService& Service() {
                return _service;
            }

        private:
            FileSystem _fileSystem;
            SaveGameService _service;
            std::filesystem::path _directory;
        };

        SlotWorkspace& Workspace() {
            static SlotWorkspace workspace;
            return workspace;
        }

        bool IsFileContentError(SaveErrorKind kind) {
            return kind == SaveErrorKind::Corrupt || kind == SaveErrorKind::WrongGame || kind == SaveErrorKind::VersionTooNew || kind == SaveErrorKind::VersionTooOld;
        }

        void CheckSlot(SaveGameService& service, const Vector<uint8_t>& bytes) {
            FUZZ_CHECK(service.WriteSlot(kSlot, bytes).has_value());

            // The same file as read back: what the service stores is what it was given
            const auto stored = service.ReadSlot(kSlot);
            FUZZ_CHECK(stored.has_value());
            FUZZ_CHECK(*stored == bytes);

            const auto peek = service.Peek(kSlot);
            if (!peek.has_value()) {
                FUZZ_CHECK(peek.error().kind == SaveErrorKind::Corrupt);
                FUZZ_CHECK(!peek.error().detail.empty());
            }

            const ProbeState untouched = MakeFilledProbe();
            const uint64_t untouchedDigest = DigestProbe(untouched);
            ProbeState state = untouched;
            bool readerCalled = false;
            const auto loaded = service.Load(kSlot, kSchema, eastl::function<void(IDeserializer&)>([&](IDeserializer& reader) {
                                                 readerCalled = true;
                                                 state.Deserialize(reader);
                                             }));

            if (loaded.has_value()) {
                // A load that succeeded read a file that Peek read too, of this game and a version the schema accepts
                FUZZ_CHECK(readerCalled);
                FUZZ_CHECK(peek.has_value());
                FUZZ_CHECK(loaded->game == peek->game);
                FUZZ_CHECK(loaded->version == peek->version);
                FUZZ_CHECK(StringView(loaded->game) == kGame);
                FUZZ_CHECK(loaded->version >= kSchema.minVersion && loaded->version <= kSchema.version);
            } else {
                FUZZ_CHECK(IsFileContentError(loaded.error().kind));
                FUZZ_CHECK(!loaded.error().detail.empty());
                // A file Peek could not read is not loaded; a refusal on the game or the version is made before the reader is called
                if (loaded.error().kind != SaveErrorKind::Corrupt) {
                    FUZZ_CHECK(!readerCalled);
                    FUZZ_CHECK(DigestProbe(state) == untouchedDigest);
                }
            }
            if (!peek.has_value()) {
                FUZZ_CHECK(!loaded.has_value());
                FUZZ_CHECK(!readerCalled);
            }

            // The reflected overload gives the same verdict as the callback
            ProbeState direct = untouched;
            const auto again = service.Load(kSlot, kSchema, direct);
            FUZZ_CHECK(again.has_value() == loaded.has_value());
            if (again.has_value()) {
                FUZZ_CHECK(DigestProbe(direct) == DigestProbe(state));
            } else {
                FUZZ_CHECK(again.error().kind == loaded.error().kind);
            }
        }

    }  // namespace

    void FuzzSaveEnvelope(const uint8_t* data, size_t size) {
        SaveGameService& service = Workspace().Service();
        CheckSlot(service, Vector<uint8_t>(data, data + size));

        const Vector<uint8_t> repaired = WithTrueBinaryHeader(data, size);
        if (!repaired.empty()) {
            CheckSlot(service, repaired);
        }
    }

    Vector<uint8_t> MakeSaveFileBytes(StringView game, uint32_t version) {
        const String tag = Format("{:08x}", std::random_device{}());
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / ToStd("BagiFuzzSeed_" + tag);
        Vector<uint8_t> bytes;
        {
            FileSystem fileSystem;
            SaveGameService service(fileSystem);
            if (service.Initialize(directory) && service.Save(kSlot, SaveSchema{game, version, version}, MakeFilledProbe()).has_value()) {
                if (auto read = service.ReadSlot(kSlot); read.has_value()) {
                    bytes = std::move(*read);
                }
            }
        }
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        return bytes;
    }

}  // namespace BECore::Fuzz
