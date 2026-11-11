#include "FrameRingBackend.h"

#include <BECore/Profiler/Profiler.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cstdlib>
#include <new>
#include <thread>

namespace BECore {

    namespace {

        /// The rings get ids 1, 2, ...; a thread caches the buffer of the last ring it wrote to
        constinit std::atomic<uint64_t> g_nextRingId{1};
        constinit thread_local uint64_t t_cachedRing = 0;
        constinit thread_local void* t_cachedBuffer = nullptr;

        /// A slot is five words: site, begin tick, end tick, allocation count with the depth in its top byte, allocated bytes
        constexpr size_t kSlotWords = 5;
        constexpr uint32_t kDepthShift = 56;
        constexpr uint64_t kCountMask = (uint64_t{1} << kDepthShift) - 1;
        constexpr uint32_t kSlotMask = FrameRingBackend::kEventsPerThread - 1;
        static_assert((FrameRingBackend::kEventsPerThread & kSlotMask) == 0, "kEventsPerThread must be a power of two");
        static_assert(FrameRingBackend::kMaxZoneDepth < 256, "the depth is kept in one byte");

        void StoreWord(uint64_t& word, uint64_t value) {
            std::atomic_ref<uint64_t>(word).store(value, std::memory_order_relaxed);
        }

        uint64_t LoadWord(uint64_t& word) {
            return std::atomic_ref<uint64_t>(word).load(std::memory_order_relaxed);
        }

        /// The first entry of a ring of @p capacity that is intact while a writer is writing entry @p reserved - 1 (it overwrites reserved - 1 - capacity)
        uint64_t FirstIntactIndex(uint64_t reserved, uint32_t capacity) {
            return reserved > capacity ? reserved - capacity : 0;
        }

    }  // namespace

    struct FrameRingBackend::ThreadBuffer {
        std::thread::id owner;
        uint32_t threadId = 0;
        uint64_t* slots = nullptr;          // kEventsPerThread * kSlotWords words, written through atomic_ref by the owner
        std::atomic<uint64_t> count{0};     // entries [count - capacity, count) are complete; written by the owner only
        std::atomic<uint64_t> reserved{0};  // the owner is writing entry reserved - 1 (it overwrites reserved - 1 - capacity)
        // Owner only
        uint64_t generation = 0;  // the ring's generation the open stack belongs to
        uint32_t depth = 0;       // zones open now, also those deeper than kMaxZoneDepth
        uint64_t openBegin[kMaxZoneDepth]{};
    };

    FrameRingBackend::FrameRingBackend() : FrameRingBackend(Options{}) {}

    FrameRingBackend::FrameRingBackend(const Options& options) : _options(options), _id(g_nextRingId.fetch_add(1, std::memory_order_relaxed)) {
        _epochTicks = options.now();
    }

