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

    PoolStringStats PoolString::GetStats() {
        return Storage().GetStats();
    }

    Optional<PoolString> PoolString::Find(StringView str) {
        return Storage().Find(str);
    }

}  // namespace BECore
