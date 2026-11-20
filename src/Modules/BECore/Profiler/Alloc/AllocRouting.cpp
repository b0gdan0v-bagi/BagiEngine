#include "AllocRouting.h"

#include <BECore/Profiler/Alloc/AllocPlatform.h>
#include <SDL3/SDL_stdinc.h>
#include <imgui.h>

namespace BECore {

#if BE_PROFILER_ALLOC

    namespace {

        constinit std::atomic<bool> g_installed{false};

        void* PugixmlAllocate(size_t size) {
            return AllocRouting::Malloc(AllocSource::Pugixml, size);
        }
        void PugixmlDeallocate(void* ptr) {
            AllocRouting::Free(AllocSource::Pugixml, ptr);
        }

        // ImGui hands its user-data pointer back; the engine sets none
        void* ImGuiAllocate(size_t size, void* /*userData*/) {
            return AllocRouting::Malloc(AllocSource::ImGui, size);
        }
        void ImGuiFree(void* ptr, void* /*userData*/) {
            AllocRouting::Free(AllocSource::ImGui, ptr);
        }

        void* SDLCALL SdlMalloc(size_t size) {
            return AllocRouting::Malloc(AllocSource::SDL, size);
        }
        void* SDLCALL SdlCalloc(size_t count, size_t size) {
            return AllocRouting::Calloc(AllocSource::SDL, count, size);
        }
        void* SDLCALL SdlRealloc(void* ptr, size_t size) {
            return AllocRouting::Realloc(AllocSource::SDL, ptr, size);
        }
        void SDLCALL SdlFree(void* ptr) {
            AllocRouting::Free(AllocSource::SDL, ptr);
        }

        /// Installs the routing before main: SDL_SetMemoryFunctions must precede the first SDL allocation, and tests and tools share no start-up function
        [[maybe_unused]] const bool g_installAtStartup = (AllocRouting::Install(), true);

    }  // namespace

    void AllocRouting::Install() {
        if (g_installed.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        pugi::set_memory_management_functions(PugixmlAllocate, PugixmlDeallocate);
        ImGui::SetAllocatorFunctions(ImGuiAllocate, ImGuiFree, nullptr);
        SDL_SetMemoryFunctions(SdlMalloc, SdlCalloc, SdlRealloc, SdlFree);
    }

    bool AllocRouting::IsInstalled() {
        return g_installed.load(std::memory_order_acquire);
    }

    void* AllocRouting::TrackedMalloc(AllocSource source, size_t size) {
        void* ptr = std::malloc(size);
        if (ptr != nullptr) {
            AllocTracker::OnRoutedAlloc(source, ptr, size, AllocPlatform::LibcUsableSize(ptr));
        }
        return ptr;
    }

    void* AllocRouting::TrackedCalloc(AllocSource source, size_t count, size_t size) {
        void* ptr = std::calloc(count, size);
        if (ptr != nullptr) {
            AllocTracker::OnRoutedAlloc(source, ptr, count * size, AllocPlatform::LibcUsableSize(ptr));
        }
        return ptr;
    }

    void* AllocRouting::TrackedRealloc(AllocSource source, void* ptr, size_t size) {
        if (ptr == nullptr) {
            return TrackedMalloc(source, size);
        }
        const size_t oldUsable = AllocPlatform::LibcUsableSize(ptr);
        void* result = std::realloc(ptr, size);
        if (result == nullptr) {
            // realloc(ptr, 0) frees the block on glibc and answers nullptr; a real failure keeps it
            if (size == 0) {
                AllocTracker::OnRoutedFree(source, ptr, oldUsable);
            }
            return nullptr;
        }
        // The old block is gone whether it moved or not: its address goes to the backend as a value and is never read
        AllocTracker::OnRoutedFree(source, ptr, oldUsable);
        AllocTracker::OnRoutedAlloc(source, result, size, AllocPlatform::LibcUsableSize(result));
        return result;
    }

    void AllocRouting::TrackedFree(AllocSource source, void* ptr) {
        if (ptr == nullptr) {
            return;
        }
        AllocTracker::OnRoutedFree(source, ptr, AllocPlatform::LibcUsableSize(ptr));
        std::free(ptr);
    }

#else

    void AllocRouting::Install() {}

    bool AllocRouting::IsInstalled() {
        return false;
    }

#endif

}  // namespace BECore