    FrameRingBackend::~FrameRingBackend() {
        const uint32_t count = _bufferCount.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < count; ++i) {
            ThreadBuffer* buffer = _buffers[i];
            std::free(buffer->slots);
            buffer->~ThreadBuffer();
            std::free(buffer);
        }
    }

    bool FrameRingBackend::Start() {
        _generation.fetch_add(1, std::memory_order_relaxed);
        // The clock is read under the mark mutex, so no mark of the new epoch is older than its start
        std::lock_guard markLock(_markMutex);
        const uint64_t ticks = _options.now();
        std::lock_guard epochLock(_epochMutex);
        _epochMark = _markCount.load(std::memory_order_relaxed);
        _epochTicks = ticks;
        return true;
    }

    FrameRingBackend::ThreadBuffer* FrameRingBackend::GetThreadBuffer() {
        if (t_cachedRing == _id) {
            return static_cast<ThreadBuffer*>(t_cachedBuffer);
        }

        const std::thread::id self = std::this_thread::get_id();
        std::lock_guard lock(_registryMutex);
        ThreadBuffer* found = nullptr;
        const uint32_t count = _bufferCount.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < count && found == nullptr; ++i) {
            if (_buffers[i]->owner == self) {
                found = _buffers[i];
            }
        }
        if (found == nullptr) {
            if (count == kMaxThreadBuffers) {
                return nullptr;
            }
            // std::malloc, not new: recording must not enter the global operator new that ENGINE_PROFILER_ALLOC counts
            void* memory = std::malloc(sizeof(ThreadBuffer));
            auto* slots = static_cast<uint64_t*>(std::malloc(sizeof(uint64_t) * kSlotWords * kEventsPerThread));
            if (memory == nullptr || slots == nullptr) {
                std::free(memory);
                std::free(slots);
                return nullptr;
            }
            found = new (memory) ThreadBuffer();
            found->owner = self;
            found->threadId = Profiler::CurrentThreadId();
            found->slots = slots;
            _buffers[count] = found;
            _bufferCount.store(count + 1, std::memory_order_release);
        }
        t_cachedRing = _id;
        t_cachedBuffer = found;
        return found;
    }

    uint64_t FrameRingBackend::BeginZone(const ProfileSite& /*site*/) {
        ThreadBuffer* buffer = GetThreadBuffer();
        if (buffer == nullptr) {
            _dropped.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        const uint64_t generation = _generation.load(std::memory_order_relaxed);
        if (buffer->generation != generation) {
            buffer->generation = generation;
            buffer->depth = 0;
        }
        if (buffer->depth < kMaxZoneDepth) {
            buffer->openBegin[buffer->depth] = _options.now();
        } else {
            _dropped.fetch_add(1, std::memory_order_relaxed);
        }
        ++buffer->depth;
        return 0;
    }

    void FrameRingBackend::EndZone(uint64_t /*token*/, const ProfileSite& site, const ZoneAllocs& allocs) {
        ThreadBuffer* buffer = GetThreadBuffer();
        if (buffer == nullptr) {
            return;
        }
        const uint64_t generation = _generation.load(std::memory_order_relaxed);
        if (buffer->generation != generation) {
            buffer->generation = generation;
            buffer->depth = 0;
        }
        if (buffer->depth == 0) {
            return;  // the ring was switched on in the middle of this zone
        }
        --buffer->depth;
        if (buffer->depth >= kMaxZoneDepth) {
            return;
        }

        const uint64_t index = buffer->count.load(std::memory_order_relaxed);
        // Announce the slot before it is overwritten: a reader that sees any of the new words then sees this store too
        buffer->reserved.store(index + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        // The end tick is read after the announce, so that a test's clock can take a snapshot at the moment a slot is claimed and not yet written
        const uint64_t endTicks = _options.now();
        uint64_t* slot = buffer->slots + (index & kSlotMask) * kSlotWords;
        StoreWord(slot[0], std::bit_cast<uintptr_t>(&site));
        StoreWord(slot[1], buffer->openBegin[buffer->depth]);
        StoreWord(slot[2], endTicks);
        StoreWord(slot[3], (allocs.count & kCountMask) | (static_cast<uint64_t>(buffer->depth) << kDepthShift));
        StoreWord(slot[4], allocs.bytes);
        buffer->count.store(index + 1, std::memory_order_release);
    }

    void FrameRingBackend::FrameMark() {
        std::lock_guard lock(_markMutex);
        const uint64_t ticks = _options.now();  // under the lock, so the stored ticks never go backwards
        const uint64_t index = _markCount.load(std::memory_order_relaxed);
        _markReserved.store(index + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        StoreWord(_markTicks[index % kMaxFrames], ticks);
        _markCount.store(index + 1, std::memory_order_release);
    }

    uint64_t FrameRingBackend::GetDroppedCount() const {
        return _dropped.load(std::memory_order_relaxed);
    }

    RingSnapshot FrameRingBackend::Snapshot() const {
        RingSnapshot snapshot;
        snapshot.nanosecondsPerTick = _options.nanosecondsPerTick;
        snapshot.dropped = GetDroppedCount();

        // Frames: mark k ends frame k, which begins at mark k-1; the first frame of an epoch (the ring made, or Start) begins at the epoch's
        // tick, and what came before the epoch is left out. The oldest mark still held has lost its predecessor, so the frame it ends is
        // left out. The marks are counted before the epoch is read: a Start between the two leaves no frame instead of one over the gap
        uint64_t epochMark = 0;
        uint64_t epochTicks = 0;
        {
            const uint64_t count = _markCount.load(std::memory_order_acquire);
            {
                std::lock_guard epochLock(_epochMutex);
                epochMark = _epochMark;
                epochTicks = _epochTicks;
            }
            const uint64_t first = count > kMaxFrames ? count - kMaxFrames : 0;
            eastl::vector<uint64_t> ticks;
            ticks.reserve(static_cast<size_t>(count - first));
            for (uint64_t index = first; index < count; ++index) {
                ticks.push_back(LoadWord(_markTicks[index % kMaxFrames]));
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint64_t intact = std::max(first, FirstIntactIndex(_markReserved.load(std::memory_order_relaxed), kMaxFrames));
            for (uint64_t index = std::max(intact, epochMark); index < count; ++index) {
                if (index == epochMark) {
                    snapshot.frames.push_back(RingFrame{index, epochTicks, ticks[static_cast<size_t>(index - first)]});
                } else if (index > intact) {
                    snapshot.frames.push_back(RingFrame{index, ticks[static_cast<size_t>(index - 1 - first)], ticks[static_cast<size_t>(index - first)]});
                }
            }
        }

        // No registry mutex: a buffer is published by the release store of the count, and is never freed or moved before the destructor
        const uint32_t bufferCount = _bufferCount.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < bufferCount; ++i) {
            const ThreadBuffer& buffer = *_buffers[i];
            RingThread& thread = snapshot.threads.emplace_back();
            thread.threadId = buffer.threadId;
            if (const char* name = Profiler::GetThreadName(buffer.threadId)) {
                thread.name = name;
            }
            thread.sortIndex = Profiler::GetThreadSortIndex(buffer.threadId);

            const uint64_t count = buffer.count.load(std::memory_order_acquire);
            const uint64_t first = FirstIntactIndex(count, kEventsPerThread);
            thread.zones.reserve(static_cast<size_t>(count - first));
            for (uint64_t index = first; index < count; ++index) {
                uint64_t* slot = buffer.slots + (index & kSlotMask) * kSlotWords;
                ZoneRecord& zone = thread.zones.emplace_back();
                zone.site = std::bit_cast<const ProfileSite*>(static_cast<uintptr_t>(LoadWord(slot[0])));
                zone.beginTicks = LoadWord(slot[1]);
                zone.endTicks = LoadWord(slot[2]);
                const uint64_t packed = LoadWord(slot[3]);
                zone.allocCount = packed & kCountMask;
                zone.depth = static_cast<uint32_t>(packed >> kDepthShift);
                zone.allocBytes = LoadWord(slot[4]);
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            // The owner may have started overwriting the oldest slots of the window while it was copied: those are dropped
            const uint64_t intact = FirstIntactIndex(buffer.reserved.load(std::memory_order_relaxed), kEventsPerThread);
            if (intact > first) {
                const size_t torn = static_cast<size_t>(std::min(intact - first, static_cast<uint64_t>(thread.zones.size())));
                thread.zones.erase(thread.zones.begin(), thread.zones.begin() + static_cast<ptrdiff_t>(torn));
            }
            eastl::sort(thread.zones.begin(), thread.zones.end(),
                        [](const ZoneRecord& a, const ZoneRecord& b) { return a.beginTicks != b.beginTicks ? a.beginTicks < b.beginTicks : a.depth < b.depth; });
            // Zones of an earlier epoch are still in the ring
            const auto older = eastl::lower_bound(thread.zones.begin(), thread.zones.end(), epochTicks, [](const ZoneRecord& zone, uint64_t ticks) { return zone.beginTicks < ticks; });
            thread.zones.erase(thread.zones.begin(), older);
        }
        return snapshot;
    }

}  // namespace BECore
