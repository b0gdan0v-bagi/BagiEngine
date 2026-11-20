#include "FileWatcher.h"

#include <EASTL/algorithm.h>
#include <chrono>
#include <system_error>

namespace BECore {

    namespace {

        double SteadyNowSeconds() {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        std::filesystem::path Normalise(const std::filesystem::path& path) {
            return std::filesystem::absolute(path).lexically_normal();
        }

        /// The part of @p path below @p base, or empty when @p path is the base itself or is not inside it
        std::filesystem::path RelativeInside(const std::filesystem::path& path, const std::filesystem::path& base) {
            std::filesystem::path relative = path.lexically_relative(base);
            if (relative.empty() || relative == ".") {
                return {};
            }
            if (*relative.begin() == "..") {
                return {};
            }
            return relative;
        }

    }  // namespace

    FileWatchHandle::~FileWatchHandle() {
        Reset();
    }

    FileWatchHandle::FileWatchHandle(FileWatchHandle&& other) noexcept : _watcher(other._watcher), _id(other._id) {
        other._watcher = nullptr;
        other._id = 0;
    }

    FileWatchHandle& FileWatchHandle::operator=(FileWatchHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            _watcher = other._watcher;
            _id = other._id;
            other._watcher = nullptr;
            other._id = 0;
        }
        return *this;
    }

    bool FileWatchHandle::IsActive() const {
        return _watcher != nullptr && _watcher->IsWatchActive(_id);
    }

    void FileWatchHandle::Reset() {
        if (_watcher != nullptr) {
            _watcher->EndWatch(_id);
            _watcher = nullptr;
            _id = 0;
        }
    }

    FileWatcher::FileWatcher(const FileSystem& fileSystem) : _fileSystem(fileSystem) {}

    FileWatcher::~FileWatcher() {
        Stop();
    }

    void FileWatcher::Start(IntrusivePtrAtomic<IFileWatchBackend> backend) {
        Stop();
        if (backend == nullptr) {
            return;
        }
        _backend = std::move(backend);
        _backend->SetSink(this);
    }

    void FileWatcher::Stop() {
        if (_backend == nullptr) {
            return;
        }
        ASSERT(!_delivering);  // the callable that is running would be destroyed under itself
        _backend->Shutdown();
        _backend->SetSink(nullptr);
        _backend.Reset();
        _watches.clear();
        _directories.clear();
        _pending.clear();
        _due.clear();
        _hasDeadWatches = false;
        LockGuard lock(_queueMutex);
        _incoming.clear();
        _hasPosts.store(false, std::memory_order_relaxed);
    }

    FileWatchHandle FileWatcher::WatchFile(StringView virtualPath, FileWatchCallback callback) {
        return MakeWatch(virtualPath, false, std::move(callback));
    }

    FileWatchHandle FileWatcher::WatchFolder(StringView virtualFolder, FileWatchCallback callback) {
        return MakeWatch(virtualFolder, true, std::move(callback));
    }

    FileWatchHandle FileWatcher::MakeWatch(StringView virtualPath, bool folder, FileWatchCallback callback) {
        if (_backend == nullptr || callback == nullptr) {
            return {};
        }
        while (!virtualPath.empty() && (virtualPath.back() == '/' || virtualPath.back() == '\\')) {
            virtualPath.remove_suffix(1);
        }
        const size_t firstSlash = virtualPath.find_first_of("/\\");
        const StringView mountPoint = virtualPath.substr(0, firstSlash);
        const StringView remaining = firstSlash == StringView::npos ? StringView{} : virtualPath.substr(firstSlash + 1);
        if (mountPoint.empty() || (!folder && remaining.empty())) {
            return {};
        }

        auto watch = MakeUnique<Watch>();
        for (const std::filesystem::path& layer : _fileSystem.GetMountLayers(PoolString::Intern(mountPoint))) {
            std::filesystem::path target = layer;
            if (!remaining.empty()) {
                target /= ToStd(remaining);
            }
            target = Normalise(target);
            const std::filesystem::path directory = folder ? target : target.parent_path();
            std::error_code error;
            if (!std::filesystem::is_directory(directory, error)) {
                continue;
            }
            watch->targets.push_back(std::move(target));
            watch->directories.push_back(directory);
        }
        if (watch->directories.empty()) {
            return {};
        }

        watch->id = _nextId++;
        watch->folder = folder;
        watch->virtualPath = String(virtualPath);
        watch->virtualName = PoolString::Intern(virtualPath);
        watch->callback = std::move(callback);
        for (const std::filesystem::path& directory : watch->directories) {
            AcquireDirectory(directory, folder);
        }
        const uint64_t id = watch->id;
        _watches.push_back(std::move(watch));
        return FileWatchHandle(this, id);
    }

    void FileWatcher::AcquireDirectory(const std::filesystem::path& directory, bool recursive) {
        for (DirectoryUse& use : _directories) {
            if (use.path != directory) {
                continue;
            }
            ++use.uses;
            if (recursive && use.recursiveUses++ == 0) {
                // Watched non-recursively until now: the backend replaces its watch
                _backend->RemoveDirectory(directory);
                _backend->AddDirectory(directory, true);
            }
            return;
        }
        _directories.push_back({directory, 1, recursive ? 1 : 0});
        _backend->AddDirectory(directory, recursive);
    }

