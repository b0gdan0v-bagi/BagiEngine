#include "File.h"

#include <BECore/File/Path.h>
#include <BECore/Memory/LinearArena.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstdlib>
#include <sys/stat.h>
#endif

namespace BECore {

    namespace {

        /// Bytes of a path that CPath keeps inside the object; a longer path moves to the heap
        constexpr size_t kInlinePathBytes = 512;

        /// A path with a terminating zero, which SDL needs and a StringView does not have
        class CPath {
        public:
            explicit CPath(StringView path) {
                _chars.assign(path.begin(), path.end());
                _chars.push_back('\0');
            }

            const char* Get() const {
                return _chars.data();
            }

        private:
            SmallVector<char, kInlinePathBytes + 1> _chars;
        };

        const char* ModeText(FileMode mode) {
            switch (mode) {
                case FileMode::Read:
                    return "rb";
                case FileMode::Append:
                    return "ab";
                case FileMode::Write:
                case FileMode::CreateNew:
                    return "wb";
            }
            return "rb";
        }

        /// SDL's error of the call that just failed, which is thread-local and overwritten by the next SDL call
        String Failure(const char* action, StringView path) {
            return Format("Cannot {} '{}': {}", action, path, SDL_GetError());
        }

        String Failure(const char* action) {
            return Format("Cannot {}: {}", action, SDL_GetError());
        }

        /// An open file and the size to read from it, once the size passed the limit
        struct WholeFile {
            File file;
            size_t size = 0;
        };

        std::expected<WholeFile, String> OpenForWholeRead(StringView path, size_t maxBytes) {
            auto file = File::Open(path, FileMode::Read);
            if (!file) {
                return std::unexpected(std::move(file.error()));
            }
            const auto size = file->Size();
            if (!size) {
                return std::unexpected(Format("Cannot read '{}': {}", path, size.error()));
            }
            if (*size > maxBytes) {
                return std::unexpected(Format("Cannot read '{}': it is {} bytes and the limit is {}", path, *size, maxBytes));
            }
            return WholeFile{std::move(*file), static_cast<size_t>(*size)};
        }

        /// A short read of a file whose size was just taken means it shrank or the medium failed: the caller must not see a half file
        std::expected<void, String> ReadToTheEnd(File& file, void* data, size_t size, StringView path) {
            if (size == 0) {
                return {};
            }
            const auto count = file.Read(data, size);
            if (!count) {
                return std::unexpected(Format("Cannot read '{}': {}", path, count.error()));
            }
            if (*count != size) {
                return std::unexpected(Format("Cannot read '{}' to the end", path));
            }
            return {};
        }

        /// The folder part of @p path without the separator; empty for a name with no folder or one directly under the root
        /// (POSIX "/name"; on Windows also "C:\name", whose folder part "C:" is a drive that SDL_CreateDirectory refuses)
        StringView FolderOf(StringView path) {
            const size_t separator = path.find_last_of("/\\");
            if (separator == StringView::npos) {
                return {};
            }
#ifdef _WIN32
            constexpr size_t kDriveLength = 2;
            if (separator == kDriveLength && path[1] == ':') {
                return {};
            }
#endif
            return path.substr(0, separator);
        }

        std::expected<void, String> WriteAndClose(File& file, const void* data, size_t size, StringView tempPath) {
            auto added = [&](String error) { return std::unexpected(Format("{} ('{}')", error, tempPath)); };
            if (size != 0) {
                if (auto written = file.Write(data, size); !written) {
                    return added(std::move(written.error()));
                }
            }
            if (auto flushed = file.Flush(); !flushed) {
                return added(std::move(flushed.error()));
            }
            if (auto closed = file.Close(); !closed) {
                return added(std::move(closed.error()));
            }
            return {};
        }

