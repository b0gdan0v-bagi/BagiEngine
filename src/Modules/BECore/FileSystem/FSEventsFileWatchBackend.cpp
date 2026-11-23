#if defined(__APPLE__)

#include <BECore/File/File.h>
#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/FileSystem/PlatformFileWatchBackend.h>
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

namespace BECore {

    namespace {

        /// How long FSEvents may hold an event to merge its neighbours; the facade settles a save anyway, so this only bounds the latency
        constexpr CFTimeInterval kStreamLatencySeconds = 0.05;

        constexpr FSEventStreamCreateFlags kStreamFlags = kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer;

        constexpr FSEventStreamEventFlags kLostEventsFlags = kFSEventStreamEventFlagMustScanSubDirs | kFSEventStreamEventFlagUserDropped | kFSEventStreamEventFlagKernelDropped;

        /// How long a failed stream waits before the next try
        constexpr double kStreamRetrySeconds = 1.0;

        void DoNothing(void* /*context*/) {}

        /// Whether @p path is @p root or lies below it; both lexically normal
        bool IsUnder(const Path& path, const Path& root) {
            return path == root || !path.Relative(root).Empty();
        }

        /// A watched directory in the two forms it has: the one the facade gave and the one FSEvents reports
        struct Watch {
            Path reported;
            Path real;
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
        void PostSubtree(IFileWatchSink& sink, const Watch& watch, const Path& realDir) {
            if (!watch.recursive && realDir != watch.real) {
                return;
            }
            // What lies below the watched directory, to be appended to the form the facade added
            const Path below = realDir.Relative(watch.real);
            const Path top = below.Empty() ? watch.reported : watch.reported / below;
            static_cast<void>(WalkDirectory(realDir.View(), watch.recursive ? WalkMode::Tree : WalkMode::Folder, [&](StringView relative, FileKind /*kind*/) {
                sink.Post((top / relative).LexicallyNormal());
                return true;
            }));
        }

        /**
         * One FSEventStream for the whole set of watched directories, delivered on a serial dispatch queue of the system: no thread of ours.
         * The stream is rebuilt when the set changes. FSEvents always watches the subtree, so a non-recursive directory filters the deeper paths out.
         *
         * fseventsd may drop events without a word when the machine makes many file events; nothing here polls for them (a poll would stat every
         * file in the frame). FileWatcher::Rescan finds what was missed, on request. A stream that failed to start is tried again from Tick.
         */
        class FSEventsFileWatchBackend final : public IFileWatchBackend {
        public:
            ~FSEventsFileWatchBackend() override {
                Shutdown();
            }

            void AddDirectory(const Path& realPath, bool recursive) override {
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
                std::expected<String, String> canonical = CanonicalPath(realPath.View());
                known->real = canonical ? Path(eastl::move(*canonical)) : realPath.LexicallyNormal();
                known->recursive = recursive;
                Rebuild();
            }

            void RemoveDirectory(const Path& realPath) override {
                for (size_t i = 0; i < _watches.size(); ++i) {
                    if (_watches[i].reported == realPath) {
                        _watches.erase(_watches.begin() + static_cast<ptrdiff_t>(i));
                        Rebuild();
                        return;
                    }
                }
            }

            void SetSink(IFileWatchSink* sink) override {
                _sink = sink;
            }

            void Tick(double nowSeconds) override {
                if (_streamFailed && nowSeconds >= _nextStreamRetryAt) {
                    _nextStreamRetryAt = nowSeconds + kStreamRetrySeconds;
                    Rebuild();
                }
            }

            void Shutdown() override {
                _watches.clear();
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
                _streamFailed = !_watches.empty() && _stream.ref == nullptr;
                if (!_streamFailed) {
                    _failureLogged = false;
                }
            }

            /// A stream that cannot start is tried again every kStreamRetrySeconds: the first failure is logged, the next ones only after a success in between
            void LogFailureOnce(const char* message) {
                if (!_failureLogged) {
                    _failureLogged = true;
                    LOG_ERROR(message);
                }
            }

            Stream CreateStream() {
                Stream stream;
                stream.snapshot = MakeUnique<Snapshot>();
                stream.snapshot->sink = _sink;
                stream.snapshot->watches = _watches;

                CFMutableArrayRef paths = CFArrayCreateMutable(kCFAllocatorDefault, static_cast<CFIndex>(_watches.size()), &kCFTypeArrayCallBacks);
                for (const Watch& watch : _watches) {
                    // A Path has no terminating zero of its own to hand out
                    const String realText(watch.real.View().data(), watch.real.View().size());
                    CFStringRef path = CFStringCreateWithFileSystemRepresentation(kCFAllocatorDefault, realText.c_str());
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
                    LogFailureOnce("File watch: FSEventStreamCreate failed");
                    return {};
                }

                if (_queue == nullptr) {
                    _queue = dispatch_queue_create("BagiEngine.FileWatch", DISPATCH_QUEUE_SERIAL);
                }
                FSEventStreamSetDispatchQueue(stream.ref, _queue);
                if (!FSEventStreamStart(stream.ref)) {
                    LogFailureOnce("File watch: FSEventStreamStart failed");
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
                    const Path path = Path(StringView(paths[i])).LexicallyNormal();
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
                        const Path relative = path.Relative(watch.real);
                        if (relative.Empty()) {
                            continue;
                        }
                        if (!watch.recursive && relative.View().find('/') != StringView::npos) {
                            continue;
                        }
                        snapshot.sink->Post((watch.reported / relative).LexicallyNormal());
                    }
                }
            }

            IFileWatchSink* _sink = nullptr;
            double _nextStreamRetryAt = 0.0;
            bool _streamFailed = false;
            bool _failureLogged = false;
            Vector<Watch> _watches;
            Stream _stream;
            dispatch_queue_t _queue = nullptr;
        };

    }  // namespace

    IntrusivePtr<IFileWatchBackend> MakeFSEventsFileWatchBackend() {
        return New<FSEventsFileWatchBackend>();
    }

}  // namespace BECore

#endif
