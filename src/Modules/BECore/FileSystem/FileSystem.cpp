#include "FileSystem.h"

#include <BECore/FileSystem/MountPoints.h>
#include <EASTL/sort.h>

namespace BECore {

    void FileSystem::Initialize() {
        if (_roots.empty()) {
            AddContentRoot(FindRootDirectory());
        }

        // Lowest priority first, so the layer of the game lands above the engine's
        for (const auto& root : _roots) {
            for (const PoolString name : Array<PoolString, 2>{MountPoints::config, MountPoints::assets}) {
                const auto dirName = name.ToStringView();
                std::filesystem::path dirPath = root / ToStd(dirName);
                if (std::filesystem::exists(dirPath) && std::filesystem::is_directory(dirPath)) {
                    Mount(name, dirPath);
                }
            }
        }
    }

    void FileSystem::AddContentRoot(const std::filesystem::path& root) {
        if (root.empty()) {
            return;
        }

        std::filesystem::path normalizedRoot = std::filesystem::absolute(root).lexically_normal();
        if (eastl::find(_roots.begin(), _roots.end(), normalizedRoot) != _roots.end()) {
            return;
        }

        _roots.push_back(normalizedRoot);
        _rootPath = _roots.back();
    }

    void FileSystem::SetRootPath(const std::filesystem::path& rootPath) {
        _roots.clear();
        _rootPath.clear();
        AddContentRoot(rootPath);
    }

    bool FileSystem::Mount(PoolString virtualPath, const std::filesystem::path& realPath) {
        if (virtualPath.Empty()) {
            return false;
        }

        std::filesystem::path normalizedPath = std::filesystem::absolute(realPath).lexically_normal();
        if (!std::filesystem::exists(normalizedPath)) {
            return false;
        }

        if (!std::filesystem::is_directory(normalizedPath)) {
            return false;
        }

        auto& layers = _mountPoints[virtualPath];
        if (eastl::find(layers.begin(), layers.end(), normalizedPath) == layers.end()) {
            layers.push_back(std::move(normalizedPath));
        }
        return true;
    }

    void FileSystem::Unmount(PoolString virtualPath) {
        _mountPoints.erase(virtualPath);
    }

    std::filesystem::path FileSystem::ResolvePath(StringView virtualPath) const {
        if (virtualPath.empty()) {
            return {};
        }

        std::filesystem::path path(ToStd(virtualPath));  // std::filesystem::path always allocates
        if (path.is_absolute()) {
            return std::filesystem::exists(path) ? path : std::filesystem::path{};
        }

        size_t firstSlash = virtualPath.find_first_of("/\\");

        StringView mountPoint;
        StringView remainingPath;

        if (firstSlash != StringView::npos) {
            mountPoint = virtualPath.substr(0, firstSlash);
            remainingPath = virtualPath.substr(firstSlash + 1);
        } else {
            mountPoint = virtualPath;
        }

        // Heterogeneous lookup: no allocation for the mount point; layers from the top
        auto it = _mountPoints.Find(mountPoint);

        if (it != _mountPoints.end()) {
            for (auto layer = it->second.rbegin(); layer != it->second.rend(); ++layer) {
                std::filesystem::path fullPath = *layer;

                if (!remainingPath.empty()) {
                    fullPath /= ToStd(remainingPath);
                }

                if (std::filesystem::exists(fullPath)) {
                    return fullPath;
                }
            }
        }

        for (auto root = _roots.rbegin(); root != _roots.rend(); ++root) {
            std::filesystem::path rootFullPath = *root / ToStd(virtualPath);
            if (std::filesystem::exists(rootFullPath)) {
                return rootFullPath;
            }
        }

        std::filesystem::path currentPath = std::filesystem::current_path() / ToStd(virtualPath);
        if (std::filesystem::exists(currentPath)) {
            return currentPath;
        }

        return {};
    }

    bool FileSystem::Exists(StringView virtualPath) const {
        std::filesystem::path resolved = ResolvePath(virtualPath);
        return !resolved.empty() && std::filesystem::exists(resolved);
    }

