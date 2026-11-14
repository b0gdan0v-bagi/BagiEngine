#include "RmlUiFileInterface.h"

#include <BECore/GameManager/CoreManager.h>

namespace BECore {

    RmlUiFileStamp RmlUiFileStamp::Of(const std::filesystem::path& path) {
        RmlUiFileStamp stamp;
        stamp.path = path;
        std::error_code error;
        const auto lastWrite = std::filesystem::last_write_time(path, error);
        if (error) {
            return stamp;
        }
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            return stamp;
        }
        stamp.lastWrite = lastWrite;
        stamp.size = size;
        return stamp;
    }

    bool RmlUiFileStamp::IsStale() const {
        std::error_code error;
        const auto lastWrite = std::filesystem::last_write_time(path, error);
        if (error) {
            return false;
        }
        const auto currentSize = std::filesystem::file_size(path, error);
        if (error) {
            return false;
        }
        return lastWrite != this->lastWrite || currentSize != size;
    }

    void RmlUiFileInterface::BeginRecording() {
        _recorded.clear();
        _isRecording = true;
    }

    eastl::vector<RmlUiFileStamp> RmlUiFileInterface::EndRecording() {
        _isRecording = false;
        return std::move(_recorded);
    }

    Rml::FileHandle RmlUiFileInterface::Open(const Rml::String& path) {
        const StringView virtualPath(path.data(), path.size());
        std::filesystem::path realPath = CoreManager::GetFileSystem().ResolvePath(virtualPath);
        std::error_code error;
        if (realPath.empty() || !std::filesystem::is_regular_file(realPath, error)) {
            realPath = std::filesystem::path(path);
        }
        if (_isRecording) {
            const bool known = std::any_of(_recorded.begin(), _recorded.end(), [&](const RmlUiFileStamp& stamp) { return stamp.path == realPath; });
            if (!known) {
                _recorded.push_back(RmlUiFileStamp::Of(realPath));
            }
        }
        std::ifstream file(realPath, std::ios::binary | std::ios::ate);
        if (!file) {
            return 0;
        }
        OpenFile opened;
        opened.bytes.resize(static_cast<size_t>(file.tellg()));
        file.seekg(0);
        file.read(opened.bytes.data(), static_cast<std::streamsize>(opened.bytes.size()));
        if (!file) {
            return 0;
        }
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
