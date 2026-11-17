#include "PoolString.h"

#include <BECore/PoolString/PoolBlockSource.h>
#include <BECore/PoolString/PoolStringStorage.h>

namespace BECore {

    namespace {

        /// The pool of the process. Never destroyed: a static destructor of another file may still read a `PoolString`
        /// during exit, and the blocks must stay mapped for it (the pool frees nothing, by contract).
        struct ProcessPool {
            static constexpr size_t kInitialTableCapacity = 16384;

            UniquePtr<IPoolBlockSource> source = MakeDefaultBlockSource();
            Details::PoolStringStorage storage{*source, kInitialTableCapacity};
        };

        Details::PoolStringStorage& Storage() {
            static ProcessPool* const pool = new ProcessPool();
            return pool->storage;
        }

    }  // namespace

    PoolString PoolString::Intern(StringView str) {
        return Storage().Intern(str);
    }

    void PoolString::InternBatch(Span<const StringView> texts, Span<PoolString> out) {
        Storage().InternBatch(texts, out);
    }

    void PoolString::InternBatch(Span<const StringView> texts, Span<const uint64_t> hashes, Span<PoolString> out) {
        Storage().InternBatch(texts, hashes, out);
    }

    void PoolString::RegisterStatic(Span<const PoolStaticRegistration> registrations) {
        Storage().RegisterStatic(registrations);
    }

    PoolString PoolString::RegisterStatic(const Details::PoolStringEntry& entry) {
        PoolString value;
        const PoolStaticRegistration registration{&entry, &value};
        Storage().RegisterStatic(Span<const PoolStaticRegistration>(&registration, 1));
        return value;
    }

    void PoolString::Reserve(size_t strings, size_t bytes) {
        Storage().Reserve(strings, bytes);
    }

    PoolStringStats PoolString::GetStats() {
        return Storage().GetStats();
    }

    Optional<PoolString> PoolString::Find(StringView str) {
        return Storage().Find(str);
    }

}  // namespace BECore
