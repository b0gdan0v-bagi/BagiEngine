#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>
#include <expected>

struct SDL_IOStream;

namespace BECore {

    /**
     * How File::Open treats the file. Write, Append and CreateNew do not make the folder above the file (CreateDirectories does).
     * Every mode is binary: no line-end translation on any platform.
     */
    CORE_ENUM(FileMode, uint8_t,
              Read,       ///< the file must exist
              Write,      ///< creates the file or truncates it
              Append,     ///< creates the file or appends to it
              CreateNew)  ///< fails when the file exists; not race-free (SDL has no exclusive create): two processes can both pass the check

    CORE_ENUM(FileKind, uint8_t, File, Directory, Other)

    /// What GetFileInfo knows about a path. A directory has no size (0).
    struct FileInfo {
        FileKind kind = FileKind::Other;
        uint64_t size = 0;
        int64_t modifyTimeNs = 0;  ///< nanoseconds since the Unix epoch
    };

    /**
     * One open file, closed by the destructor. Move-only.
     *
     * Every path of this layer is a UTF-8 real path taken as it is: a virtual path ("config/A.xml") goes through
     * FileSystem::ResolvePath first. Every failure is a std::expected error with a sentence for the log; File::Open names the path,
     * the calls on an open file name only what SDL says (the file keeps no copy of the path, which would allocate), so the caller adds the path.
     */
    class File {
    public:
        File() noexcept = default;
        ~File();
        File(File&& other) noexcept;
        File& operator=(File&& other) noexcept;
        File(const File&) = delete;
        File& operator=(const File&) = delete;

        [[nodiscard]] static std::expected<File, String> Open(StringView path, FileMode mode);

        [[nodiscard]] bool IsOpen() const noexcept {
            return _stream != nullptr;
        }

        /// The size of the stream, not of the path: it also works for an Android asset, which GetFileInfo does not know
        [[nodiscard]] std::expected<uint64_t, String> Size() const;

        /// The number of bytes read: less than @p size only at the end of the file; an error for a read that failed
        [[nodiscard]] std::expected<size_t, String> Read(void* data, size_t size);

        /// All @p size bytes or an error
        [[nodiscard]] std::expected<void, String> Write(const void* data, size_t size);
        [[nodiscard]] std::expected<void, String> Write(StringView text);

        [[nodiscard]] std::expected<void, String> Flush();

        /// The one place where a deferred write error can show; the destructor closes without reporting. The file is closed afterwards even on an error.
        [[nodiscard]] std::expected<void, String> Close();

    private:
        explicit File(SDL_IOStream* stream) noexcept : _stream(stream) {}

        SDL_IOStream* _stream = nullptr;
    };

    /// The kind, size and modification time of @p path; nullopt for a path that is missing or cannot be read.
    /// Allocates nothing on POSIX; on Windows SDL converts the path to UTF-16 with one allocation.
    [[nodiscard]] Optional<FileInfo> GetFileInfo(StringView path);

    /// Makes @p path and the folders above it; a folder that exists is success
    [[nodiscard]] std::expected<void, String> CreateDirectories(StringView path);

    /// Unlike SDL_RemovePath, a path that is missing is an error (and so is a folder): on success the file is gone and it was there
    [[nodiscard]] std::expected<void, String> RemoveFile(StringView path);

    /// Replaces @p to when it is a file; fails when it is a folder
    [[nodiscard]] std::expected<void, String> RenameFile(StringView from, StringView to);

    /// The largest file ReadFileBytes and ReadTextFile read whole unless the caller passes a tighter bound: nothing in the engine reads more at once
    inline constexpr size_t kDefaultMaxFileBytes = size_t{256} * 1024 * 1024;

    /// Appended to the path for the file WriteFileAtomically writes before it renames it over the target
    inline constexpr StringView kAtomicTempSuffix = ".tmp";

    /**
     * The whole file in one allocation. The size comes from the open stream and is checked against @p maxBytes before anything is
     * allocated, so a file of an untrusted size cannot ask for memory the caller did not allow. An empty file is an empty result.
     */
    [[nodiscard]] std::expected<Vector<uint8_t>, String> ReadFileBytes(StringView path, size_t maxBytes = kDefaultMaxFileBytes);

    /// ReadFileBytes into a String: the bytes as they are, no encoding or line-end change
    [[nodiscard]] std::expected<String, String> ReadTextFile(StringView path, size_t maxBytes = kDefaultMaxFileBytes);

    /**
     * Writes @p path so a reader sees the old file or the new one: the folder above is made, the bytes go to "<path>.tmp"
     * (kAtomicTempSuffix), which is flushed, closed and renamed over the target. On a failure after the temp file was opened it is
     * removed and the first error is returned; the old target is untouched until the rename succeeds. There is no fsync: a power loss
     * can still lose the new file (and keep the old one); this is about a failed write, not about durability.
     */
    [[nodiscard]] std::expected<void, String> WriteFileAtomically(StringView path, Span<const uint8_t> bytes);
    [[nodiscard]] std::expected<void, String> WriteFileAtomically(StringView path, StringView text);

}  // namespace BECore