        std::expected<void, String> WriteAtomically(StringView path, const void* data, size_t size) {
            if (const StringView folder = FolderOf(path); !folder.empty()) {
                if (auto made = CreateDirectories(folder); !made) {
                    return made;
                }
            }

            const String tempPath = Format("{}{}", path, kAtomicTempSuffix);
            auto file = File::Open(tempPath, FileMode::Write);
            if (!file) {
                // Nothing was created, so nothing is removed: the temp name may be somebody else's folder
                return std::unexpected(std::move(file.error()));
            }

            // The temp file is closed before it is removed: Windows refuses to delete an open file
            auto written = WriteAndClose(*file, data, size, tempPath);
            *file = File{};
            if (!written) {
                static_cast<void>(RemoveFile(tempPath));
                return written;
            }

            const CPath cTemp{tempPath};
            const CPath cPath{path};
            if (!SDL_RenamePath(cTemp.Get(), cPath.Get())) {
                String error = Failure("replace", path);
                static_cast<void>(RemoveFile(tempPath));
                return std::unexpected(std::move(error));
            }
            return {};
        }

        /// The deepest folder WalkDirectory enters: a bound for a tree that loops without a symbolic link (a bind mount), and for the open
        /// folder handles, one per level
        constexpr size_t kMaxWalkDepth = 64;

        String ToString(StringView text) {
            return String(text.data(), text.size());
        }

#ifdef _WIN32
        /// A path as UTF-16 with a terminating zero, which the wide Windows calls need
        class WidePath {
        public:
            explicit WidePath(StringView path) {
                const int length = path.empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0);
                _chars.resize(static_cast<size_t>(length) + 1, L'\0');
                if (length > 0) {
                    MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), _chars.data(), length);
                }
            }

            const wchar_t* Get() const {
                return _chars.data();
            }

        private:
            SmallVector<wchar_t, kInlinePathBytes + 1> _chars;
        };

        String WindowsFailure(const char* action, StringView path) {
            return Format("Cannot {} '{}': Windows error {}", action, path, GetLastError());
        }
#else
        /// The failure of a POSIX call that SDL does not wrap, worded like SDL's own
        String ErrnoFailure(const char* action, StringView path) {
            return Format("Cannot {} '{}': {}", action, path, std::strerror(errno));
        }
