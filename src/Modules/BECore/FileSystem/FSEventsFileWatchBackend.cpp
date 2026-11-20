#if defined(__APPLE__)

#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/FileSystem/PlatformFileWatchBackend.h>
#include <BECore/FileSystem/PollingFileWatchBackend.h>
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <iterator>
#include <system_error>

namespace BECore {

    namespace {

        /// How long FSEvents may hold an event to merge its neighbours; the facade settles a save anyway, so this only bounds the latency
        constexpr CFTimeInterval kStreamLatencySeconds = 0.05;

        constexpr FSEventStreamCreateFlags kStreamFlags = kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer;

        constexpr FSEventStreamEventFlags kLostEventsFlags = kFSEventStreamEventFlagMustScanSubDirs | kFSEventStreamEventFlagUserDropped | kFSEventStreamEventFlagKernelDropped;

        /// How often the safety poll looks; the stream is the fast path, the poll only catches what fseventsd never delivers
        constexpr double kSafetyPollSeconds = 2.0;

        /// How long a failed stream waits before the next try
        constexpr double kStreamRetrySeconds = 1.0;

        void DoNothing(void* /*context*/) {}

        String PathKey(const std::filesystem::path& path) {
            return String(path.lexically_normal().c_str());
        }

        /**
         * Joins the two sources of one directory set: the stream (dispatch queue) and the safety poll (main thread). A path the stream
         * already reported is dropped when the poll finds the same change, so one save is one post; the set is forgotten after every poll
         * because the poll that follows a change is the first that can see it.
         */
        class SeenFilter {
        public:
            void SetTarget(IFileWatchSink* target) {
                LockGuard lock(_mutex);
                _target = target;
            }

            void PostFromStream(const std::filesystem::path& path) {
                LockGuard lock(_mutex);
                _seen.insert(PathKey(path));
                if (_target != nullptr) {
                    _target->Post(path);
                }
            }

            void PostFromPoll(const std::filesystem::path& path) {
                LockGuard lock(_mutex);
                if (_seen.erase(PathKey(path)) == 0 && _target != nullptr) {
                    _target->Post(path);
                }
            }

            void Forget() {
                LockGuard lock(_mutex);
                _seen.clear();
            }

        private:
            Mutex _mutex{BE_LOCK_SITE("FSEvents seen paths")};
            IFileWatchSink* _target = nullptr;
            HashSet<String> _seen;
        };

        class StreamSink final : public IFileWatchSink {
        public:
            explicit StreamSink(SeenFilter& filter) : _filter(filter) {}

            void Post(const std::filesystem::path& realPath) override {
                _filter.PostFromStream(realPath);
            }

        private:
            SeenFilter& _filter;
        };

        class PollSink final : public IFileWatchSink {
        public:
            explicit PollSink(SeenFilter& filter) : _filter(filter) {}

            void Post(const std::filesystem::path& realPath) override {
                _filter.PostFromPoll(realPath);
            }

        private:
            SeenFilter& _filter;
        };

        /// Whether @p path is @p root or lies below it
        bool IsUnder(const std::filesystem::path& path, const std::filesystem::path& root) {
            auto pathPart = path.begin();
            for (auto rootPart = root.begin(); rootPart != root.end(); ++rootPart, ++pathPart) {
                if (pathPart == path.end() || *pathPart != *rootPart) {
                    return false;
                }
            }
            return true;
        }

        /// A watched directory in the two forms it has: the one the facade gave and the one FSEvents reports
        struct Watch {
            std::filesystem::path reported;
            std::filesystem::path real;
            bool recursive = false;
        };

        /// What a stream callback reads. Immutable once its stream exists, so the dispatch queue and the main thread share nothing mutable
        struct Snapshot {
            IFileWatchSink* sink = nullptr;
            Vector<Watch> watches;
        };

        struct Stream {
            FSEventStreamRef ref = nullptr;
            UniquePtr<Snapshot> snapshot;
        };

