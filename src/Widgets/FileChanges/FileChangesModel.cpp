#include "FileChangesModel.h"

#include <EASTL/algorithm.h>

namespace BECore {

    void FileChangesModel::Rescan(FileWatcher& watcher) {
        _pending = watcher.Rescan();
        _scanned = true;
    }

    void FileChangesModel::ApplyAll(FileWatcher& watcher) {
        _applied = watcher.Apply();
        _pending.clear();
        _scanned = true;
    }

    void FileChangesModel::ApplyOne(FileWatcher& watcher, PoolString virtualPath) {
        const StringView paths[] = {virtualPath.ToStringView()};
        _applied = watcher.Apply(Span<const StringView>(paths, 1));
        _pending.erase(eastl::remove_if(_pending.begin(), _pending.end(), [virtualPath](const PendingFileChange& change) { return change.virtualPath == virtualPath; }), _pending.end());
    }

}  // namespace BECore
