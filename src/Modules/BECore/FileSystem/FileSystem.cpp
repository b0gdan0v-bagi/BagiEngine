#include "FileSystem.h"

#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Memory/ScratchArena.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        /// A separator of std::filesystem::path: a backslash is one only where the OS says so (a POSIX name may hold it)
        bool IsPathSeparator(char ch) {
#ifdef _WIN32
            return ch == '/' || ch == '\\';
#else
            return ch == '/';
#endif
        }

        /**
         * The part of @p file below @p base, or an empty view when @p file does not start with @p base as a folder. The iterator builds each path as
         * "<base>/<name>", so cutting the prefix gives what std::filesystem::relative would, without its calls to the OS and its temporary paths.
         */
        StringView RelativeText(const std::string& file, const std::string& base) {  //-NoStd: the text of a std::filesystem::path
            if (file.size() <= base.size() || file.compare(0, base.size(), base) != 0) {
                return {};
            }
            size_t start = base.size();
            const bool baseEndsInSeparator = !base.empty() && IsPathSeparator(base.back());
            if (!baseEndsInSeparator && !IsPathSeparator(file[start])) {
                return {};  // "<base>x/y" is a sibling folder of the base
            }
            while (start < file.size() && IsPathSeparator(file[start])) {
                ++start;
            }
            return StringView(file.data() + start, file.size() - start);
        }

        /// The next component of @p text from @p pos (empty components skipped), advancing @p pos past it
        StringView NextComponent(StringView text, size_t& pos) {
            while (pos < text.size() && IsPathSeparator(text[pos])) {
                ++pos;
            }
            const size_t start = pos;
            while (pos < text.size() && !IsPathSeparator(text[pos])) {
                ++pos;
            }
            return text.substr(start, pos - start);
        }

        /**
         * Orders two relative paths as std::filesystem::path does, component by component and byte by byte: "a/b" comes before "a-b" and "a.txt",
         * which a comparison of the whole text would put after them. The listing's order has always been the path's, and callers (the slot list
         * of a save, the editors' file pickers) show it.
         */
        int ComparePathText(StringView left, StringView right) {
            size_t leftPos = 0;
            size_t rightPos = 0;
            for (;;) {
                const StringView leftPart = NextComponent(left, leftPos);
                const StringView rightPart = NextComponent(right, rightPos);
                if (leftPart.empty() || rightPart.empty()) {
                    return static_cast<int>(!leftPart.empty()) - static_cast<int>(!rightPart.empty());
                }
                const size_t common = eastl::min(leftPart.size(), rightPart.size());
                for (size_t i = 0; i < common; ++i) {
                    const auto leftByte = static_cast<unsigned char>(leftPart[i]);
                    const auto rightByte = static_cast<unsigned char>(rightPart[i]);
                    if (leftByte != rightByte) {
                        return leftByte < rightByte ? -1 : 1;
                    }
                }
                if (leftPart.size() != rightPart.size()) {
                    return leftPart.size() < rightPart.size() ? -1 : 1;
                }
            }
        }

    }  // namespace

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

        const ScratchScope scratch;

        // The virtual text of each match, "<mount>/<relative path>", as the OS wrote the relative path: the order below needs its separators
        // and the backslash of a POSIX name is turned into a slash only after it. A file present in several layers is listed once.
        const StringView mountText = mountPoint.ToStringView();
        ArenaVector<ArenaString> files;

        for (const auto& basePath : it->second) {
            std::error_code ec;
            std::filesystem::recursive_directory_iterator fileSystemIt(basePath, std::filesystem::directory_options::skip_permission_denied, ec);
            if (ec) {
                LOG_ERROR("FileSystem::EnumerateFiles: error opening '{}': {}", mountPoint.ToStringView(), ec.message());
                continue;
            }
            std::filesystem::recursive_directory_iterator fileSystemItEnd;
            const std::string baseStr = basePath.string();  //-NoStd: the text of a std::filesystem::path

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
                        //-NoStd: the text of a std::filesystem::path
                        const std::string fileStr = filePath.string();
                        StringView relative = RelativeText(fileStr, baseStr);
                        std::string fallback;  //-NoStd: the text of a std::filesystem::path
                        if (relative.empty()) {
                            fallback = std::filesystem::relative(filePath, basePath).string();
                            relative = StringView(fallback.data(), fallback.size());
                        }
                        ArenaString& text = files.emplace_back();
                        text.reserve(mountText.size() + 1 + relative.size());
                        text.append(mountText.data(), mountText.size());
                        text.push_back('/');
                        text.append(relative.data(), relative.size());
                    }
                }

                fileSystemIt.increment(ec);
                if (ec) {
                    LOG_ERROR("FileSystem::EnumerateFiles: error scanning '{}': {}", mountPoint.ToStringView(), ec.message());
                    break;
                }
            }
        }

        const size_t relativeStart = mountText.size() + 1;
        const auto byPath = [relativeStart](const ArenaString& left, const ArenaString& right) {
            return ComparePathText(StringView(left.data() + relativeStart, left.size() - relativeStart), StringView(right.data() + relativeStart, right.size() - relativeStart)) < 0;
        };
        eastl::sort(files.begin(), files.end(), byPath);
        files.erase(eastl::unique(files.begin(), files.end(), [&](const ArenaString& left, const ArenaString& right) { return !byPath(left, right) && !byPath(right, left); }), files.end());

        // One batch: the names come from the disk, so the form that hashes them
        ArenaVector<StringView> views;
        views.reserve(files.size());
        for (ArenaString& text : files) {
            for (auto& ch : text) {
                if (ch == '\\') {
                    ch = '/';
                }
            }
            views.push_back(StringView(text.data(), text.size()));
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