    std::filesystem::path FileSystem::GetMountedPath(PoolString virtualPath) const {
        auto it = _mountPoints.find(virtualPath);
        if (it != _mountPoints.end() && !it->second.empty()) {
            return it->second.back();
        }
        return {};
    }

    Vector<std::filesystem::path> FileSystem::GetMountLayers(PoolString virtualPath) const {
        auto it = _mountPoints.find(virtualPath);
        if (it != _mountPoints.end()) {
            return it->second;
        }
        return {};
    }

    Vector<PoolString> FileSystem::EnumerateFiles(PoolString mountPoint, Span<const StringView> extensions) const {
        Vector<PoolString> result;

        auto it = _mountPoints.find(mountPoint);
        if (it == _mountPoints.end()) {
            return result;
        }

        // Relative paths from all layers: a file present in several layers is listed once
        Vector<std::filesystem::path> files;

        for (const auto& basePath : it->second) {
            std::error_code ec;
            std::filesystem::recursive_directory_iterator fileSystemIt(basePath, std::filesystem::directory_options::skip_permission_denied, ec);
            if (ec) {
                LOG_ERROR("FileSystem::EnumerateFiles: error opening '{}': {}", mountPoint.ToStringView(), ec.message());
                continue;
            }
            std::filesystem::recursive_directory_iterator fileSystemItEnd;

            while (fileSystemIt != fileSystemItEnd) {
                const auto& entry = *fileSystemIt;
                if (entry.is_regular_file()) {
                    const auto& filePath = entry.path();
                    auto ext = filePath.extension().string();

                    bool matches = false;
                    for (const auto& targetExt : extensions) {
                        if (ext.size() == targetExt.size()) {
                            bool same = true;
                            for (size_t i = 0; i < ext.size(); ++i) {
                                char a = std::tolower(static_cast<unsigned char>(ext[i]));
                                char b = std::tolower(static_cast<unsigned char>(targetExt[i]));
                                if (a != b) {
                                    same = false;
                                    break;
                                }
                            }
                            if (same) {
                                matches = true;
                                break;
                            }
                        }
                    }

                    if (matches) {
                        files.push_back(std::filesystem::relative(filePath, basePath));
                    }
                }

                fileSystemIt.increment(ec);
                if (ec) {
                    LOG_ERROR("FileSystem::EnumerateFiles: error scanning '{}': {}", mountPoint.ToStringView(), ec.message());
                    break;
                }
            }
        }

        eastl::sort(files.begin(), files.end());
        files.erase(eastl::unique(files.begin(), files.end()), files.end());

        Vector<String> virtualPaths;
        virtualPaths.reserve(files.size());
        for (const auto& relativePath : files) {
            String virtualPath(mountPoint.ToStringView().data(), mountPoint.ToStringView().size());
            virtualPath += "/";
            //-NoStd: the text of a std::filesystem::path
            const std::string relStr = relativePath.string();
            virtualPath.append(relStr.data(), relStr.size());

            for (auto& ch : virtualPath) {
                if (ch == '\\') {
                    ch = '/';
                }
            }

            virtualPaths.push_back(eastl::move(virtualPath));
        }

        // One batch: the names come from the disk, so the form that hashes them
        Vector<StringView> views;
        views.reserve(virtualPaths.size());
        for (const String& virtualPath : virtualPaths) {
            views.push_back(StringView(virtualPath));
        }
        result.resize(views.size());
        PoolString::InternBatch(Span<const StringView>(views.data(), views.size()), Span<PoolString>(result.data(), result.size()));

        return result;
    }

    std::filesystem::path FileSystem::FindRootDirectory() const {
        std::filesystem::path exePath;

        std::filesystem::path current = std::filesystem::current_path();

        std::filesystem::path searchPath = current;
        int maxDepth = 10;

        for (int i = 0; i < maxDepth; ++i) {
            std::filesystem::path configDir = searchPath / "config";
            if (std::filesystem::exists(configDir) && std::filesystem::is_directory(configDir)) {
                return searchPath;
            }

            if (searchPath.has_parent_path() && searchPath != searchPath.parent_path()) {
                searchPath = searchPath.parent_path();
            } else {
                break;
            }
        }

        return current;
    }

}  // namespace BECore
