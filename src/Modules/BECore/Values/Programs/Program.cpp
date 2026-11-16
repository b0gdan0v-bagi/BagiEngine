#include "Program.h"

namespace BECore {

    namespace {

        constexpr uint64_t kHashSeed = 0xcbf29ce484222325ULL;
        constexpr uint64_t kHashPrime = 0x100000001b3ULL;

        /// FNV-1a over a 64-bit word: the hash only buckets programs, equality is `SameCode`
        constexpr uint64_t Fold(uint64_t hash, uint64_t word) {
            return (hash ^ word) * kHashPrime;
        }

        constexpr size_t kChunkSize = 256;
        constexpr size_t kMaxChunks = ProgramPool::kMaxPrograms / kChunkSize;
        constexpr size_t kExpectedProgramsPerHash = 1;

        struct Chunk {
            Array<Program, kChunkSize> programs;
        };

        /// Programs sit in chunks that are never moved or freed, and a chunk is published before any id in it is given out, so `Get` takes no lock
        class Storage {
        public:
            static Storage& Instance() {
                static Storage storage;
                return storage;
            }

            Optional<ProgramId> Intern(Program program) {
                const uint64_t hash = program.CodeHash();
                LockGuard lock(_mutex);
                auto bucket = _byHash.find(hash);
                if (bucket != _byHash.end()) {
                    for (const ProgramId id : bucket->second) {
                        if (Slot(id).SameCode(program)) {
                            return id;
                        }
                    }
                }
                const size_t count = _count.load(std::memory_order_relaxed);
                if (count >= ProgramPool::kMaxPrograms) {
                    return std::nullopt;
                }
                const auto id = static_cast<ProgramId>(count);
                const size_t chunkIndex = count / kChunkSize;
                if (_chunks[chunkIndex].load(std::memory_order_relaxed) == nullptr) {
                    _owned.push_back(MakeUnique<Chunk>());
                    _chunks[chunkIndex].store(_owned.back().get(), std::memory_order_release);
                }
                Slot(id) = std::move(program);
                _byHash[hash].push_back(id);
                _count.store(count + 1, std::memory_order_release);
                return id;
            }

            const Program& Get(ProgramId id) const {
                // The acquire pairs with the release of `Intern` that follows the write of the program: it must run in a Release build too, so `Get` is safe on its own and not only
                // through whatever synchronization hands the id over
                [[maybe_unused]] const size_t count = _count.load(std::memory_order_acquire);
                ASSERT(id < count, "ProgramPool::Get: no program has this id");
                const Chunk* chunk = _chunks[id / kChunkSize].load(std::memory_order_acquire);
                return chunk->programs[id % kChunkSize];
            }

            size_t Count() const {
                return _count.load(std::memory_order_acquire);
            }

        private:
            Storage() = default;

            Program& Slot(ProgramId id) {
                return _chunks[id / kChunkSize].load(std::memory_order_relaxed)->programs[id % kChunkSize];
            }

            Mutex _mutex{BE_LOCK_SITE("ProgramPool")};
            Array<std::atomic<Chunk*>, kMaxChunks> _chunks{};
            Vector<UniquePtr<Chunk>> _owned;
            HashMap<uint64_t, SmallVector<ProgramId, kExpectedProgramsPerHash>> _byHash;
            std::atomic<size_t> _count{0};
        };

    }  // namespace

    bool Program::SameCode(const Program& other) const {
        return _resultType == other._resultType && _resultRegister == other._resultRegister && _registerCount == other._registerCount && _reads == other._reads && _code == other._code &&
               _constants == other._constants;
    }

    uint64_t Program::CodeHash() const {
        uint64_t hash = Fold(kHashSeed, static_cast<uint64_t>(_resultType));
        hash = Fold(hash, _resultRegister);
        hash = Fold(hash, _registerCount);
        hash = Fold(hash, _reads);
        for (const Instruction& ins : _code) {
            hash = Fold(hash, static_cast<uint64_t>(ins.op));
            hash = Fold(hash, static_cast<uint64_t>(ins.dst) | static_cast<uint64_t>(ins.a) << 8 | static_cast<uint64_t>(ins.b) << 16 | static_cast<uint64_t>(ins.c) << 24 |
                                  static_cast<uint64_t>(ins.imm) << 32);
        }
        for (const RegisterImage& image : _constants) {
            for (const uint8_t byte : image.bytes) {
                hash = Fold(hash, byte);
            }
        }
        return hash;
    }

    String Program::Disassemble() const {
        String text;
        for (size_t i = 0; i < _code.size(); ++i) {
            const Instruction& ins = _code[i];
            if (ins.op == Opcode::LoadTable) {
                text += Format("{}: LoadTable r{} <- r{} [{} entries from constant {}]\n", i, ins.dst, ins.a, TableEntryCount(ins), ins.imm);
                continue;
            }
            text += Format("{}: {} r{} <- r{} r{} r{} imm {}\n", i, EnumUtils<Opcode>::ToString(ins.op), ins.dst, ins.a, ins.b, ins.c, ins.imm);
        }
        text += Format("result {} in r{}\n", EnumUtils<ProgramType>::ToString(_resultType), _resultRegister);
        return text;
    }

    Optional<ProgramId> ProgramPool::Intern(Program program) {
        return Storage::Instance().Intern(std::move(program));
    }

    const Program& ProgramPool::Get(ProgramId id) {
        return Storage::Instance().Get(id);
    }

    size_t ProgramPool::Count() {
        return Storage::Instance().Count();
    }

}  // namespace BECore
