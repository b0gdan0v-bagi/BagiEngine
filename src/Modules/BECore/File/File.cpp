#include "File.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

namespace BECore {

    namespace {

        /// Bytes of an ordinary path that stay inside the object: a call with a path allocates nothing
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

    }  // namespace

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

}  // namespace BECore
