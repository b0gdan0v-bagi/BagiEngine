#include "PollingFileWatchBackend.h"

#include <BECore/File/File.h>
#include <EASTL/sort.h>
#include <chrono>

namespace BECore {

    /// What the disk says about @p entry now; false when it is gone
    bool PollingFileWatchBackend::ReadState(Entry& entry) {
        const Optional<FileInfo> info = GetFileInfo(entry.path.View());
        if (!info) {
            return false;
        }
        entry.writeTimeNs = info->modifyTimeNs;
        if (!entry.directory) {
            entry.size = info->size;
        }
        return true;
    }

    void PollingFileWatchBackend::AddDirectory(const Path& realPath, bool recursive) {
        RemoveDirectory(realPath);
        Watched& watched = _watched.emplace_back();
        watched.root = realPath;
        watched.recursive = recursive;
        // The files that exist now are the baseline: only a later change is reported
        Rescan(watched, false);
    }

    void PollingFileWatchBackend::RemoveDirectory(const Path& realPath) {
        for (size_t i = 0; i < _watched.size(); ++i) {
            if (_watched[i].root == realPath) {
                _watched.erase(_watched.begin() + static_cast<ptrdiff_t>(i));
                return;
            }
        }
    }

    void PollingFileWatchBackend::Shutdown() {
        _watched.clear();
    }

    void PollingFileWatchBackend::Tick(double nowSeconds) {
        if (nowSeconds < _nextPollAt) {
            return;
        }
        _nextPollAt = nowSeconds + kPollIntervalSeconds;
        for (Watched& watched : _watched) {
            Poll(watched);
        }
    }

    void PollingFileWatchBackend::Report(const Path& path) const {
        if (_sink != nullptr) {
            _sink->Post(path);
        }
    }

    void PollingFileWatchBackend::Poll(Watched& watched) {
        bool listAgain = false;
        const int64_t unsettledSinceNs = watched.listedAtNs - kFolderStampUnsettledNs;
        for (Entry& entry : watched.entries) {
            const int64_t lastTimeNs = entry.writeTimeNs;
            const uint64_t lastSize = entry.size;
            if (!ReadState(entry)) {
                if (entry.present) {
                    entry.present = false;
                    listAgain = true;
                    if (!entry.directory) {
                        Report(entry.path);
                    }
                }
                continue;
            }
            if (!entry.present) {
                entry.present = true;  // the watched folder itself came back
                listAgain = true;
            } else if (entry.directory) {
                listAgain = listAgain || entry.writeTimeNs != lastTimeNs || entry.writeTimeNs >= unsettledSinceNs;
            } else if (entry.writeTimeNs != lastTimeNs || entry.size != lastSize) {
                Report(entry.path);
            }
        }
        if (listAgain) {
            Rescan(watched, true);
        }
    }

    void PollingFileWatchBackend::Rescan(Watched& watched, bool reportNewFiles) {
        // The system clock counts from the Unix epoch, as modifyTimeNs does
        watched.listedAtNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        Vector<Entry> fresh;
        Entry& root = fresh.emplace_back();
        root.path = watched.root;
        root.directory = true;
        root.present = ReadState(root);

        // An unreadable top folder leaves only the root entry, which Poll reports as gone
        static_cast<void>(WalkDirectory(watched.root.View(), watched.recursive ? WalkMode::Tree : WalkMode::Folder, [&](StringView relative, FileKind kind) {
            // What is neither a file nor a folder (a link to nowhere, a socket) has no state to compare
            if (kind == FileKind::Other || (!watched.recursive && kind == FileKind::Directory)) {
                return true;
            }
            Entry entry;
            entry.path = watched.root / relative;
            entry.directory = kind == FileKind::Directory;
            if (ReadState(entry)) {
                fresh.push_back(eastl::move(entry));
            }
            return true;
        }));
        eastl::sort(fresh.begin(), fresh.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });

        // A file that is new since the last listing is a change; the old list is sorted the same way
        size_t known = 0;
        for (const Entry& entry : fresh) {
            while (known < watched.entries.size() && watched.entries[known].path < entry.path) {
                ++known;
            }
            const bool isKnown = known < watched.entries.size() && watched.entries[known].path == entry.path;
            if (reportNewFiles && !isKnown && !entry.directory) {
                Report(entry.path);
            }
        }
        watched.entries = std::move(fresh);
    }

}  // namespace BECore
