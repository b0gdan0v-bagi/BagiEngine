#include "RmlUiFileInterface.h"

#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>

namespace BECore {

    RmlUiFileStamp RmlUiFileStamp::Of(const Path& path) {
        RmlUiFileStamp stamp;
        stamp.path = path;
        const Optional<FileInfo> info = GetFileInfo(path);
        if (!info || info->kind != FileKind::File) {
            return stamp;
        }
        stamp.lastWriteNs = info->modifyTimeNs;
        stamp.size = info->size;
        return stamp;
    }

    bool RmlUiFileStamp::IsStale() const {
        const Optional<FileInfo> info = GetFileInfo(path);
        if (!info || info->kind != FileKind::File) {
            return false;
        }
        return info->modifyTimeNs != lastWriteNs || info->size != size;
    }

    void RmlUiFileInterface::BeginRecording() {
        _recorded.clear();
        _isRecording = true;
    }

    Vector<RmlUiFileStamp> RmlUiFileInterface::EndRecording() {
        _isRecording = false;
        return std::move(_recorded);
    }

    Rml::FileHandle RmlUiFileInterface::Open(const Rml::String& path) {
        const StringView virtualPath(path.data(), path.size());
        Path realPath = CoreManager::GetFileSystem().ResolvePath(virtualPath);
        const Optional<FileInfo> info = realPath.Empty() ? Optional<FileInfo>() : GetFileInfo(realPath);
        if (!info || info->kind != FileKind::File) {
            realPath = Path(virtualPath);
        }
        if (_isRecording) {
            const bool known = std::any_of(_recorded.begin(), _recorded.end(), [&](const RmlUiFileStamp& stamp) { return stamp.path == realPath; });
            if (!known) {
                _recorded.push_back(RmlUiFileStamp::Of(realPath));
            }
        }
        auto bytes = ReadFileBytes(realPath);
        if (!bytes) {
            return 0;
        }
        OpenFile opened;
        opened.bytes = std::move(*bytes);
        const Rml::FileHandle handle = _nextHandle++;
        _files.emplace(handle, std::move(opened));
        return handle;
    }

    void RmlUiFileInterface::Close(Rml::FileHandle file) {
        _files.erase(file);
    }

    size_t RmlUiFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file) {
        const auto found = _files.find(file);
        if (found == _files.end()) {
            return 0;
        }
        OpenFile& opened = found->second;
        const size_t count = std::min(size, opened.bytes.size() - opened.position);
        std::memcpy(buffer, opened.bytes.data() + opened.position, count);
        opened.position += count;
        return count;
    }

    bool RmlUiFileInterface::Seek(Rml::FileHandle file, long offset, int origin) {
        const auto found = _files.find(file);
        if (found == _files.end()) {
            return false;
        }
        OpenFile& opened = found->second;
        long base = 0;
        if (origin == SEEK_CUR) {
            base = static_cast<long>(opened.position);
        } else if (origin == SEEK_END) {
            base = static_cast<long>(opened.bytes.size());
        }
        const long target = base + offset;
        if (target < 0 || target > static_cast<long>(opened.bytes.size())) {
            return false;
        }
        opened.position = static_cast<size_t>(target);
        return true;
    }

    size_t RmlUiFileInterface::Tell(Rml::FileHandle file) {
        const auto found = _files.find(file);
        return found == _files.end() ? 0 : found->second.position;
    }

    size_t RmlUiFileInterface::Length(Rml::FileHandle file) {
        const auto found = _files.find(file);
        return found == _files.end() ? 0 : found->second.bytes.size();
    }

}  // namespace BECore
