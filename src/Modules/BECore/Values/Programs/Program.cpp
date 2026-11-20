#include "Program.h"

#include <BECore/Values/Programs/Lists/ListImage.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>

namespace BECore {

    namespace {

        constexpr uint64_t kHashSeed = 0xcbf29ce484222325ULL;
        constexpr uint64_t kHashPrime = 0x100000001b3ULL;

        /// The line of a list or loop instruction (jump targets are instruction indexes), nothing for any other; a descriptor constant that is not there reads as an empty one, `Finish` refused it
        Optional<String> DisassembleList(size_t i, const Instruction& ins, Span<const RegisterImage> constants) {
            const StringView name = EnumUtils<Opcode>::ToString(ins.op);
            const auto descriptor = [&] { return ins.imm < constants.size() ? ListDescriptor::FromImage(constants[ins.imm]) : ListDescriptor{}; };
            const auto where = [&] {
                const ListDescriptor list = descriptor();
                return Format("offset {} stride {} {} count {}", list.offset, list.stride, IsValidListLayout(list.layout) ? EnumUtils<ListLayout>::ToString(list.layout) : StringView("?"), list.count);
            };
            switch (ins.op) {
                case Opcode::ListSlot:
                    return Format("{}: {} r{} <- level {} list at {}\n", i, name, ins.dst, ins.a, where());
                case Opcode::ListElem:
                    return Format("{}: {} r{} <- list at {} of element r{}\n", i, name, ins.dst, where(), ins.a);
                case Opcode::ListSize:
                    return Format("{}: {} r{} <- size of list r{}\n", i, name, ins.dst, ins.a);
                case Opcode::ListAt:
                    return Format("{}: {} r{} <- element r{} of list r{}\n", i, name, ins.dst, ins.b, ins.a);
                case Opcode::LoopBegin:
                    return Format("{}: {} r{} over list r{}, none: jump to {}\n", i, name, ins.dst, ins.a, ins.imm);
                case Opcode::LoopEnd:
                    return Format("{}: {} r{} next of list r{}, one: jump to {}\n", i, name, ins.dst, ins.a, ins.imm);
                case Opcode::BreakIf:
                    return Format("{}: {} r{} is {}: jump to {}\n", i, name, ins.a, ins.b, ins.imm);
                default:
                    break;
            }
            if (IsLoadElem(ins.op)) {
                return Format("{}: {} r{} <- element r{} offset {}{}\n", i, name, ins.dst, ins.a, ins.imm, ins.op == Opcode::LoadElemEnum ? Format(" ({} bytes)", ins.b) : String());
            }
            return std::nullopt;
        }

        /// FNV-1a over a 64-bit word: the hash only buckets programs, equality is `SameCode`
        constexpr uint64_t Fold(uint64_t hash, uint64_t word) {
            return (hash ^ word) * kHashPrime;
        }

        constexpr size_t kChunkSize = 256;
        constexpr size_t kMaxChunks = ProgramPool::kMaxPrograms / kChunkSize;
        constexpr size_t kExpectedProgramsPerHash = 1;

        struct Chunk {
            Array<Program, kChunkSize> programs;
            /// Beside the programs, so a `Program` stays immutable and movable; null until `NativePrograms` binds a function
            Array<std::atomic<NativeProgramFn>, kChunkSize> natives{};
        };

        /// What `NativePrograms` says is bound to a program now, asked under the pool's lock (the registry's lock is taken inside it, never the other way round)
        using NativeLookup = FunctionRef<NativeProgramFn(const Program&)>;

        /// What `Intern` leaves for the caller to do once the pool's lock is free
        struct InternOutcome {
            Optional<ProgramId> id;
            bool nativeMissed = false;
            PoolString source;
        };

        /// Programs sit in chunks that are never moved or freed, and a chunk is published before any id in it is given out, so `Get` takes no lock
        class Storage {
        public:
            static Storage& Instance() {
                static Storage storage;
                return storage;
            }

