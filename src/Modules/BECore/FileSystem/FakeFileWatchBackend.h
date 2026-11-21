#pragma once

#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/Types/Types.h>
#include <EASTL/algorithm.h>

namespace BECore {

    /// A backend that watches nothing by itself: a test calls Emit(realPath) from any thread, and the calls the facade made are recorded
    class FakeFileWatchBackend : public IFileWatchBackend {
    public:
        struct Added {
            std::filesystem::path path;
            bool recursive = false;
        };

        void AddDirectory(const std::filesystem::path& realPath, bool recursive) override {
            _added.push_back({realPath, recursive});
            _directories.push_back(realPath);
        }

        void RemoveDirectory(const std::filesystem::path& realPath) override {
            _removed.push_back(realPath);
            _directories.erase(eastl::remove(_directories.begin(), _directories.end(), realPath), _directories.end());
        }

        void SetSink(IFileWatchSink* sink) override {
            _sink = sink;
        }

        void Tick(double nowSeconds) override {
            _lastTick = nowSeconds;
        }

        void Shutdown() override {
            ++_shutdownCount;
        }

        /// What the platform would report for a change of @p realPath. Posts even for a path that no directory covers: filtering is the facade's job
        void Emit(const std::filesystem::path& realPath) {
            if (_sink != nullptr) {
                _sink->Post(realPath);
            }
        }

        [[nodiscard]] const Vector<Added>& AddedDirectories() const {
            return _added;
        }

        [[nodiscard]] const Vector<std::filesystem::path>& RemovedDirectories() const {
            return _removed;
        }

        /// The directories added and not removed yet
        [[nodiscard]] const Vector<std::filesystem::path>& Directories() const {
            return _directories;
        }

        [[nodiscard]] int ShutdownCount() const {
            return _shutdownCount;
        }

        [[nodiscard]] bool HasSink() const {
            return _sink != nullptr;
        }

        [[nodiscard]] double LastTick() const {
            return _lastTick;
        }

    private:
        IFileWatchSink* _sink = nullptr;
        Vector<Added> _added;
        Vector<std::filesystem::path> _removed;
        Vector<std::filesystem::path> _directories;
        int _shutdownCount = 0;
        double _lastTick = 0.0;
    };

}  // namespace BECore
