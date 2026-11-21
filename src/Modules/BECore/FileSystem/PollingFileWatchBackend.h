#pragma once

#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/Types/Types.h>
#include <cstdint>
#include <filesystem>

namespace BECore {

    /// How often the polling backend looks at the disk, in seconds
    inline constexpr double kPollIntervalSeconds = 0.5;

    /**
     * @brief Watches by looking: every kPollIntervalSeconds it compares the write time and the size of each known file, and the write time
     * of each folder, with the last look. The fallback for a platform without a native API, and the contract every other backend is tested against.
     *
     * No thread: it works in Tick (FileWatcher::Update), so every Post comes from the main thread. A poll that finds nothing changed only
     * stats the known files and folders and allocates nothing; a changed folder is listed again (that allocates). A change that keeps both the
     * write time and the size of a file is not seen, and a file made and removed between two looks is not seen at all.
     */
    class PollingFileWatchBackend final : public IFileWatchBackend {
    public:
        void AddDirectory(const std::filesystem::path& realPath, bool recursive) override;
        void RemoveDirectory(const std::filesystem::path& realPath) override;

        void SetSink(IFileWatchSink* sink) override {
            _sink = sink;
        }

        void Tick(double nowSeconds) override;
        void Shutdown() override;

    private:
        struct Entry {
            std::filesystem::path path;
            std::filesystem::file_time_type writeTime{};
            uintmax_t size = 0;
            bool directory = false;
            bool present = true;
        };

        /// Entries are sorted by path; the first is the watched folder itself, so a file made in it shows as a change of the folder's write time
        struct Watched {
            std::filesystem::path root;
            bool recursive = false;
            Vector<Entry> entries;
        };

        static bool ReadState(Entry& entry);
        void Poll(Watched& watched);
        /// Lists the folder again; the new files it finds are reported unless this is the baseline of AddDirectory
        void Rescan(Watched& watched, bool reportNewFiles);
        void Report(const std::filesystem::path& path) const;

        IFileWatchSink* _sink = nullptr;
        Vector<Watched> _watched;
        double _nextPollAt = 0.0;
    };

}  // namespace BECore