            InternOutcome Intern(Program program, NativeLookup lookup) {
                const uint64_t hash = program.CodeHash();
                LockGuard lock(_mutex);
                auto bucket = _byHash.find(hash);
                if (bucket != _byHash.end()) {
                    for (const ProgramId id : bucket->second) {
                        if (Slot(id).SameCode(program)) {
                            return InternOutcome{id, false, {}};
                        }
                    }
                }
                const size_t count = _count.load(std::memory_order_relaxed);
                if (count >= ProgramPool::kMaxPrograms) {
                    return InternOutcome{};
                }
                const auto id = static_cast<ProgramId>(count);
                const size_t chunkIndex = count / kChunkSize;
                if (_chunks[chunkIndex].load(std::memory_order_relaxed) == nullptr) {
                    _owned.push_back(MakeUnique<Chunk>());
                    _chunks[chunkIndex].store(_owned.back().get(), std::memory_order_release);
                }
                // The lookup is under this lock, so an `Install` that adds its entries first and rebinds after finds this program when it rebinds, and one that comes later is seen here
                const NativeProgramFn native = lookup(program);
                InternOutcome outcome{id, native == nullptr, program.Source()};
                Slot(id) = std::move(program);
                _chunks[chunkIndex].load(std::memory_order_relaxed)->natives[id % kChunkSize].store(native, std::memory_order_release);
                _byHash[hash].push_back(id);
                _count.store(count + 1, std::memory_order_release);
                return outcome;
            }

            NativeProgramFn Native(ProgramId id) const {
                ASSERT(id < Count(), "ProgramPool::Native: no program has this id");
                const Chunk* chunk = _chunks[id / kChunkSize].load(std::memory_order_acquire);
                return chunk != nullptr ? chunk->natives[id % kChunkSize].load(std::memory_order_acquire) : nullptr;
            }

            size_t NativeCount() const {
                const size_t count = Count();
                size_t bound = 0;
                for (size_t id = 0; id < count; ++id) {
                    bound += Native(static_cast<ProgramId>(id)) != nullptr ? 1 : 0;
                }
                return bound;
            }

            void RebindNative(NativeLookup lookup) {
                LockGuard lock(_mutex);
                const size_t count = _count.load(std::memory_order_relaxed);
                for (size_t id = 0; id < count; ++id) {
                    const NativeProgramFn native = lookup(Slot(static_cast<ProgramId>(id)));
                    _chunks[id / kChunkSize].load(std::memory_order_relaxed)->natives[id % kChunkSize].store(native, std::memory_order_release);
                }
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

    bool ProgramCodeView::operator==(const ProgramCodeView& other) const {
        return resultType == other.resultType && resultRegister == other.resultRegister && registerCount == other.registerCount && reads == other.reads &&
               std::equal(code.begin(), code.end(), other.code.begin(), other.code.end()) && std::equal(constants.begin(), constants.end(), other.constants.begin(), other.constants.end());
    }

    bool Program::SameCode(const Program& other) const {
        return CodeView() == other.CodeView();
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
            if (IsMulAddSlot(ins.op)) {
                text += Format("{}: {} r{} <- level {} offset {} * constant {} + constant {}\n", i, EnumUtils<Opcode>::ToString(ins.op), ins.dst, ins.a, ins.imm, ins.b, ins.c);
                continue;
            }
            if (IsLerpSlots(ins.op)) {
                text +=
                    Format("{}: {} r{} <- level {} offset {}, level {} offset {}, t r{}\n", i, EnumUtils<Opcode>::ToString(ins.op), ins.dst, ins.a, LowHalf(ins.imm), ins.b, HighHalf(ins.imm), ins.c);
                continue;
            }
            if (ins.op == Opcode::LoadTableSlot) {
                text += Format("{}: LoadTableSlot r{} <- level {} offset {} [{} entries from constant {}]\n", i, ins.dst, ins.a, LowHalf(ins.imm), TableEntryCount(ins), HighHalf(ins.imm));
                continue;
            }
            if (const Optional<String> line = DisassembleList(i, ins, _constants)) {
                text += *line;
                continue;
            }
            text += Format("{}: {} r{} <- r{} r{} r{} imm {}\n", i, EnumUtils<Opcode>::ToString(ins.op), ins.dst, ins.a, ins.b, ins.c, ins.imm);
        }
        text += Format("result {} in r{}\n", EnumUtils<ProgramType>::ToString(_resultType), _resultRegister);
        return text;
    }

    Optional<ProgramId> ProgramPool::Intern(Program program) {
        const InternOutcome outcome = Storage::Instance().Intern(std::move(program), [](const Program& candidate) { return NativePrograms::Find(candidate); });
        if (outcome.nativeMissed) {
            NativePrograms::ReportMiss(outcome.source);
        }
        return outcome.id;
    }

    const Program& ProgramPool::Get(ProgramId id) {
        return Storage::Instance().Get(id);
    }

    size_t ProgramPool::Count() {
        return Storage::Instance().Count();
    }

    NativeProgramFn ProgramPool::Native(ProgramId id) {
        return Storage::Instance().Native(id);
    }

    size_t ProgramPool::NativeCount() {
        return Storage::Instance().NativeCount();
    }

    void ProgramPool::RebindNative() {
        Storage::Instance().RebindNative([](const Program& candidate) { return NativePrograms::Find(candidate); });
    }

}  // namespace BECore
