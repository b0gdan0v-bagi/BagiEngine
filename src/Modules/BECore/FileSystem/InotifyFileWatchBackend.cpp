#if defined(__linux__)

#include <BECore/File/File.h>
#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/FileSystem/PlatformFileWatchBackend.h>
#include <cerrno>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

namespace BECore {

    namespace {

        /// A rename-over save is a CLOSE_WRITE of the temp file and a MOVED_TO of the target; MOVED_FROM is a file that left the directory
        constexpr uint32_t kWatchMask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_CREATE | IN_DELETE | IN_MODIFY | IN_ONLYDIR;

        /// Room for a burst of events read in one call; what does not fit stays in the kernel queue for the next read
        constexpr size_t kEventBufferBytes = 16 * 1024;

        /// One inotify watch: the directory it is on, and the directory the facade added (the root of its tree)
        struct Watched {
            int wd = -1;
            Path dir;
            Path root;
            bool recursive = false;
        };

        /// Whether @p dir is @p top or lies below it
        bool IsAtOrBelow(const Path& dir, const Path& top) {
            return dir == top || !dir.Relative(top).Empty();
        }

        /**
         * One inotify descriptor and one reader thread for every watched directory. inotify is not recursive, so a recursive directory
         * gets a watch per subfolder, and a folder that appears later is watched when its CREATE arrives (and its files, which may have
         * been made before the watch, are posted). The thread starts at the first AddDirectory and is woken to stop by an eventfd.
         */
        class InotifyFileWatchBackend final : public IFileWatchBackend {
        public:
            ~InotifyFileWatchBackend() override {
                Shutdown();
            }

            void AddDirectory(const Path& realPath, bool recursive) override {
                RemoveDirectory(realPath);
                LockGuard lock(_mutex);
                if (!Start()) {
                    return;
                }
                AddTree(realPath, realPath, recursive);
            }

            void RemoveDirectory(const Path& realPath) override {
                LockGuard lock(_mutex);
                for (size_t i = _watched.size(); i-- > 0;) {
                    if (_watched[i].root != realPath) {
                        continue;
                    }
                    const int wd = _watched[i].wd;
                    _watched.erase(_watched.begin() + static_cast<ptrdiff_t>(i));
                    // Two roots that overlap share the watch of the inode they share
                    if (!HasWatch(wd)) {
                        inotify_rm_watch(_inotifyFd, wd);
                    }
                }
            }

            void SetSink(IFileWatchSink* sink) override {
                LockGuard lock(_mutex);
                _sink = sink;
            }

            void Shutdown() override {
                if (_thread.joinable()) {
                    const uint64_t one = 1;
                    // An eventfd takes exactly 8 bytes; the thread leaves its loop when it reads the descriptor as ready
                    if (write(_stopFd, &one, sizeof(one)) != static_cast<ssize_t>(sizeof(one))) {
                        LOG_ERROR("File watch: cannot wake the reader thread");
                    }
                    _thread.join();
                }
                LockGuard lock(_mutex);
                _watched.clear();
                if (_inotifyFd >= 0) {
                    close(_inotifyFd);
                    _inotifyFd = -1;
                }
                if (_stopFd >= 0) {
                    close(_stopFd);
                    _stopFd = -1;
                }
            }

        private:
            /// With _mutex held. False when the kernel refuses a descriptor
            bool Start() {
                if (_inotifyFd >= 0) {
                    return true;
                }
                _inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
                if (_inotifyFd < 0) {
                    LOG_ERROR("File watch: inotify_init1 failed: {}", std::strerror(errno));
                    return false;
                }
                _stopFd = eventfd(0, EFD_CLOEXEC);
                if (_stopFd < 0) {
                    LOG_ERROR("File watch: eventfd failed: {}", std::strerror(errno));
                    close(_inotifyFd);
                    _inotifyFd = -1;
                    return false;
                }
                _thread = Thread("FileWatch", [this] { ReadLoop(); });
                return true;
            }

            [[nodiscard]] bool HasWatch(int wd) const {
                for (const Watched& watched : _watched) {
                    if (watched.wd == wd) {
                        return true;
                    }
                }
                return false;
            }

            /// With _mutex held: a watch on @p dir and, when @p recursive, on every folder below it
            void AddTree(const Path& dir, const Path& root, bool recursive) {
                const String dirText(dir.View().data(), dir.View().size());
                const int wd = inotify_add_watch(_inotifyFd, dirText.c_str(), kWatchMask);
                if (wd < 0) {
                    LOG_WARNING("File watch: cannot watch '{}': {}", dir.View(), std::strerror(errno));
                    return;
                }
                _watched.push_back(Watched{wd, dir, root, recursive});
                if (!recursive) {
                    return;
                }
                // The subfolders are collected first: a watch added inside the walk would be a second open folder under the one being listed
                Vector<Path> subfolders;
                static_cast<void>(WalkDirectory(dir.View(), WalkMode::Folder, [&](StringView name, FileKind kind) {
                    const Path child = dir / name;
                    // A link to a folder is not followed: it may lead out of the tree or back into it. The walk reports a link with the kind of its target
                    if (kind == FileKind::Directory && !IsSymbolicLink(child.View())) {
                        subfolders.push_back(child);
                    }
                    return true;
                }));
                for (const Path& subfolder : subfolders) {
                    AddTree(subfolder, root, true);
                }
            }

