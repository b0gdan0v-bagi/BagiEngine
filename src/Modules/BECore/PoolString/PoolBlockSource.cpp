#include "PoolBlockSource.h"

#if defined(PLATFORM_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace BECore {

    namespace {

        constexpr size_t kWordBytes = sizeof(uint64_t);

        /// The least step a commit is rounded to; every page size in use divides it
        constexpr size_t kCommitGranularity = size_t{64} * 1024;

        constexpr size_t AlignUp(size_t value, size_t alignment) {
            return (value + alignment - 1) / alignment * alignment;
        }

        class HeapBlockSource final : public IPoolBlockSource {
            // uint64_t words, so every range is aligned to 8
            Vector<UniquePtr<uint64_t[]>> _blocks;

        public:
            PoolMemory Allocate(size_t bytes) override {
                ASSERT(bytes > 0 && bytes % kWordBytes == 0);
                const size_t words = AlignUp(bytes, kWordBytes) / kWordBytes;
                UniquePtr<uint64_t[]> block = MakeUnique<uint64_t[]>(words);
                void* data = block.get();
                _blocks.push_back(eastl::move(block));
                return {data, words * kWordBytes};
            }
        };

#if !defined(__EMSCRIPTEN__)

        /// Address space reserved once and made usable in chunks as the bump pointer moves into them. Nothing is copied or
        /// moved, so a range handed out stays valid for the life of the source.
        class VirtualBlockSource final : public IPoolBlockSource {
            char* _base;
            size_t _limit;       // bytes the caller may take: the requested reservation
            size_t _mapped;      // bytes reserved: `_limit` rounded up to a whole chunk
            size_t _chunkBytes;  // commit step
            size_t _committed = 0;
            size_t _used = 0;

            static void* Reserve(size_t bytes) {
#if defined(PLATFORM_WINDOWS)
                return VirtualAlloc(nullptr, bytes, MEM_RESERVE, PAGE_NOACCESS);
#else
                void* address = mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
                return address == MAP_FAILED ? nullptr : address;
#endif
            }

            bool Commit(size_t offset, size_t bytes) const {
#if defined(PLATFORM_WINDOWS)
                return VirtualAlloc(_base + offset, bytes, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
                return mprotect(_base + offset, bytes, PROT_READ | PROT_WRITE) == 0;
#endif
            }

        public:
            VirtualBlockSource(char* base, size_t limit, size_t mapped, size_t chunkBytes) : _base(base), _limit(limit), _mapped(mapped), _chunkBytes(chunkBytes) {}

            VirtualBlockSource(const VirtualBlockSource&) = delete;
            VirtualBlockSource& operator=(const VirtualBlockSource&) = delete;

            ~VirtualBlockSource() override {
#if defined(PLATFORM_WINDOWS)
                VirtualFree(_base, 0, MEM_RELEASE);
#else
                munmap(_base, _mapped);
#endif
            }

            /// Null when the address space is refused
            static UniquePtr<IPoolBlockSource> Create(size_t reserveBytes, size_t commitChunkBytes) {
#if !defined(PLATFORM_WINDOWS)
                ASSERT(kCommitGranularity % static_cast<size_t>(sysconf(_SC_PAGESIZE)) == 0);
#endif
                const size_t chunk = AlignUp(commitChunkBytes == 0 ? kCommitGranularity : commitChunkBytes, kCommitGranularity);
                const size_t limit = reserveBytes / kWordBytes * kWordBytes;
                // A reservation too large to round up cannot be mapped anyway
                if (limit == 0 || limit > SIZE_MAX - chunk) {
                    return nullptr;
                }
                const size_t mapped = AlignUp(limit, chunk);
                void* base = Reserve(mapped);
                if (base == nullptr) {
                    return nullptr;
                }
                return MakeUnique<VirtualBlockSource>(static_cast<char*>(base), limit, mapped, chunk);
            }

            PoolMemory Allocate(size_t bytes) override {
                ASSERT(bytes > 0 && bytes % kWordBytes == 0);
                bytes = AlignUp(bytes, kWordBytes);
                // `_used <= _limit`, so the subtraction cannot wrap and a huge `bytes` cannot overflow the sum
                if (bytes > _limit - _used) {
                    return {};
                }
                const size_t end = _used + bytes;
                if (end > _committed) {
                    const size_t newCommitted = std::min(AlignUp(end, _chunkBytes), _mapped);
                    if (!Commit(_committed, newCommitted - _committed)) {
                        return {};
                    }
                    _committed = newCommitted;
                }
                void* data = _base + _used;
                _used = end;
                return {data, bytes};
            }
        };

#endif

    }  // namespace

    UniquePtr<IPoolBlockSource> MakeHeapBlockSource() {
        return MakeUnique<HeapBlockSource>();
    }

    UniquePtr<IPoolBlockSource> MakeVirtualBlockSource([[maybe_unused]] size_t reserveBytes, [[maybe_unused]] size_t commitChunkBytes) {
#if defined(__EMSCRIPTEN__)
        return nullptr;
#else
        return VirtualBlockSource::Create(reserveBytes, commitChunkBytes);
#endif
    }

    UniquePtr<IPoolBlockSource> MakeDefaultBlockSource() {
        UniquePtr<IPoolBlockSource> source = MakeVirtualBlockSource();
        return source ? eastl::move(source) : MakeHeapBlockSource();
    }

}  // namespace BECore