#endif

        /// Whether @p path is itself a symbolic link (or a Windows reparse point such as a junction), which SDL_GetPathInfo cannot tell: it follows links
        bool IsLinkAt(const char* path) {
#ifdef _WIN32
            const DWORD attributes = GetFileAttributesW(WidePath(path).Get());
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
            struct stat info;
            return lstat(path, &info) == 0 && S_ISLNK(info.st_mode);
#endif
        }

        /// What tells two paths that name the same file apart from two that do not
        struct FileIdentity {
            uint64_t volume = 0;
            uint64_t index = 0;
            bool operator==(const FileIdentity&) const = default;
        };

        std::expected<FileIdentity, String> IdentityOf(StringView path) {
#ifdef _WIN32
            const HANDLE handle = CreateFileW(WidePath(path).Get(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                return std::unexpected(WindowsFailure("open", path));
            }
            BY_HANDLE_FILE_INFORMATION info;
            const bool known = GetFileInformationByHandle(handle, &info) != 0;
            const String failure = known ? String{} : WindowsFailure("read the identity of", path);
            CloseHandle(handle);
            if (!known) {
                return std::unexpected(failure);
            }
            constexpr unsigned kHighWordShift = 32;
            return FileIdentity{info.dwVolumeSerialNumber, (static_cast<uint64_t>(info.nFileIndexHigh) << kHighWordShift) | info.nFileIndexLow};
#else
            const CPath cPath{path};
            struct stat info;
            if (stat(cPath.Get(), &info) != 0) {
                return std::unexpected(ErrnoFailure("read the identity of", path));
            }
            return FileIdentity{static_cast<uint64_t>(info.st_dev), static_cast<uint64_t>(info.st_ino)};
#endif
        }

        /// The state of one WalkDirectory call; `path` is the folder being listed and always ends in a separator while SDL lists it
        struct Walker {
            WalkMode mode = WalkMode::Folder;
            FunctionRef<bool(StringView, FileKind)> visit;
            String path;
            size_t rootLength = 0;
            size_t depth = 0;
            bool stopped = false;
        };

        SDL_EnumerationResult SDLCALL WalkEntry(void* userdata, const char* /*folder*/, const char* name) {
            Walker& walker = *static_cast<Walker*>(userdata);
            const size_t folderLength = walker.path.size();
            walker.path.append(name);

            const Optional<FileInfo> info = GetFileInfo(walker.path);
            const FileKind kind = info ? info->kind : FileKind::Other;
            if (!walker.visit(StringView(walker.path.data() + walker.rootLength, walker.path.size() - walker.rootLength), kind)) {
                walker.stopped = true;
                return SDL_ENUM_SUCCESS;
            }

            if (walker.mode == WalkMode::Tree && kind == FileKind::Directory && walker.depth < kMaxWalkDepth && !IsLinkAt(walker.path.c_str())) {
                walker.path.push_back('/');
                ++walker.depth;
                // A subfolder that cannot be read is skipped: the result of the call is not looked at
                static_cast<void>(SDL_EnumerateDirectory(walker.path.c_str(), &WalkEntry, &walker));
                --walker.depth;
            }
            walker.path.resize(folderLength);
            return walker.stopped ? SDL_ENUM_SUCCESS : SDL_ENUM_CONTINUE;
        }

        SDL_EnumerationResult SDLCALL CollectName(void* userdata, const char* /*folder*/, const char* name) {
            static_cast<Vector<String>*>(userdata)->push_back(String(name));
            return SDL_ENUM_CONTINUE;
        }

        /// @p path as it stands, with the working folder in front when it is relative; not normal, so `link/..` still goes through the link
        std::expected<Path, String> JoinedWithCurrentDirectory(StringView path) {
            if (path.empty()) {
                return std::unexpected(String("Cannot make an empty path absolute"));
            }
            if (PathIsAbsolute(path)) {
                return Path(path);
            }
            auto current = CurrentDirectory();
            if (!current) {
                return std::unexpected(std::move(current.error()));
            }
            return Path(*current) / path;
        }

        std::expected<void, String> RemoveTreeAt(const String& path) {
            if (!IsLinkAt(path.c_str())) {
                const Optional<FileInfo> info = GetFileInfo(path);
                if (!info) {
                    return std::unexpected(Format("Cannot remove '{}': there is no such file", path));
                }
                if (info->kind == FileKind::Directory) {
                    // Listed first and removed after: the folder is not changed while SDL walks it
                    Vector<String> names;
                    if (!SDL_EnumerateDirectory(path.c_str(), &CollectName, &names)) {
                        return std::unexpected(Failure("list", path));
                    }
                    for (const String& name : names) {
                        if (auto removed = RemoveTreeAt(Format("{}/{}", path, name)); !removed) {
                            return removed;
                        }
                    }
                }
            }
            if (!SDL_RemovePath(path.c_str())) {
                return std::unexpected(Failure("remove", path));
            }
            return {};
        }

    }  // namespace

#ifdef _WIN32
    String WideToUtf8(const wchar_t* text, size_t length) {
        if (length == 0) {
            return {};
        }
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
        String result(static_cast<size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), result.data(), bytes, nullptr, nullptr);
        return result;
    }
