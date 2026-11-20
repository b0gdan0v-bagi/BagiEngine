#include "ProgramDomain.h"

#include <BECore/Values/Programs/ExprFunctions.h>

namespace BECore {

    namespace {

        constexpr uint64_t kHashSeed = 0xcbf29ce484222325ULL;
        constexpr uint64_t kHashPrime = 0x100000001b3ULL;

        constexpr uint64_t Fold(uint64_t hash, uint64_t word) {
            return (hash ^ word) * kHashPrime;
        }

        /// Entries sit in chunks that are never moved or freed, and a chunk is published before any id in it is given out, so `Get` takes no lock (the model of `ProgramPool`)
        template <typename Entry, size_t ChunkSize, size_t MaxEntries>
        class AppendOnlyTable {
        public:
            static_assert(MaxEntries % ChunkSize == 0);
            static constexpr size_t kMaxChunks = MaxEntries / ChunkSize;

            /// The caller holds the lock of the registry and has checked `Count() < MaxEntries`
            uint32_t Append(Entry entry) {
                const size_t count = _count.load(std::memory_order_relaxed);
                ASSERT(count < MaxEntries, "AppendOnlyTable: full");
                const size_t chunkIndex = count / ChunkSize;
                if (_chunks[chunkIndex].load(std::memory_order_relaxed) == nullptr) {
                    _owned.push_back(MakeUnique<Chunk>());
                    _chunks[chunkIndex].store(_owned.back().get(), std::memory_order_release);
                }
                _chunks[chunkIndex].load(std::memory_order_relaxed)->entries[count % ChunkSize] = std::move(entry);
                _count.store(count + 1, std::memory_order_release);
                return static_cast<uint32_t>(count);
            }

            const Entry& Get(uint32_t id) const {
                // The acquire pairs with the release of `Append` that follows the write of the entry, in every build, so `Get` is safe without the lock
                [[maybe_unused]] const size_t count = _count.load(std::memory_order_acquire);
                ASSERT(id < count, "ProgramDomain: no entry has this id");
                return _chunks[id / ChunkSize].load(std::memory_order_acquire)->entries[id % ChunkSize];
            }

            size_t Count() const {
                return _count.load(std::memory_order_acquire);
            }

        private:
            struct Chunk {
                Array<Entry, ChunkSize> entries;
            };

            Array<std::atomic<Chunk*>, kMaxChunks> _chunks{};
            Vector<UniquePtr<Chunk>> _owned;
            std::atomic<size_t> _count{0};
        };

        constexpr size_t kTypeChunk = 16;
        constexpr size_t kOperationChunk = 64;

        class Registry {
        public:
            static Registry& Instance() {
                static Registry registry;
                return registry;
            }

            Mutex& Lock() {
                return _mutex;
            }

            AppendOnlyTable<DomainType, kTypeChunk, ProgramDomain::kMaxTypes> types;
            AppendOnlyTable<DomainOperation, kOperationChunk, ProgramDomain::kMaxOperations> operations;

        private:
            Registry() = default;

            Mutex _mutex{BE_LOCK_SITE("ProgramDomain")};
        };

