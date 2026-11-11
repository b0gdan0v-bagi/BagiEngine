#pragma once

/**
 * The profiling macros of the engine and of games (BECore/Profiler/CLAUDE.md). With ENGINE_PROFILER=OFF (BE_PROFILER=0) every one
 * of them is `static_cast<void>(0)`: nothing is recorded and the arguments are not evaluated.
 *
 *   BE_PROFILE_SCOPE("Name")       a zone from here to the end of the scope; the name is a string literal
 *   BE_PROFILE_FUNCTION()          a zone named after the function
 *   BE_PROFILE_FRAME()             the end of a frame
 *   BE_PROFILE_COUNTER("Name", v)  a named value at this moment
 *   BE_PROFILE_THREAD("Name")      names the calling thread (the text is copied)
 *   BE_PROFILE_THREAD_ORDER("Name", n)  the same, and the thread's place in the list of threads (lower is higher; Chrome trace)
 *   BE_PROFILE_NO_ALLOC("Name")    no heap allocation is expected until the end of the scope; one that happens is reported, never
 *                                  fatal (needs ENGINE_PROFILER_ALLOC, otherwise nothing)
 *
 * Zones are for coarse work (a frame stage, a system, a rollout), not for a function called millions of times per second.
 */

#include <BECore/Profiler/Alloc/NoAllocScope.h>
#include <BECore/Profiler/ProfileZone.h>

#define BE_PROFILE_CONCAT_INNER(a, b) a##b
#define BE_PROFILE_CONCAT(a, b) BE_PROFILE_CONCAT_INNER(a, b)

#if BE_PROFILER

#define BE_PROFILE_SCOPE_WITH_ID(name, id)                                                                                                                                                             \
    static constexpr ::BECore::ProfileSite BE_PROFILE_CONCAT(beProfileSite, id){name, __FUNCTION__, __FILE__, static_cast<uint32_t>(__LINE__), 0};                                                     \
    const ::BECore::ProfileZone BE_PROFILE_CONCAT(beProfileZone, id)(BE_PROFILE_CONCAT(beProfileSite, id))
#define BE_PROFILE_SCOPE(name) BE_PROFILE_SCOPE_WITH_ID(name, __COUNTER__)
#define BE_PROFILE_FUNCTION() BE_PROFILE_SCOPE_WITH_ID(__FUNCTION__, __COUNTER__)
#define BE_PROFILE_FRAME() ::BECore::Profiler::FrameMark()
#define BE_PROFILE_COUNTER(name, value) ::BECore::Profiler::Counter(name, static_cast<double>(value))
#define BE_PROFILE_THREAD(name) ::BECore::Profiler::SetThreadName(name)
#define BE_PROFILE_THREAD_ORDER(name, sortIndex) ::BECore::Profiler::SetThreadName(name, static_cast<uint32_t>(sortIndex))

#else

#define BE_PROFILE_SCOPE(name) static_cast<void>(0)
#define BE_PROFILE_FUNCTION() static_cast<void>(0)
#define BE_PROFILE_FRAME() static_cast<void>(0)
#define BE_PROFILE_COUNTER(name, value) static_cast<void>(0)
#define BE_PROFILE_THREAD(name) static_cast<void>(0)
#define BE_PROFILE_THREAD_ORDER(name, sortIndex) static_cast<void>(0)

#endif

#if BE_PROFILER_ALLOC

#define BE_PROFILE_NO_ALLOC_WITH_ID(name, id)                                                                                                                                                          \
    static constexpr ::BECore::ProfileSite BE_PROFILE_CONCAT(beNoAllocSite, id){name, __FUNCTION__, __FILE__, static_cast<uint32_t>(__LINE__), 0};                                                     \
    const ::BECore::NoAllocScope BE_PROFILE_CONCAT(beNoAllocScope, id)(BE_PROFILE_CONCAT(beNoAllocSite, id))
#define BE_PROFILE_NO_ALLOC(name) BE_PROFILE_NO_ALLOC_WITH_ID(name, __COUNTER__)

#else

#define BE_PROFILE_NO_ALLOC(name) static_cast<void>(0)

#endif