        /// Posts every file below @p realDir (a folder of @p watch) in the form the facade added, as many as the watch reaches
        void PostSubtree(IFileWatchSink& sink, const Watch& watch, const std::filesystem::path& realDir) {
            if (!watch.recursive && realDir != watch.real) {
                return;
            }
            std::error_code error;
            std::filesystem::recursive_directory_iterator it(realDir, error);  //-NoStd: the rare rescan after lost events, no engine type walks a folder
            const std::filesystem::recursive_directory_iterator end;           //-NoStd: same
            for (; !error && it != end; it.increment(error)) {
                sink.Post((watch.reported / it->path().lexically_relative(watch.real)).lexically_normal());
                if (!watch.recursive) {
                    it.disable_recursion_pending();
                }
            }
        }

        /**
         * One FSEventStream for the whole set of watched directories, delivered on a serial dispatch queue of the system: no thread of ours.
         * The stream is rebuilt when the set changes. FSEvents always watches the subtree, so a non-recursive directory filters the deeper paths out.
         *
         * fseventsd drops events without a word when the machine makes many file events (no flag reaches the stream), and a stream can fail to
         * start, so the same directories are also watched by a slow PollingFileWatchBackend: the stream gives the latency, the poll the guarantee.
         */
        class FSEventsFileWatchBackend final : public IFileWatchBackend {
        public:
            FSEventsFileWatchBackend() {
                _poll.SetSink(&_pollSink);
            }

            ~FSEventsFileWatchBackend() override {
                Shutdown();
            }

            void AddDirectory(const std::filesystem::path& realPath, bool recursive) override {
                Watch* known = nullptr;
                for (Watch& watch : _watches) {
                    if (watch.reported == realPath) {
                        known = &watch;
                    }
                }
                if (known == nullptr) {
                    known = &_watches.emplace_back();
                    known->reported = realPath;
                }
                // FSEvents names a file by its real path (/private/var for /var), so what it says is matched against the real form of the directory
                // and told to the facade in the form it was given
                std::error_code error;
                known->real = std::filesystem::canonical(realPath, error);
                if (error) {
                    known->real = realPath.lexically_normal();
                }
                known->recursive = recursive;
                _poll.AddDirectory(realPath, recursive);
                Rebuild();
            }

            void RemoveDirectory(const std::filesystem::path& realPath) override {
                for (size_t i = 0; i < _watches.size(); ++i) {
                    if (_watches[i].reported == realPath) {
                        _watches.erase(_watches.begin() + static_cast<ptrdiff_t>(i));
                        _poll.RemoveDirectory(realPath);
                        Rebuild();
                        return;
                    }
                }
            }

            void SetSink(IFileWatchSink* sink) override {
                _filter.SetTarget(sink);
            }

            void Tick(double nowSeconds) override {
                if (_streamFailed && nowSeconds >= _nextStreamRetryAt) {
                    _nextStreamRetryAt = nowSeconds + kStreamRetrySeconds;
                    Rebuild();
                }
                if (nowSeconds < _nextSafetyPollAt) {
                    return;
                }
                _nextSafetyPollAt = nowSeconds + kSafetyPollSeconds;
                _poll.Tick(nowSeconds);
                _filter.Forget();
            }

            void Shutdown() override {
                _watches.clear();
                _poll.Shutdown();
                Rebuild();
                if (_queue != nullptr) {
                    dispatch_release(_queue);
                    _queue = nullptr;
                }
            }

        private:
            void Rebuild() {
                Stream next;
                if (!_watches.empty()) {
                    next = CreateStream();
                }
                // The new stream runs before the old one stops, so a change made while the set is rebuilt is seen by one of them (the facade coalesces a double)
                if (_stream.ref != nullptr) {
                    DestroyStream(_stream);
                }
                _stream = eastl::move(next);
                // A stream that did not start leaves its directories to the poll until a later try succeeds
                _streamFailed = !_watches.empty() && _stream.ref == nullptr;
            }