        [[nodiscard]] bool IsIdentifier(StringView name) {
            if (name.empty() || (std::isdigit(static_cast<unsigned char>(name[0])) != 0)) {
                return false;
            }
            for (const char ch : name) {
                const bool word = std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
                if (!word) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool IsEngineTypeName(StringView name) {
            for (const ProgramType type : EnumUtils<ProgramType>::Values()) {
                if (ProgramTypeName(type) == name) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool SameParameters(const DomainOperation& operation, Span<const DomainValueType> parameters) {
            if (operation.parameterCount != parameters.size()) {
                return false;
            }
            for (size_t i = 0; i < parameters.size(); ++i) {
                if (!(operation.parameters[i] == parameters[i])) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] uint64_t FoldValueType(uint64_t hash, const DomainValueType& type) {
            hash = Fold(hash, static_cast<uint64_t>(type.type));
            hash = Fold(hash, type.domain);
            if (type.enumType != nullptr) {
                for (const StringView name : type.enumType->names) {
                    hash = Fold(hash, StringUtils::GetHash(name));
                }
            }
            return hash;
        }

    }  // namespace

    std::expected<DomainTypeId, String> ProgramDomain::RegisterTypeImpl(StringView name, const TypeDesc& descriptor, size_t size, DomainEquality equality, bool uniqueRepresentation) {
        if (!IsIdentifier(name)) {
            return std::unexpected(Format("type name '{}' is not an identifier (letters, digits and '_', no digit first)", name));
        }
        if (IsEngineTypeName(name)) {
            return std::unexpected(Format("type name '{}' is the name of an engine type", name));
        }
        if (size > kProgramRegisterBytes) {
            return std::unexpected(Format("type '{}' is {} bytes, a register holds {}", name, size, kProgramRegisterBytes));
        }
        if (equality == DomainEquality::Bitwise && !uniqueRepresentation) {
            return std::unexpected(Format("type '{}' has padding or floating-point members, so its bytes cannot say whether two values are equal: register an operation that compares", name));
        }
        Registry& registry = Registry::Instance();
        LockGuard lock(registry.Lock());
        const size_t count = registry.types.Count();
        for (size_t i = 0; i < count; ++i) {
            const DomainType& known = registry.types.Get(static_cast<DomainTypeId>(i));
            if (known.name.ToStringView() == name) {
                return std::unexpected(Format("type name '{}' is taken", name));
            }
            if (SameType(*known.descriptor, descriptor)) {
                return std::unexpected(Format("type '{}' is registered already as '{}'", name, known.name.ToStringView()));
            }
        }
        if (count >= kMaxTypes) {
            return std::unexpected(Format("type '{}': the registry holds at most {} types", name, kMaxTypes));
        }
        return registry.types.Append(DomainType{PoolString::Intern(name), &descriptor, static_cast<uint32_t>(size), equality});
    }

    std::expected<DomainOperationId, String> ProgramDomain::AddOperationImpl(StringView name, Span<const DomainValueType> parameters, const DomainValueType& result, bool takesContext, LevelMask reads,
                                                                             DomainOperationFn function) {
        if (!IsIdentifier(name)) {
            return std::unexpected(Format("operation name '{}' is not an identifier (letters, digits and '_', no digit first)", name));
        }
        if (reads != 0 && !takesContext) {
            return std::unexpected(Format("operation '{}' declares the levels it reads, but takes no context", name));
        }
        if (FindExprFunction(name) != nullptr || name == kTableFunction || name == kNamedTableFunction) {
            return std::unexpected(Format("operation name '{}' is a function of the formula language", name));
        }
        DomainOperation operation;
        operation.name = PoolString::Intern(name);
        operation.parameterCount = static_cast<uint8_t>(parameters.size());
        for (size_t i = 0; i < parameters.size(); ++i) {
            operation.parameters[i] = parameters[i];
        }
        operation.result = result;
        operation.takesContext = takesContext;
        operation.reads = reads;
        operation.function = function;

        Registry& registry = Registry::Instance();
        LockGuard lock(registry.Lock());
        const size_t count = registry.operations.Count();
        for (size_t i = 0; i < count; ++i) {
            const DomainOperation& known = registry.operations.Get(static_cast<DomainOperationId>(i));
            if (known.name == operation.name && SameParameters(known, parameters)) {
                return std::unexpected(Format("operation '{}' is registered already with these parameters", name));
            }
        }
        if (count >= kMaxOperations) {
            return std::unexpected(Format("operation '{}': the registry holds at most {} operations", name, kMaxOperations));
        }
        return registry.operations.Append(std::move(operation));
    }

    Optional<DomainTypeId> ProgramDomain::FindType(StringView name) {
        Registry& registry = Registry::Instance();
        LockGuard lock(registry.Lock());
        const size_t count = registry.types.Count();
        for (size_t i = 0; i < count; ++i) {
            if (registry.types.Get(static_cast<DomainTypeId>(i)).name.ToStringView() == name) {
                return static_cast<DomainTypeId>(i);
            }
        }
        return std::nullopt;
    }

    Optional<DomainTypeId> ProgramDomain::FindTypeByDescriptor(const TypeDesc& descriptor) {
        const Registry& registry = Registry::Instance();
        const size_t count = registry.types.Count();
        for (size_t i = 0; i < count; ++i) {
            if (SameType(*registry.types.Get(static_cast<DomainTypeId>(i)).descriptor, descriptor)) {
                return static_cast<DomainTypeId>(i);
            }
        }
        return std::nullopt;
    }

    const DomainType& ProgramDomain::Type(DomainTypeId id) {
        return Registry::Instance().types.Get(id);
    }

    size_t ProgramDomain::TypeCount() {
        return Registry::Instance().types.Count();
    }

    Vector<DomainOperationId> ProgramDomain::FindOperations(StringView name) {
        Vector<DomainOperationId> found;
        const Registry& registry = Registry::Instance();
        const size_t count = registry.operations.Count();
        for (size_t i = 0; i < count; ++i) {
            if (registry.operations.Get(static_cast<DomainOperationId>(i)).name.ToStringView() == name) {
                found.push_back(static_cast<DomainOperationId>(i));
            }
        }
        return found;
    }

    const DomainOperation& ProgramDomain::Operation(DomainOperationId id) {
        return Registry::Instance().operations.Get(id);
    }

    size_t ProgramDomain::OperationCount() {
        return Registry::Instance().operations.Count();
    }

    uint64_t ProgramDomain::Fingerprint() {
        const Registry& registry = Registry::Instance();
        uint64_t hash = kHashSeed;
        const size_t typeCount = registry.types.Count();
        for (size_t i = 0; i < typeCount; ++i) {
            const DomainType& type = registry.types.Get(static_cast<DomainTypeId>(i));
            hash = Fold(hash, StringUtils::GetHash(type.name.ToStringView()));
            hash = Fold(hash, type.size);
            hash = Fold(hash, static_cast<uint64_t>(type.equality));
            hash = Fold(hash, type.descriptor->opaqueHash);
        }
        const size_t operationCount = registry.operations.Count();
        for (size_t i = 0; i < operationCount; ++i) {
            const DomainOperation& operation = registry.operations.Get(static_cast<DomainOperationId>(i));
            hash = Fold(hash, StringUtils::GetHash(operation.name.ToStringView()));
            hash = Fold(hash, operation.parameterCount);
            for (size_t p = 0; p < operation.parameterCount; ++p) {
                hash = FoldValueType(hash, operation.parameters[p]);
            }
            hash = FoldValueType(hash, operation.result);
            hash = Fold(hash, operation.takesContext ? 1 : 0);
            hash = Fold(hash, operation.reads);
        }
        return hash;
    }

}  // namespace BECore
