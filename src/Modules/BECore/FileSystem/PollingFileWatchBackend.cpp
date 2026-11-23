#include "PollingFileWatchBackend.h"

#include <EASTL/sort.h>
#include <system_error>

namespace BECore {

    /// What the disk says about @p entry now; false when it is gone
    bool PollingFileWatchBackend::ReadState(Entry& entry) {
        std::error_code error;
        entry.writeTime = std::filesystem::last_write_time(entry.path, error);
        if (error) {
            return false;
        }
        if (!entry.directory) {
            entry.size = std::filesystem::file_size(entry.path, error);
        }
        return !error;
    }

    void PollingFileWatchBackend::AddDirectory(const std::filesystem::path& realPath, bool recursive) {
        RemoveDirectory(realPath);
        Watched& watched = _watched.emplace_back();
        watched.root = realPath;
        watched.recursive = recursive;
        // The files that exist now are the baseline: only a later change is reported
        Rescan(watched, false);
    }

    void PollingFileWatchBackend::RemoveDirectory(const std::filesystem::path& realPath) {
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

    void PollingFileWatchBackend::Report(const std::filesystem::path& path) const {
        if (_sink != nullptr) {
            _sink->Post(path);
        }
    }

    void PollingFileWatchBackend::Poll(Watched& watched) {
        bool listAgain = false;
        const std::filesystem::file_time_type unsettledSince = watched.listedAt - kFolderStampUnsettledSeconds;
        for (Entry& entry : watched.entries) {
            const std::filesystem::file_time_type lastTime = entry.writeTime;
            const uintmax_t lastSize = entry.size;
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
                listAgain = listAgain || entry.writeTime != lastTime || entry.writeTime >= unsettledSince;
            } else if (entry.writeTime != lastTime || entry.size != lastSize) {
                Report(entry.path);
            }
        }
        if (listAgain) {
            Rescan(watched, true);
        }
    }

    void PollingFileWatchBackend::Rescan(Watched& watched, bool reportNewFiles) {
        watched.listedAt = std::filesystem::file_time_type::clock::now();
        Vector<Entry> fresh;
        Entry& root = fresh.emplace_back();
        root.path = watched.root;
        root.directory = true;
        root.present = ReadState(root);

        std::error_code error;
        constexpr auto kOptions = std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::recursive_directory_iterator it(watched.root, kOptions, error);  //-NoStd: lists a changed folder; a poll that changed nothing does not
        const std::filesystem::recursive_directory_iterator end;                          //-NoStd: the end marker of the listing above
        while (!error && it != end) {
            Entry entry;
            std::error_code entryError;
            entry.path = it->path();
            entry.directory = it->is_directory(entryError);
            if (!watched.recursive && entry.directory) {
                it.disable_recursion_pending();
            } else if (ReadState(entry)) {
                fresh.push_back(std::move(entry));
            }
            it.increment(error);
        }
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
