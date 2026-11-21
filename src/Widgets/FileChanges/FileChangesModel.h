#pragma once

#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief What the File changes panel shows, without ImGui: the list the last look found and the result of the last apply.
     *
     * Nothing here runs by itself. Each call is one request of the person (a button): the model reads the disk only inside Rescan(), and the panel
     * never calls anything while it is closed or idle.
     */
    class FileChangesModel {
    public:
        /// Looks at the disk (`FileWatcher::Rescan`) and replaces the list; the result of the last apply stays until the next apply
        void Rescan(FileWatcher& watcher);

        /// Applies every pending change; the list is empty afterwards and holds what a rescan finds from then on
        void ApplyAll(FileWatcher& watcher);

        /// Applies the change of @p virtualPath only; the other rows stay
        void ApplyOne(FileWatcher& watcher, PoolString virtualPath);

        [[nodiscard]] const Vector<PendingFileChange>& GetPending() const {
            return _pending;
        }

        /// The result of the last apply; empty before the first one
        [[nodiscard]] const Vector<AppliedFileChange>& GetApplied() const {
            return _applied;
        }

        /// Whether a rescan ran since the model was made: a panel that never looked says so instead of "nothing changed"
        [[nodiscard]] bool HasScanned() const {
            return _scanned;
        }

    private:
        Vector<PendingFileChange> _pending;
        Vector<AppliedFileChange> _applied;
        bool _scanned = false;
    };

}  // namespace BECore
