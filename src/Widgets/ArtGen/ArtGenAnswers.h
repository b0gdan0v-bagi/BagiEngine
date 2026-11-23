#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    // The answers of the `artgen_*` control methods, written by the generated `WriteJson` (`ControlCall::ReplyObject`). What stays a `Json` document by hand and why:
    // src/Widgets/ArtGen/CLAUDE.md, "The contract". An empty `Optional` writes no member, so a member that is absent never means anything else.

    /// `artgen_progress`, `artgen_image`, `artgen_done` and `artgen_setup_report`: `stale` is true when the report is not for the run or the setup in work
    struct ArtGenStaleAnswer {
        BE_CLASS(ArtGenStaleAnswer)

        BE_REFLECT_FIELD bool stale = false;
    };

    /// `artgen_generate`: the id of the request that was queued
    struct ArtGenQueuedAnswer {
        BE_CLASS(ArtGenQueuedAnswer)

        BE_REFLECT_FIELD uint32_t id = 0;
        BE_REFLECT_FIELD bool serviceConnected = false;
    };

    /// `artgen_setup`: `{}` for the cancel of the action in work, else the id of the action that was queued
    struct ArtGenSetupQueuedAnswer {
        BE_CLASS(ArtGenSetupQueuedAnswer)

        BE_REFLECT_FIELD Optional<uint32_t> id;
        BE_REFLECT_FIELD Optional<bool> serviceConnected;
    };

    /// `artgen_save_preset`
    struct ArtGenSavedAnswer {
        BE_CLASS(ArtGenSavedAnswer)

        BE_REFLECT_FIELD String path;
    };

    /// `artgen_cancel`: `{}` for the run, `{job, set, state}` for a job of a set and `{set, state}` for a whole set
    struct ArtGenCancelAnswer {
        BE_CLASS(ArtGenCancelAnswer)

        BE_REFLECT_FIELD Optional<String> job;
        BE_REFLECT_FIELD Optional<String> set;
        BE_REFLECT_FIELD Optional<String> state;
    };

}  // namespace BECore