#endif

    bool IsSymbolicLink(StringView path) {
        return IsLinkAt(CPath{path}.Get());
    }

    File::~File() {
        if (_stream != nullptr) {
            SDL_CloseIO(_stream);
        }
    }

    File::File(File&& other) noexcept : _stream(std::exchange(other._stream, nullptr)) {}

    File& File::operator=(File&& other) noexcept {
        if (this != &other) {
            if (_stream != nullptr) {
                SDL_CloseIO(_stream);
            }
            _stream = std::exchange(other._stream, nullptr);
        }
        return *this;
    }

    std::expected<File, String> File::Open(StringView path, FileMode mode) {
        if (mode == FileMode::CreateNew && GetFileInfo(path).has_value()) {
            return std::unexpected(Format("Cannot create '{}': the file exists", path));
        }
        const CPath cPath{path};
        SDL_IOStream* stream = SDL_IOFromFile(cPath.Get(), ModeText(mode));
        if (stream == nullptr) {
            return std::unexpected(Failure("open", path));
        }
        return File{stream};
    }

    std::expected<uint64_t, String> File::Size() const {
        ASSERT(_stream != nullptr, "File::Size: the file is not open");
        const int64_t size = SDL_GetIOSize(_stream);
        if (size < 0) {
            return std::unexpected(Failure("get the size"));
        }
        return static_cast<uint64_t>(size);
    }

    std::expected<size_t, String> File::Read(void* data, size_t size) {
        ASSERT(_stream != nullptr, "File::Read: the file is not open");
        const size_t count = SDL_ReadIO(_stream, data, size);
        if (count < size && SDL_GetIOStatus(_stream) == SDL_IO_STATUS_ERROR) {
            return std::unexpected(Failure("read"));
        }
        return count;
    }

    std::expected<void, String> File::Write(const void* data, size_t size) {
        ASSERT(_stream != nullptr, "File::Write: the file is not open");
        if (SDL_WriteIO(_stream, data, size) != size) {
            return std::unexpected(Failure("write"));
        }
        return {};
    }

    std::expected<void, String> File::Write(StringView text) {
        return Write(text.data(), text.size());
    }

    std::expected<void, String> File::Flush() {
        ASSERT(_stream != nullptr, "File::Flush: the file is not open");
        if (!SDL_FlushIO(_stream)) {
            return std::unexpected(Failure("flush"));
        }
        return {};
    }

    std::expected<void, String> File::Close() {
        ASSERT(_stream != nullptr, "File::Close: the file is not open");
        SDL_IOStream* stream = std::exchange(_stream, nullptr);
        if (!SDL_CloseIO(stream)) {
            return std::unexpected(Failure("close"));
        }
        return {};
    }

    Optional<FileInfo> GetFileInfo(StringView path) {
        const CPath cPath{path};
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(cPath.Get(), &info)) {
            return std::nullopt;
        }
        FileInfo result;
        switch (info.type) {
            case SDL_PATHTYPE_FILE:
                result.kind = FileKind::File;
                result.size = info.size;
                break;
            case SDL_PATHTYPE_DIRECTORY:
                result.kind = FileKind::Directory;
                break;
            case SDL_PATHTYPE_NONE:
                return std::nullopt;
            case SDL_PATHTYPE_OTHER:
                result.kind = FileKind::Other;
                result.size = info.size;
                break;
        }
        result.modifyTimeNs = info.modify_time;
        return result;
    }

    std::expected<void, String> CreateDirectories(StringView path) {
        const CPath cPath{path};
        if (!SDL_CreateDirectory(cPath.Get())) {
            return std::unexpected(Failure("create the folder", path));
        }
        return {};
    }

    std::expected<void, String> RemoveFile(StringView path) {
        const Optional<FileInfo> info = GetFileInfo(path);
        if (!info) {
            return std::unexpected(Format("Cannot remove '{}': there is no such file", path));
        }
        if (info->kind == FileKind::Directory) {
            return std::unexpected(Format("Cannot remove '{}': it is a folder", path));
        }
        const CPath cPath{path};
        if (!SDL_RemovePath(cPath.Get())) {
            return std::unexpected(Failure("remove", path));
        }
        return {};
    }

    std::expected<void, String> RenameFile(StringView from, StringView to) {
        const CPath cFrom{from};
        const CPath cTo{to};
        if (!SDL_RenamePath(cFrom.Get(), cTo.Get())) {
            return std::unexpected(Format("Cannot rename '{}' to '{}': {}", from, to, SDL_GetError()));
        }
        return {};
    }

    std::expected<void, String> WalkDirectory(StringView folder, WalkMode mode, FunctionRef<bool(StringView, FileKind)> visit) {
        Walker walker{mode, visit, ToString(folder)};
        if (walker.path.empty() || !PathIsSeparator(walker.path.back())) {
            walker.path.push_back('/');
        }
        walker.rootLength = walker.path.size();
        // The result is false for the folder that cannot be opened, and the callback never fails: a stop is a success
        if (!SDL_EnumerateDirectory(walker.path.c_str(), &WalkEntry, &walker)) {
            return std::unexpected(Failure("list", folder));
        }
        return {};
    }

    std::expected<void, String> RemoveTree(StringView path) {
        return RemoveTreeAt(ToString(path));
    }

    std::expected<void, String> CopyFileTo(StringView from, StringView to, CopyMode mode) {
        const Optional<FileInfo> source = GetFileInfo(from);
        if (!source) {
            return std::unexpected(Format("Cannot copy '{}': there is no such file", from));
        }
        if (source->kind == FileKind::Directory) {
            return std::unexpected(Format("Cannot copy '{}': it is a folder", from));
        }
        if (const Optional<FileInfo> target = GetFileInfo(to)) {
            if (target->kind == FileKind::Directory) {
                return std::unexpected(Format("Cannot copy '{}' to '{}': the target is a folder", from, to));
            }
            if (mode == CopyMode::Fail) {
                return std::unexpected(Format("Cannot copy '{}' to '{}': the target exists", from, to));
            }
            // Truncating the target would empty the source before it is read
            if (const auto same = IsSameFile(from, to); same && *same) {
                return std::unexpected(Format("Cannot copy '{}' to '{}': they are the same file", from, to));
            }
        }
        const CPath cFrom{from};
        const CPath cTo{to};
        if (!SDL_CopyFile(cFrom.Get(), cTo.Get())) {
            return std::unexpected(Format("Cannot copy '{}' to '{}': {}", from, to, SDL_GetError()));
        }
        return {};
    }

    std::expected<String, String> CurrentDirectory() {
        char* current = SDL_GetCurrentDirectory();
        if (current == nullptr) {
            return std::unexpected(Failure("get the current folder"));
        }
        String text(current);
        SDL_free(current);
        PathToGenericSeparators(text);
        // SDL ends the path with a separator; Path joins with its own, and a root keeps the one it is made of
        size_t rootLength = 1;
#ifdef _WIN32
        constexpr size_t kDriveRootLength = 3;
        if (text.size() >= 2 && text[1] == ':') {
            rootLength = kDriveRootLength;
        }
#endif
        while (text.size() > rootLength && text.back() == '/') {
            text.pop_back();
        }
        return text;
    }

    std::expected<String, String> AbsolutePath(StringView path) {
        const auto joined = JoinedWithCurrentDirectory(path);
        if (!joined) {
            return std::unexpected(joined.error());
        }
        return ToString(joined->LexicallyNormal().View());
    }

    std::expected<String, String> CanonicalPath(StringView path) {
#ifdef _WIN32
        constexpr DWORD kStartCapacity = MAX_PATH;
        constexpr StringView kExtendedPrefix = "\\\\?\\";
        constexpr StringView kExtendedUncPrefix = "\\\\?\\UNC\\";
        const HANDLE handle = CreateFileW(WidePath(path).Get(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return std::unexpected(WindowsFailure("resolve", path));
        }
        SmallVector<wchar_t, kStartCapacity> buffer(kStartCapacity);
        DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), VOLUME_NAME_DOS);
        if (length >= buffer.size()) {
            buffer.resize(length + 1);
            length = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), VOLUME_NAME_DOS);
        }
        const String failure = length == 0 ? WindowsFailure("resolve", path) : String{};
        CloseHandle(handle);
        if (length == 0) {
            return std::unexpected(failure);
        }
        String text = WideToUtf8(buffer.data(), length);
        const auto startsWith = [&](StringView prefix) { return text.compare(0, prefix.size(), prefix.data(), prefix.size()) == 0; };
        if (startsWith(kExtendedUncPrefix)) {
            // \\?\UNC\host\share is \\host\share
            text.erase(0, kExtendedUncPrefix.size() - 2);
        } else if (startsWith(kExtendedPrefix)) {
            text.erase(0, kExtendedPrefix.size());
        }
        PathToGenericSeparators(text);
        return text;
