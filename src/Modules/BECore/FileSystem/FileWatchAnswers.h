#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// A change of `files_rescan`; `kind` is lower case (`changed`, `added`, `removed`), which a reflected enum would not write
    struct FileWatchRescanChange {
        BE_CLASS(FileWatchRescanChange)

        BE_REFLECT_FIELD ArenaVector<PoolString> consumers;
        BE_REFLECT_FIELD PoolString kind;
        BE_REFLECT_FIELD PoolString path;
    };

    /// `files_rescan`
    struct FileWatchRescanAnswer {
        BE_CLASS(FileWatchRescanAnswer)

        BE_REFLECT_FIELD ArenaVector<FileWatchRescanChange> changes;
    };

    /// A change of `files_apply`: `error` only when `ok` is false, a view of the text `done` holds until the answer is written
    struct FileWatchAppliedChange {
        BE_CLASS(FileWatchAppliedChange)

        BE_REFLECT_FIELD ArenaVector<PoolString> consumers;
        BE_REFLECT_FIELD Optional<TextView> error;
        BE_REFLECT_FIELD bool ok = true;
        BE_REFLECT_FIELD PoolString path;
    };

    /// `files_apply`
    struct FileWatchApplyAnswer {
        BE_CLASS(FileWatchApplyAnswer)

        BE_REFLECT_FIELD ArenaVector<FileWatchAppliedChange> applied;
    };

}  // namespace BECore