    void FileWatcher::ReleaseDirectory(const std::filesystem::path& directory, bool recursive) {
        for (size_t i = 0; i < _directories.size(); ++i) {
            DirectoryUse& use = _directories[i];
            if (use.path != directory) {
                continue;
            }
            --use.uses;
            if (use.uses == 0) {
                _backend->RemoveDirectory(directory);
                _directories.erase(_directories.begin() + static_cast<ptrdiff_t>(i));
            } else if (recursive && --use.recursiveUses == 0) {
                _backend->RemoveDirectory(directory);
                _backend->AddDirectory(directory, false);
            }
            return;
        }
    }

    void FileWatcher::Post(const std::filesystem::path& realPath) {
        {
            LockGuard lock(_queueMutex);
            _incoming.push_back(realPath);
        }
        _hasPosts.store(true, std::memory_order_release);
    }

    void FileWatcher::Update() {
        if (_backend == nullptr) {
            return;
        }
        Update(SteadyNowSeconds());
    }

    void FileWatcher::Update(double nowSeconds) {
        if (_backend == nullptr || _delivering) {
            return;
        }
        _backend->Tick(nowSeconds);
        if (_hasPosts.exchange(false, std::memory_order_acquire)) {
            {
                LockGuard lock(_queueMutex);
                _routing.swap(_incoming);
            }
            for (const std::filesystem::path& realPath : _routing) {
                RouteChange(realPath, nowSeconds);
            }
            _routing.clear();
        }
        if (!_pending.empty()) {
            Deliver(nowSeconds);
        }
    }

    void FileWatcher::RouteChange(const std::filesystem::path& realPath, double nowSeconds) {
        const std::filesystem::path path = realPath.lexically_normal();
        for (const UniquePtr<Watch>& watch : _watches) {
            if (!watch->alive) {
                continue;
            }
            for (const std::filesystem::path& target : watch->targets) {
                if (!watch->folder) {
                    if (path == target) {
                        Schedule(watch->id, watch->virtualName, nowSeconds);
                        break;
                    }
                    continue;
                }
                const std::filesystem::path relative = RelativeInside(path, target);
                if (!relative.empty()) {
                    Schedule(watch->id, PoolString::Intern(Format("{}/{}", watch->virtualPath, FromStdString(relative.generic_string()))), nowSeconds);
                    break;
                }
            }
        }
    }

    void FileWatcher::Schedule(uint64_t watchId, PoolString virtualPath, double nowSeconds) {
        for (Pending& pending : _pending) {
            if (pending.watchId == watchId && pending.virtualPath == virtualPath) {
                pending.lastPost = nowSeconds;
                return;
            }
        }
        _pending.push_back({watchId, virtualPath, nowSeconds});
    }

    void FileWatcher::Deliver(double nowSeconds) {
        size_t kept = 0;
        for (size_t i = 0; i < _pending.size(); ++i) {
            if (nowSeconds - _pending[i].lastPost >= kFileWatchSettleSeconds) {
                _due.push_back(_pending[i]);
            } else {
                _pending[kept++] = _pending[i];
            }
        }
        _pending.resize(kept);

        _delivering = true;
        for (size_t i = 0; i < _due.size(); ++i) {
            Watch* watch = FindWatch(_due[i].watchId);
            if (watch != nullptr && watch->alive) {
                watch->callback(FileChange{_due[i].virtualPath});
            }
        }
        _delivering = false;
        _due.clear();
        SweepDeadWatches();
    }

    FileWatcher::Watch* FileWatcher::FindWatch(uint64_t id) const {
        for (const UniquePtr<Watch>& watch : _watches) {
            if (watch->id == id) {
                return watch.get();
            }
        }
        return nullptr;
    }

    bool FileWatcher::IsWatchActive(uint64_t id) const {
        const Watch* watch = FindWatch(id);
        return watch != nullptr && watch->alive;
    }

    void FileWatcher::EndWatch(uint64_t id) {
        Watch* watch = FindWatch(id);
        if (watch == nullptr || !watch->alive) {
            return;
        }
        watch->alive = false;
        _hasDeadWatches = true;
        _pending.erase(eastl::remove_if(_pending.begin(), _pending.end(), [id](const Pending& pending) { return pending.watchId == id; }), _pending.end());
        if (!_delivering) {
            SweepDeadWatches();
        }
    }

    void FileWatcher::SweepDeadWatches() {
        if (!_hasDeadWatches) {
            return;
        }
        _hasDeadWatches = false;
        for (size_t i = 0; i < _watches.size();) {
            Watch& watch = *_watches[i];
            if (watch.alive) {
                ++i;
                continue;
            }
            for (const std::filesystem::path& directory : watch.directories) {
                ReleaseDirectory(directory, watch.folder);
            }
            _watches.erase(_watches.begin() + static_cast<ptrdiff_t>(i));
        }
    }

}  // namespace BECore
