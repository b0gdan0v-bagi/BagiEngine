#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// The subsystem an allocation is counted under (Profiler/Alloc/CLAUDE.md, "Tags"). `Untagged` is what no scope claimed
    CORE_ENUM(MemTag, uint8_t, Untagged, Core, UI, Text, Scene, Resource, Audio, Network, Game)

    inline constexpr size_t kMemTagCount = EnumUtils<MemTag>::Count();

    /**
     * @brief BE_MEM_TAG: every allocation the calling thread makes until the end of the scope is counted under @p tag, whoever makes
     * it (the global operator new, pugixml, ImGui, SDL, stb). Scopes nest, the innermost wins.
     *
     * The tag belongs to the thread: work that moves to another thread runs under that thread's tag. Only with ENGINE_PROFILER_ALLOC;
     * otherwise the scope is an empty object and costs nothing.
     */
#if BE_PROFILER_ALLOC
    class MemTagScope {
    public:
        explicit MemTagScope(MemTag tag);
        ~MemTagScope();

        MemTagScope(const MemTagScope&) = delete;
        MemTagScope& operator=(const MemTagScope&) = delete;

    private:
        MemTag _previous;
    };
#else
    class MemTagScope {
    public:
        explicit MemTagScope(MemTag /*tag*/) {}
        ~MemTagScope() = default;

        MemTagScope(const MemTagScope&) = delete;
        MemTagScope& operator=(const MemTagScope&) = delete;
    };
#endif

}  // namespace BECore

#define BE_MEM_TAG_CONCAT_INNER(a, b) a##b
#define BE_MEM_TAG_CONCAT(a, b) BE_MEM_TAG_CONCAT_INNER(a, b)
#define BE_MEM_TAG(Tag) const ::BECore::MemTagScope BE_MEM_TAG_CONCAT(beMemTagScope, __COUNTER__)(::BECore::MemTag::Tag)