            void Post(const Path& path) const {
                if (_sink != nullptr) {
                    _sink->Post(path);
                }
            }

            /// With _mutex held. A watch added here is pushed to _watched, so entries are copied before they are used
            void HandleEvent(const inotify_event& event) {
                if ((event.mask & IN_Q_OVERFLOW) != 0) {
                    // Events were lost: everything below every watched root may have changed
                    for (const Watched& watched : _watched) {
                        if (watched.dir == watched.root && _sink != nullptr) {
                            PostTree(*_sink, watched.root, watched.recursive);
                        }
                    }
                    return;
                }
                if ((event.mask & IN_IGNORED) != 0) {
                    // The kernel dropped the watch: its folder was removed (or the watch was, by RemoveDirectory, which erased the entry already)
                    for (size_t i = _watched.size(); i-- > 0;) {
                        if (_watched[i].wd == event.wd) {
                            _watched.erase(_watched.begin() + static_cast<ptrdiff_t>(i));
                        }
                    }
                    return;
                }
                if (event.len == 0) {
                    return;
                }
                const size_t count = _watched.size();
                Path movedAway;
                for (size_t i = 0; i < count; ++i) {
                    if (_watched[i].wd != event.wd) {
                        continue;
                    }
                    const Path changed = _watched[i].dir / StringView(event.name);
                    const Path root = _watched[i].root;
                    const bool recursive = _watched[i].recursive;
                    Post(changed);
                    const bool folderAppeared = (event.mask & IN_ISDIR) != 0 && (event.mask & (IN_CREATE | IN_MOVED_TO)) != 0;
                    if (recursive && folderAppeared) {
                        AddTree(changed, root, true);
                        // What is already in the folder was made before its watch existed
                        if (_sink != nullptr) {
                            PostTree(*_sink, changed, true);
                        }
                    }
                    if (recursive && (event.mask & IN_ISDIR) != 0 && (event.mask & IN_MOVED_FROM) != 0) {
                        movedAway = changed;
                    }
                }
                if (!movedAway.Empty()) {
                    DropTree(movedAway);
                }
            }

            /// With _mutex held: the watches of a folder that was moved out of the tree follow it to its new place, where they would post paths of the old one
            void DropTree(const Path& top) {
                for (size_t i = _watched.size(); i-- > 0;) {
                    if (IsAtOrBelow(_watched[i].dir, top)) {
                        inotify_rm_watch(_inotifyFd, _watched[i].wd);
                        _watched.erase(_watched.begin() + static_cast<ptrdiff_t>(i));
                    }
                }
            }

            void ReadEvents() {
                alignas(inotify_event) char buffer[kEventBufferBytes];
                for (;;) {
                    // The descriptor is non-blocking: EAGAIN (nothing more) and any other failure return to the poll
                    const ssize_t bytes = read(_inotifyFd, buffer, sizeof(buffer));
                    if (bytes <= 0) {
                        return;
                    }
                    LockGuard lock(_mutex);
                    for (ssize_t offset = 0; offset < bytes;) {
                        const inotify_event* event = reinterpret_cast<const inotify_event*>(buffer + offset);
                        HandleEvent(*event);
                        offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
                    }
                }
            }

            /// The reader thread; the descriptors outlive it because Shutdown closes them after the join
            void ReadLoop() {
                pollfd fds[2] = {{_inotifyFd, POLLIN, 0}, {_stopFd, POLLIN, 0}};
                for (;;) {
                    const int ready = poll(fds, 2, -1);
                    if (ready < 0) {
                        if (errno == EINTR) {
                            continue;
                        }
                        LOG_ERROR("File watch: poll failed: {}", std::strerror(errno));
                        return;
                    }
                    if (fds[1].revents != 0) {
                        return;
                    }
                    if ((fds[0].revents & POLLIN) != 0) {
                        ReadEvents();
                    }
                }
            }

            Mutex _mutex{BE_LOCK_SITE("Inotify file watch")};
            IFileWatchSink* _sink = nullptr;
            Vector<Watched> _watched;
            int _inotifyFd = -1;
            int _stopFd = -1;
            Thread _thread;
        };

    }  // namespace

    IntrusivePtr<IFileWatchBackend> MakeInotifyFileWatchBackend() {
        return New<InotifyFileWatchBackend>();
    }

}  // namespace BECore

#endif
