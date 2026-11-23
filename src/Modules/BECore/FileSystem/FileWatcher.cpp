#include "FileWatcher.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
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

        String PathKey(const std::filesystem::path& path) {
            return FromStdString(path.generic_string());
        }

        /// Reads the state of the regular file at @p path; false when it is missing or is not a file
        bool ReadState(const std::filesystem::path& path, std::filesystem::file_time_type& writeTime, uintmax_t& size) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) {
                return false;
            }
            writeTime = std::filesystem::last_write_time(path, error);
            if (error) {
                return false;
            }
            size = std::filesystem::file_size(path, error);
            return !error;
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

    void FileWatcher::Start(IntrusivePtr<IFileWatchBackend> backend) {
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
        _baseline.clear();
        _hasDeadWatches = false;
        LockGuard lock(_queueMutex);
        _incoming.clear();
        _hasPosts.store(false, std::memory_order_relaxed);
    }

    FileWatchHandle FileWatcher::WatchFile(StringView virtualPath, FileWatchCallback callback, StringView consumer) {
        return MakeWatch(virtualPath, false, std::move(callback), consumer);
    }

    FileWatchHandle FileWatcher::WatchFolder(StringView virtualFolder, FileWatchCallback callback, StringView consumer) {
        return MakeWatch(virtualFolder, true, std::move(callback), consumer);
    }

    FileWatchHandle FileWatcher::MakeWatch(StringView virtualPath, bool folder, FileWatchCallback callback, StringView consumer) {
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
        watch->consumer = consumer.empty() ? watch->virtualName : PoolString::Intern(consumer);
        watch->callback = std::move(callback);
        for (const std::filesystem::path& directory : watch->directories) {
            AcquireDirectory(directory, folder);
        }
        const uint64_t id = watch->id;
        SnapshotWatch(*watch);
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
            PoolString virtualPath;
            if (watch->alive && Matches(*watch, path, virtualPath)) {
                Schedule(watch->id, virtualPath, nowSeconds);
            }
        }
    }

    bool FileWatcher::Covers(const Watch& watch, const std::filesystem::path& path) {
        for (const std::filesystem::path& target : watch.targets) {
            if (watch.folder ? !RelativeInside(path, target).empty() : path == target) {
                return true;
            }
        }
        return false;
    }

    bool FileWatcher::Matches(const Watch& watch, const std::filesystem::path& path, PoolString& virtualPath) {
        for (const std::filesystem::path& target : watch.targets) {
            if (!watch.folder) {
                if (path == target) {
                    virtualPath = watch.virtualName;
                    return true;
                }
                continue;
            }
            const std::filesystem::path relative = RelativeInside(path, target);
            if (!relative.empty()) {
                virtualPath = PoolString::Intern(Format("{}/{}", watch.virtualPath, FromStdString(relative.generic_string())));
                return true;
            }
        }
        return false;
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
            if (watch != nullptr && watch->alive && !IsAppliedAndUnchanged(*watch, _due[i].virtualPath.ToStringView())) {
                RefreshBaseline(*watch, _due[i].virtualPath.ToStringView(), false);
                FileChange change{_due[i].virtualPath};
                watch->callback(change);
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
        PruneBaseline();
    }

    Vector<std::filesystem::path> FileWatcher::LayerFiles(const Watch& watch, StringView virtualPath) {
        Vector<std::filesystem::path> files;
        std::filesystem::path relative;
        if (watch.folder) {
            const size_t prefix = watch.virtualPath.size() + 1;  // the folder and its slash
            if (virtualPath.size() <= prefix) {
                return files;
            }
            relative = ToStd(virtualPath.substr(prefix));
        }
        for (const std::filesystem::path& target : watch.targets) {
            files.push_back(watch.folder ? (target / relative).lexically_normal() : target);
        }
        return files;
    }

    void FileWatcher::CollectFiles(const Watch& watch, FileStates& out) {
        for (const std::filesystem::path& target : watch.targets) {
            FileState state;
            if (!watch.folder) {
                if (ReadState(target, state.writeTime, state.size)) {
                    state.path = target;
                    out.insert_or_assign(PathKey(target), std::move(state));
                }
                continue;
            }
            std::error_code error;
            constexpr auto kOptions = std::filesystem::directory_options::skip_permission_denied;
            std::filesystem::recursive_directory_iterator it(target, kOptions, error);  //-NoStd: the walk of a requested rescan, no engine type lists a folder
            const std::filesystem::recursive_directory_iterator end;                    //-NoStd: the end marker of the listing above
            for (; !error && it != end; it.increment(error)) {
                state.path = it->path().lexically_normal();
                if (ReadState(state.path, state.writeTime, state.size)) {
                    out.insert_or_assign(PathKey(state.path), state);
                }
            }
        }
    }

    void FileWatcher::SnapshotWatch(const Watch& watch) {
        FileStates files;
        CollectFiles(watch, files);
        for (auto& [key, state] : files) {
            _baseline.insert({key, std::move(state)});  // a file another watch knew keeps its older state: a change not delivered yet stays pending
        }
    }

    bool FileWatcher::IsAppliedAndUnchanged(const Watch& watch, StringView virtualPath) const {
        bool applied = false;
        for (const std::filesystem::path& file : LayerFiles(watch, virtualPath)) {
            FileState now;
            const bool exists = ReadState(file, now.writeTime, now.size);
            const auto known = _baseline.find(PathKey(file));
            if (known == _baseline.end()) {
                if (exists) {
                    return false;
                }
                continue;
            }
            const FileState& before = known->second;
            if (!exists || !before.byApply || before.writeTime != now.writeTime || before.size != now.size) {
                return false;
            }
            applied = true;
        }
        return applied;
    }

    void FileWatcher::RefreshBaseline(const std::filesystem::path& realPath, bool byApply) {
        FileState state;
        state.byApply = byApply;
        const String key = PathKey(realPath);
        if (ReadState(realPath, state.writeTime, state.size)) {
            state.path = realPath;
            _baseline.insert_or_assign(key, std::move(state));
        } else {
            _baseline.erase(key);
        }
    }

    void FileWatcher::RefreshBaseline(const Watch& watch, StringView virtualPath, bool byApply) {
        for (const std::filesystem::path& file : LayerFiles(watch, virtualPath)) {
            RefreshBaseline(file, byApply);
        }
    }

    void FileWatcher::PruneBaseline() {
        for (auto it = _baseline.begin(); it != _baseline.end();) {
            bool covered = false;
            for (const UniquePtr<Watch>& watch : _watches) {
                if (Covers(*watch, it->second.path)) {
                    covered = true;
                    break;
                }
            }
            it = covered ? ++it : _baseline.erase(it);
        }
    }

    Vector<FileWatcher::Detected> FileWatcher::Detect() const {
        FileStates current;
        for (const UniquePtr<Watch>& watch : _watches) {
            if (watch->alive) {
                CollectFiles(*watch, current);
            }
        }
        Vector<std::filesystem::path> differing;
        for (const auto& [key, state] : current) {
            const auto known = _baseline.find(key);
            if (known == _baseline.end() || known->second.writeTime != state.writeTime || known->second.size != state.size) {
                differing.push_back(state.path);
            }
        }
        for (const auto& [key, state] : _baseline) {
            if (current.find(key) == current.end()) {
                differing.push_back(state.path);
            }
        }

        Vector<Detected> detected;
        for (const std::filesystem::path& path : differing) {
            for (const UniquePtr<Watch>& watch : _watches) {
                PoolString virtualPath;
                if (!watch->alive || !Matches(*watch, path, virtualPath)) {
                    continue;
                }
                Detected* entry = nullptr;
                for (Detected& candidate : detected) {
                    if (candidate.virtualPath == virtualPath) {
                        entry = &candidate;
                        break;
                    }
                }
                if (entry == nullptr) {
                    entry = &detected.emplace_back();
                    entry->virtualPath = virtualPath;
                    // The layers of one virtual path are one file for the consumer: it was there before when any layer had it, is there now when any has it
                    bool was = false;
                    bool now = false;
                    for (const std::filesystem::path& file : LayerFiles(*watch, virtualPath.ToStringView())) {
                        const String key = PathKey(file);
                        was = was || _baseline.find(key) != _baseline.end();
                        now = now || current.find(key) != current.end();
                    }
                    entry->kind = was && now ? FileChangeKind::Changed : (now ? FileChangeKind::Added : FileChangeKind::Removed);
                }
                // Two layers of one watch both differ: the watch is told once
                if (eastl::find(entry->watchIds.begin(), entry->watchIds.end(), watch->id) == entry->watchIds.end()) {
                    entry->watchIds.push_back(watch->id);
                }
                if (eastl::find(entry->consumers.begin(), entry->consumers.end(), watch->consumer) == entry->consumers.end()) {
                    entry->consumers.push_back(watch->consumer);
                }
            }
        }
        eastl::sort(detected.begin(), detected.end(), [](const Detected& a, const Detected& b) { return a.virtualPath.ToStringView() < b.virtualPath.ToStringView(); });
        return detected;
    }

    Vector<PendingFileChange> FileWatcher::Rescan() {
        Vector<PendingFileChange> pending;
        if (_backend == nullptr) {
            return pending;
        }
        for (Detected& entry : Detect()) {
            pending.push_back({entry.virtualPath, entry.kind, std::move(entry.consumers)});
        }
        return pending;
    }

    Vector<AppliedFileChange> FileWatcher::Apply() {
        if (_backend == nullptr || _delivering) {
            return {};
        }
        return ApplyDetected(Detect());
    }

    Vector<AppliedFileChange> FileWatcher::Apply(Span<const StringView> virtualPaths) {
        if (_backend == nullptr || _delivering) {
            return {};
        }
        Vector<Detected> detected = Detect();
        detected.erase(
            eastl::remove_if(detected.begin(), detected.end(),
                             [virtualPaths](const Detected& entry) { return eastl::find(virtualPaths.begin(), virtualPaths.end(), entry.virtualPath.ToStringView()) == virtualPaths.end(); }),
            detected.end());
        return ApplyDetected(detected);
    }

    Vector<AppliedFileChange> FileWatcher::ApplyDetected(const Vector<Detected>& detected) {
        Vector<AppliedFileChange> applied;
        _delivering = true;
        for (const Detected& entry : detected) {
            AppliedFileChange& result = applied.emplace_back();
            result.virtualPath = entry.virtualPath;
            result.consumers = entry.consumers;
            for (const uint64_t id : entry.watchIds) {
                // Delivered now, so the stream's own notice of the same change must not deliver it again
                _pending.erase(eastl::remove_if(_pending.begin(), _pending.end(), [&](const Pending& pending) { return pending.watchId == id && pending.virtualPath == entry.virtualPath; }),
                               _pending.end());
                Watch* watch = FindWatch(id);
                if (watch == nullptr || !watch->alive) {
                    continue;
                }
                RefreshBaseline(*watch, entry.virtualPath.ToStringView(), true);
                FileChange change{entry.virtualPath};
                watch->callback(change);
                if (!change.ok && result.ok) {
                    result.ok = false;
                    result.error = std::move(change.error);
                }
            }
        }
        _delivering = false;
        SweepDeadWatches();
        return applied;
    }

}  // namespace BECore