            Stream CreateStream() {
                Stream stream;
                stream.snapshot = MakeUnique<Snapshot>();
                stream.snapshot->sink = &_streamSink;
                stream.snapshot->watches = _watches;

                CFMutableArrayRef paths = CFArrayCreateMutable(kCFAllocatorDefault, static_cast<CFIndex>(_watches.size()), &kCFTypeArrayCallBacks);
                for (const Watch& watch : _watches) {
                    CFStringRef path = CFStringCreateWithFileSystemRepresentation(kCFAllocatorDefault, watch.real.c_str());
                    if (path != nullptr) {
                        CFArrayAppendValue(paths, path);
                        CFRelease(path);
                    }
                }

                FSEventStreamContext context{};
                context.info = stream.snapshot.get();
                // Events since the id of this moment are replayed, so a change between here and the registration with fseventsd is not lost
                stream.ref = FSEventStreamCreate(kCFAllocatorDefault, &OnEvents, &context, paths, FSEventsGetCurrentEventId(), kStreamLatencySeconds, kStreamFlags);
                CFRelease(paths);
                if (stream.ref == nullptr) {
                    LOG_ERROR("File watch: FSEventStreamCreate failed");
                    return {};
                }

                if (_queue == nullptr) {
                    _queue = dispatch_queue_create("BagiEngine.FileWatch", DISPATCH_QUEUE_SERIAL);
                }
                FSEventStreamSetDispatchQueue(stream.ref, _queue);
                if (!FSEventStreamStart(stream.ref)) {
                    LOG_ERROR("File watch: FSEventStreamStart failed");
                    FSEventStreamInvalidate(stream.ref);
                    FSEventStreamRelease(stream.ref);
                    return {};
                }
                // Returns once fseventsd has registered the stream: without it a write right after AddDirectory was lost on a loaded machine
                FSEventStreamFlushSync(stream.ref);
                return stream;
            }

            void DestroyStream(Stream& stream) {
                FSEventStreamStop(stream.ref);
                FSEventStreamInvalidate(stream.ref);
                // A callback may be queued already: a call on the serial queue returns after it ran, and only then may the snapshot it reads go
                dispatch_sync_f(_queue, nullptr, &DoNothing);
                FSEventStreamRelease(stream.ref);
                stream.ref = nullptr;
                stream.snapshot.reset();
            }

            /// On the dispatch queue
            static void OnEvents(ConstFSEventStreamRef /*stream*/, void* info, size_t count, void* eventPaths, const FSEventStreamEventFlags* flags, const FSEventStreamEventId* /*ids*/) {
                const Snapshot& snapshot = *static_cast<const Snapshot*>(info);
                if (snapshot.sink == nullptr) {
                    return;
                }
                const char* const* paths = static_cast<const char* const*>(eventPaths);
                for (size_t i = 0; i < count; ++i) {
                    const std::filesystem::path path = std::filesystem::path(paths[i]).lexically_normal();
                    const bool lostEvents = (flags[i] & kLostEventsFlags) != 0;
                    for (const Watch& watch : snapshot.watches) {
                        if (!IsUnder(path, watch.real)) {
                            continue;
                        }
                        if (lostEvents) {
                            // The events of this subtree are gone: everything in it may have changed
                            PostSubtree(*snapshot.sink, watch, path);
                            continue;
                        }
                        // The directory itself changes whenever something in it does and says nothing the event of the file did not
                        const std::filesystem::path relative = path.lexically_relative(watch.real);
                        if (relative.empty() || relative == ".") {
                            continue;
                        }
                        if (!watch.recursive && std::distance(relative.begin(), relative.end()) > 1) {
                            continue;
                        }
                        snapshot.sink->Post((watch.reported / relative).lexically_normal());
                    }
                }
            }

            SeenFilter _filter;
            StreamSink _streamSink{_filter};
            PollSink _pollSink{_filter};
            PollingFileWatchBackend _poll;
            double _nextSafetyPollAt = 0.0;
            double _nextStreamRetryAt = 0.0;
            bool _streamFailed = false;
            Vector<Watch> _watches;
            Stream _stream;
            dispatch_queue_t _queue = nullptr;
        };

    }  // namespace

    IntrusivePtrAtomic<IFileWatchBackend> MakeFSEventsFileWatchBackend() {
        return New<FSEventsFileWatchBackend>();
    }

}  // namespace BECore

#endif
