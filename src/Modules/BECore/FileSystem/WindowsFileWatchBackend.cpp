#if defined(_WIN32)

#include <BECore/File/File.h>
#include <BECore/FileSystem/IFileWatchBackend.h>
#include <BECore/FileSystem/PlatformFileWatchBackend.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace BECore {

    namespace {

        /// ReadDirectoryChangesW refuses a larger buffer on a network share; a burst that does not fit comes back as a completion of 0 bytes
        constexpr DWORD kBufferBytes = 64 * 1024;

        constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;

        /// Room for the UTF-16 form of a path without the heap; a longer one moves there
        constexpr size_t kInlineWideChars = 260;

        /// The UTF-16 text of a UTF-8 path with the terminating zero that CreateFileW needs
        SmallVector<wchar_t, kInlineWideChars> ToWide(StringView path) {
            SmallVector<wchar_t, kInlineWideChars> wide;
            const int length = path.empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0);
            wide.resize(static_cast<size_t>(length) + 1, L'\0');
            if (length > 0) {
                MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), wide.data(), length);
            }
            return wide;
        }

        /// One watched directory. The completion key of its handle is the address of this object, so it must stay at its address and
        /// must not be freed while an operation on it is pending (RemoveDirectory waits for the completion)
        struct Dir {
            Path root;
            bool recursive = false;
            HANDLE handle = INVALID_HANDLE_VALUE;
            OVERLAPPED overlapped{};
            bool pending = false;
            bool closing = false;
            alignas(DWORD) char buffer[kBufferBytes]{};
        };

        /**
         * One I/O completion port and one reader thread for every watched directory: each directory has one ReadDirectoryChangesW pending,
         * the thread takes the completion, posts the names in the buffer and issues the next read. A rename-over save arrives as a pair of
         * names (old and new), both posted. A stop is a packet with no operation posted to the port, so no handle is waited on and then
         * closed behind the thread's back. The thread starts at the first AddDirectory.
         */
        class WindowsFileWatchBackend final : public IFileWatchBackend {
        public:
            ~WindowsFileWatchBackend() override {
                Shutdown();
            }

            void AddDirectory(const Path& realPath, bool recursive) override {
                RemoveDirectory(realPath);
                LockGuard lock(_mutex);
                if (!Start()) {
                    return;
                }
                UniquePtr<Dir> dir = MakeUnique<Dir>();
                dir->root = realPath;
                dir->recursive = recursive;
                dir->handle = CreateFileW(ToWide(realPath.View()).data(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
                if (dir->handle == INVALID_HANDLE_VALUE) {
                    LOG_WARNING("File watch: cannot open a directory, error {}", static_cast<uint32_t>(GetLastError()));
                    return;
                }
                if (CreateIoCompletionPort(dir->handle, _port, reinterpret_cast<ULONG_PTR>(dir.get()), 0) == nullptr || !Issue(*dir)) {
                    LOG_WARNING("File watch: cannot watch a directory, error {}", static_cast<uint32_t>(GetLastError()));
                    CloseHandle(dir->handle);
                    return;
                }
                _dirs.push_back(eastl::move(dir));
            }

            void RemoveDirectory(const Path& realPath) override {
                UniqueLock lock(_mutex);
                for (size_t i = 0; i < _dirs.size(); ++i) {
                    Dir& dir = *_dirs[i];
                    if (dir.root != realPath) {
                        continue;
                    }
                    dir.closing = true;
                    if (dir.pending) {
                        CancelIoEx(dir.handle, &dir.overlapped);
                        // Every issued operation completes once, cancelled or not; the thread clears `pending` and the object may go after that
                        _completed.wait(lock, [&dir] { return !dir.pending; });
                    }
                    if (dir.handle != INVALID_HANDLE_VALUE) {
                        CloseHandle(dir.handle);
                    }
                    _dirs.erase(_dirs.begin() + static_cast<ptrdiff_t>(i));
                    return;
                }
            }

            void SetSink(IFileWatchSink* sink) override {
                LockGuard lock(_mutex);
                _sink = sink;
            }

            void Shutdown() override {
                if (_thread.joinable()) {
                    PostQueuedCompletionStatus(_port, 0, 0, nullptr);
                    _thread.join();
                }
                LockGuard lock(_mutex);
                for (UniquePtr<Dir>& dir : _dirs) {
                    if (dir->handle == INVALID_HANDLE_VALUE) {
                        continue;
                    }
                    if (dir->pending) {
                        CancelIoEx(dir->handle, &dir->overlapped);
                        // The kernel writes into the buffer until the operation is over, so the buffer may be freed only after this returns
                        DWORD ignored = 0;
                        GetOverlappedResult(dir->handle, &dir->overlapped, &ignored, TRUE);
                    }
                    CloseHandle(dir->handle);
                }
                _dirs.clear();
                if (_port != nullptr) {
                    CloseHandle(_port);
                    _port = nullptr;
                }
            }

        private:
            /// With _mutex held. False when there is no port
            bool Start() {
                if (_port != nullptr) {
                    return true;
                }
                _port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
                if (_port == nullptr) {
                    LOG_ERROR("File watch: CreateIoCompletionPort failed, error {}", static_cast<uint32_t>(GetLastError()));
                    return false;
                }
                _thread = Thread("FileWatch", [this] { ReadLoop(); });
                return true;
            }

            /// With _mutex held: starts the next read of @p dir. Its OVERLAPPED is cleared first, a finished one must not be reused as it is
            bool Issue(Dir& dir) {
                dir.overlapped = OVERLAPPED{};
                dir.pending = ReadDirectoryChangesW(dir.handle, dir.buffer, kBufferBytes, dir.recursive ? TRUE : FALSE, kNotifyFilter, nullptr, &dir.overlapped, nullptr) != FALSE;
                return dir.pending;
            }

            void Post(const Path& path) const {
                if (_sink != nullptr) {
                    _sink->Post(path);
                }
            }

            /// With _mutex held: the FILE_NOTIFY_INFORMATION records the finished read left in the buffer
            void PostNames(const Dir& dir, DWORD bytes) const {
                DWORD offset = 0;
                while (offset < bytes) {
                    const FILE_NOTIFY_INFORMATION* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(dir.buffer + offset);
                    const WCHAR* name = info->FileName;
                    Post(dir.root / WideToUtf8(name, info->FileNameLength / sizeof(WCHAR)));
                    if (info->NextEntryOffset == 0) {
                        return;
                    }
                    offset += info->NextEntryOffset;
                }
            }

            /// A finished read of @p dir; @p error is 0 when it succeeded
            void Complete(Dir& dir, DWORD error, DWORD bytes) {
                LockGuard lock(_mutex);
                dir.pending = false;
                if (!dir.closing) {
                    if (error == 0 && bytes > 0) {
                        PostNames(dir, bytes);
                        Issue(dir);
                    } else if (error == 0 || error == ERROR_NOTIFY_ENUM_DIR) {
                        // No bytes (or the same said by the error) is a burst that did not fit: everything below may have changed
                        if (_sink != nullptr) {
                            PostTree(*_sink, dir.root, dir.recursive);
                        }
                        Issue(dir);
                    } else {
                        // The directory was removed or its volume went. Closing the handle lets a pending delete finish
                        LOG_DEBUG("File watch: a watch ended, error {}", static_cast<uint32_t>(error));
                        CloseHandle(dir.handle);
                        dir.handle = INVALID_HANDLE_VALUE;
                    }
                }
                _completed.notify_all();
            }

            /// The reader thread. A completion with no operation is the stop packet (or a port that failed): either way the loop ends
            void ReadLoop() {
                for (;;) {
                    DWORD bytes = 0;
                    ULONG_PTR key = 0;
                    OVERLAPPED* overlapped = nullptr;
                    const BOOL succeeded = GetQueuedCompletionStatus(_port, &bytes, &key, &overlapped, INFINITE);
                    if (overlapped == nullptr) {
                        return;
                    }
                    Complete(*reinterpret_cast<Dir*>(key), succeeded != FALSE ? 0 : GetLastError(), bytes);
                }
            }

            Mutex _mutex{BE_LOCK_SITE("Windows file watch")};
            ConditionVariable _completed;
            IFileWatchSink* _sink = nullptr;
            Vector<UniquePtr<Dir>> _dirs;
            HANDLE _port = nullptr;
            Thread _thread;
        };

    }  // namespace

    IntrusivePtr<IFileWatchBackend> MakeWindowsFileWatchBackend() {
        return New<WindowsFileWatchBackend>();
    }

}  // namespace BECore

#endif