#else
        const CPath cPath{path};
        // realpath allocates with malloc when the buffer is null, and the result is freed below
        char* resolved = realpath(cPath.Get(), nullptr);
        if (resolved == nullptr) {
            return std::unexpected(ErrnoFailure("resolve", path));
        }
        String text(resolved);
        std::free(resolved);
        return text;
#endif
    }

    std::expected<String, String> WeaklyCanonicalPath(StringView path) {
        const auto joined = JoinedWithCurrentDirectory(path);
        if (!joined) {
            return std::unexpected(joined.error());
        }
        // The longest part that exists is resolved with its links, so `link/..` leaves the link's target; the rest is only cleaned up
        const StringView text = joined->View();
        StringView existing = text;
        while (!GetFileInfo(existing)) {
            const StringView parent = PathParent(existing);
            if (parent.empty() || parent.size() >= existing.size()) {
                return std::unexpected(Format("Cannot resolve '{}': no part of it exists", path));
            }
            existing = parent;
        }
        auto canonical = CanonicalPath(existing);
        if (!canonical) {
            return canonical;
        }
        StringView rest = text.substr(existing.size());
        while (!rest.empty() && PathIsSeparator(rest.front())) {
            rest.remove_prefix(1);
        }
        if (rest.empty()) {
            return canonical;
        }
        return ToString((Path(*canonical) / rest).LexicallyNormal().View());
    }

    std::expected<bool, String> IsSameFile(StringView first, StringView second) {
        const auto left = IdentityOf(first);
        if (!left) {
            return std::unexpected(left.error());
        }
        const auto right = IdentityOf(second);
        if (!right) {
            return std::unexpected(right.error());
        }
        return *left == *right;
    }

    std::expected<Vector<uint8_t>, String> ReadFileBytes(StringView path, size_t maxBytes) {
        auto whole = OpenForWholeRead(path, maxBytes);
        if (!whole) {
            return std::unexpected(std::move(whole.error()));
        }
        Vector<uint8_t> bytes(whole->size);
        if (auto read = ReadToTheEnd(whole->file, bytes.data(), bytes.size(), path); !read) {
            return std::unexpected(std::move(read.error()));
        }
        return bytes;
    }

    std::expected<String, String> ReadTextFile(StringView path, size_t maxBytes) {
        auto whole = OpenForWholeRead(path, maxBytes);
        if (!whole) {
            return std::unexpected(std::move(whole.error()));
        }
        String text(whole->size, '\0');
        if (auto read = ReadToTheEnd(whole->file, text.data(), text.size(), path); !read) {
            return std::unexpected(std::move(read.error()));
        }
        return text;
    }

    std::expected<ScratchFileBytes, String> ReadFileBytesScratch(StringView path, LinearArena& arena, size_t maxScratchBytes, size_t maxBytes) {
        auto whole = OpenForWholeRead(path, maxBytes);
        if (!whole) {
            return std::unexpected(std::move(whole.error()));
        }
        ScratchFileBytes result;
        if (whole->size <= maxScratchBytes) {
            if (whole->size != 0) {
                uint8_t* data = arena.Allocate<uint8_t>(whole->size);
                if (auto read = ReadToTheEnd(whole->file, data, whole->size, path); !read) {
                    return std::unexpected(std::move(read.error()));
                }
                result._inArena = Span<const uint8_t>{data, whole->size};
            }
            return result;
        }
        result._heap.resize(whole->size);
        if (auto read = ReadToTheEnd(whole->file, result._heap.data(), result._heap.size(), path); !read) {
            return std::unexpected(std::move(read.error()));
        }
        result._onHeap = true;
        return result;
    }

    std::expected<void, String> WriteFileAtomically(StringView path, Span<const uint8_t> bytes) {
        return WriteAtomically(path, bytes.data(), bytes.size());
    }

    std::expected<void, String> WriteFileAtomically(StringView path, StringView text) {
        return WriteAtomically(path, text.data(), text.size());
    }

}  // namespace BECore
