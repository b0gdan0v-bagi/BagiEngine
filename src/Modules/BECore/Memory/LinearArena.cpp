#include "LinearArena.h"

#include <BECore/Memory/ArenaPoison.h>

namespace BECore {

    namespace {

        /// The offset in @p data at which an address aligned to @p alignment is, at or after @p offset
        size_t AlignedOffset(const std::byte* data, size_t offset, size_t alignment) {
            const auto base = reinterpret_cast<uintptr_t>(data);
            const uintptr_t aligned = (base + offset + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
            return static_cast<size_t>(aligned - base);
        }

    }  // namespace

    LinearArena::~LinearArena() {
        Release();
        if (_first.data != nullptr) {
            ArenaPoison::Unpoison(_first.data, _first.size);
            if (_ownsFirst) {
                ::operator delete(_first.data);
            }
        }
        _stats.Release();
    }

    void* LinearArena::Allocate(size_t size, size_t alignment) {
        ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0, "the alignment of an arena allocation is a power of two");
        size = std::max<size_t>(size, 1);
        if (!_statsClaimed) {
            _statsClaimed = true;
            _stats = ArenaStatsSlot::Claim(_name);
            if (_first.data != nullptr) {
                ArenaPoison::Poison(_first.data, _first.size);
            }
            PublishCapacity();
        }

        Chunk* chunk = _current != nullptr ? _current : (_first.data != nullptr ? &_first : nullptr);
        size_t from = _current != nullptr ? _offset : 0;
        for (; chunk != nullptr; chunk = chunk->next, from = 0) {
            const size_t start = AlignedOffset(chunk->data, from, alignment);
            if (start <= chunk->size && size <= chunk->size - start) {
                return Commit(*chunk, from, start, size);
            }
        }
        return AllocateInNewChunk(size, alignment);
    }

    void* LinearArena::AllocateArray(size_t count, size_t elementSize, size_t alignment) {
        ASSERT(count <= SIZE_MAX / elementSize, "the array does not fit in the address space");
        return Allocate(count * elementSize, alignment);
    }

    void* LinearArena::Commit(Chunk& chunk, size_t from, size_t start, size_t size) {
        if (_current != &chunk) {
            if (_current != nullptr) {
                _current->top = _offset;
            }
            _current = &chunk;
        }
        _offset = start + size;
        _used += _offset - from;
        std::byte* result = chunk.data + start;
        ArenaPoison::Unpoison(result, size);
        _stats.PublishUsed(_used);
        return result;
    }

    void* LinearArena::AllocateInNewChunk(size_t size, size_t alignment) {
        const size_t dataSize = std::max(_chunkSize, size + alignment);
        Chunk* chunk = nullptr;
        if (_first.data == nullptr) {
            _first = Chunk{static_cast<std::byte*>(::operator new(dataSize)), dataSize, 0, nullptr};
            _ownsFirst = true;
            chunk = &_first;
        } else {
            auto* block = static_cast<std::byte*>(::operator new(kHeaderSize + dataSize));
            chunk = std::construct_at(reinterpret_cast<Chunk*>(block), Chunk{block + kHeaderSize, dataSize, 0, nullptr});
            Chunk* tail = &_first;
            while (tail->next != nullptr) {
                tail = tail->next;
            }
            tail->next = chunk;
            _stats.CountOverflow();
        }
        _capacity += dataSize;
        ++_chunkCount;
        ArenaPoison::Poison(chunk->data, dataSize);
        PublishCapacity();
        return Commit(*chunk, 0, AlignedOffset(chunk->data, 0, alignment), size);
    }

    LinearArena::Mark LinearArena::GetMark() const {
        Mark mark;
        mark._chunk = _current;
        mark._offset = _offset;
        mark._used = _used;
#if ENGINE_ENABLE_ASSERTS
        mark._generation = _generation;
#endif
        return mark;
    }

    void LinearArena::Rewind(const Mark& mark) {
#if ENGINE_ENABLE_ASSERTS
        ASSERT(mark._generation == _generation, "a mark taken before Reset or Release is invalid");
        if (mark._generation != _generation) {
            return;
        }
#endif
        ASSERT(mark._used <= _used, "Rewind goes back: the mark is ahead of the arena");
        if (_current != nullptr) {
            auto* chunk = static_cast<Chunk*>(mark._chunk);
            TakeBack(chunk != nullptr ? chunk : &_first, chunk != nullptr ? mark._offset : 0);
        }
        _current = static_cast<Chunk*>(mark._chunk);
        _offset = mark._offset;
        _used = mark._used;
        _stats.PublishUsed(_used);
    }

    void LinearArena::Reset() {
        if (_current != nullptr) {
            TakeBack(&_first, 0);
        }
        _current = nullptr;
        _offset = 0;
        _used = 0;
#if ENGINE_ENABLE_ASSERTS
        ++_generation;
#endif
        _stats.CountReset();
        _stats.PublishUsed(0);
    }

    void LinearArena::Release() {
        Reset();
        FreeChunks(_first.next);
        _first.next = nullptr;
        _capacity = _first.data != nullptr ? _first.size : 0;
        _chunkCount = _first.data != nullptr ? 1 : 0;
        PublishCapacity();
    }

    void LinearArena::TakeBack(Chunk* from, size_t offset) {
        for (Chunk* chunk = from; chunk != nullptr; chunk = chunk->next) {
            const size_t extent = chunk == _current ? _offset : chunk->top;
            if (extent > offset) {
                ArenaPoison::Poison(chunk->data + offset, extent - offset);
            }
            chunk->top = 0;
            offset = 0;
            if (chunk == _current) {
                break;
            }
        }
    }

    void LinearArena::FreeChunks(Chunk* head) {
        while (head != nullptr) {
            Chunk* next = head->next;
            ArenaPoison::Unpoison(head->data, head->size);
            ::operator delete(reinterpret_cast<std::byte*>(head));
            head = next;
        }
    }

    void LinearArena::PublishCapacity() {
        _stats.PublishCapacity(_capacity, _chunkCount);
    }

}  // namespace BECore
