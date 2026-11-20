#include "ScratchArena.h"

namespace BECore {

    LinearArena& ScratchArena::Get() {
        constinit static thread_local LinearArena arena("Scratch", kChunkSize);
        return arena;
    }

    ScratchScope::~ScratchScope() {
        ASSERT(&_arena == &ScratchArena::Get(), "a ScratchScope ends on the thread that made it");
        _arena.Rewind(_mark);
    }

}  // namespace BECore
