#include "FileSystem.h"

#include <BECore/File/File.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Memory/ScratchArena.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        constexpr StringView kParentName = "..";

        struct MountedName {
            StringView mountPoint;  ///< the first segment, whether such a mount point exists or not
            StringView remainder;   ///< what follows its separator
        };

        MountedName SplitMountPoint(StringView virtualPath) {
            const size_t slash = virtualPath.find_first_of("/\\");
            if (slash == StringView::npos) {
                return {virtualPath, {}};
            }
            return {virtualPath.substr(0, slash), virtualPath.substr(slash + 1)};
        }

        bool IsConfinedName(StringView name) {
#ifdef _WIN32
            // "C:x" joined to a folder replaces the whole path, and Win32 trims trailing dots and spaces: ".. " names the parent
            if (name.find(':') != StringView::npos || (name != "." && !name.empty() && name.find_first_not_of(". ") == StringView::npos)) {
                return false;
            }
#endif
            return name != kParentName;
        }

        /// IsConfinedVirtualPath with a word about a refusal: bad data must not look like a missing file
        bool AcceptVirtualPath(StringView virtualPath) {
            if (IsConfinedVirtualPath(virtualPath)) {
                return true;
            }
            LOG_WARNING("[FileSystem] Refused a virtual path that may leave its folder: '{}'", virtualPath);
            return false;
        }

        Path JoinLayer(const Path& layer, StringView remainder) {
            Path path = layer;
            if (!remainder.empty()) {
                path /= remainder;
            }
            return path;
        }

        bool IsDirectory(const Path& path) {
            const Optional<FileInfo> info = GetFileInfo(path.View());
            return info.has_value() && info->kind == FileKind::Directory;
        }

        bool PathExists(const Path& path) {
            return GetFileInfo(path.View()).has_value();
        }

        /// Whether @p extension is one of @p extensions: byte by byte, case-insensitively
        bool MatchesExtension(StringView extension, Span<const StringView> extensions) {
            for (const StringView& target : extensions) {
                if (extension.size() != target.size()) {
                    continue;
                }
                bool same = true;
                for (size_t i = 0; i < extension.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(extension[i])) != std::tolower(static_cast<unsigned char>(target[i]))) {
                        same = false;
                        break;
                    }
                }
                if (same) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    Path AbsoluteNormalPath(const Path& path) {
        if (path.Empty()) {
            return path;
        }
        std::expected<String, String> absolute = AbsolutePath(path.View());
        return absolute ? Path(eastl::move(*absolute)) : path;
    }

    bool IsConfinedVirtualPath(StringView virtualPath) {
        // The OS cuts a path at a NUL byte, so the names checked here would not be the names it opens
        if (virtualPath.empty() || virtualPath.find('\0') != StringView::npos) {
            return false;
        }
        const MountedName split = SplitMountPoint(virtualPath);
        if (split.mountPoint.empty() || !IsConfinedName(split.mountPoint)) {
            return false;
        }
        StringView rest = split.remainder;
        while (!rest.empty()) {
            size_t length = 0;
            while (length < rest.size() && !PathIsSeparator(rest[length])) {
                ++length;
            }
            const StringView name = rest.substr(0, length);
            if (name.empty() || !IsConfinedName(name)) {
                return false;
            }
            rest.remove_prefix(length < rest.size() ? length + 1 : length);
        }
        return true;
    }

    void FileSystem::Initialize() {
        if (_roots.empty()) {
            AddContentRoot(FindRootDirectory());
        }

        // Lowest priority first, so the layer of the game lands above the engine's
        for (const Path& root : _roots) {
            for (const PoolString name : Array<PoolString, 2>{MountPoints::config, MountPoints::assets}) {
                const Path dirPath = root / name.ToStringView();
                if (IsDirectory(dirPath)) {
                    Mount(name, dirPath);
                }
            }
        }

        // Last, so the overlay is the top layer of both mount points even when no root has the folder
        if (!_overlay.Empty()) {
            for (const PoolString name : Array<PoolString, 2>{MountPoints::config, MountPoints::assets}) {
                const Path dirPath = _overlay / name.ToStringView();
                if (const auto made = CreateDirectories(dirPath.View()); !made) {
                    LOG_ERROR("[FileSystem] Content overlay: {}", made.error());
                    continue;
                }
                Mount(name, dirPath);
            }
        }
    }

    std::expected<void, String> FileSystem::SetContentOverlay(const Path& folder) {
        if (folder.Empty()) {
            return std::unexpected(String("the content overlay folder is empty"));
        }
        const Path normalized = AbsoluteNormalPath(folder);
        if (auto made = CreateDirectories(normalized.View()); !made) {
            return std::unexpected(eastl::move(made.error()));
        }
        _overlay = normalized;
        return {};
    }

    Path FileSystem::WriteTarget(const Path& realFile) const {
        if (_overlay.Empty()) {
            return realFile;
        }
        const Path file = AbsoluteNormalPath(realFile);
        if (file.IsUnder(_overlay)) {
            return realFile;
        }

        // The deepest layer wins when layers nest; the overlay's own layers were handled above
        const Path* bestLayer = nullptr;
        PoolString bestMount;
        for (const auto& [mount, layers] : _mountPoints) {
            for (const Path& layer : layers) {
                if (file.IsUnder(layer) && !file.Relative(layer).Empty() && (bestLayer == nullptr || layer.View().size() > bestLayer->View().size())) {
                    bestLayer = &layer;
                    bestMount = mount;
                }
            }
        }
        if (bestLayer == nullptr) {
            return realFile;
        }
        return _overlay / bestMount.ToStringView() / file.Relative(*bestLayer).View();
    }

    void FileSystem::AddContentRoot(const Path& root) {
        if (root.Empty()) {
            return;
        }

        Path normalizedRoot = AbsoluteNormalPath(root);
        if (eastl::find(_roots.begin(), _roots.end(), normalizedRoot) != _roots.end()) {
            return;
        }

        _roots.push_back(normalizedRoot);
        _rootPath = _roots.back();
    }

    void FileSystem::SetRootPath(const Path& rootPath) {
        _roots.clear();
        _rootPath.Clear();
        AddContentRoot(rootPath);
    }

    bool FileSystem::Mount(PoolString virtualPath, const Path& realPath) {
        if (virtualPath.Empty()) {
            return false;
        }

        Path normalizedPath = AbsoluteNormalPath(realPath);
        if (!IsDirectory(normalizedPath)) {
            return false;
        }

        auto& layers = _mountPoints[virtualPath];
        if (eastl::find(layers.begin(), layers.end(), normalizedPath) == layers.end()) {
            layers.push_back(eastl::move(normalizedPath));
        }
        return true;
    }

    void FileSystem::Unmount(PoolString virtualPath) {
        _mountPoints.erase(virtualPath);
    }

    Path FileSystem::ResolvePath(StringView virtualPath) const {
        if (virtualPath.empty()) {
            return {};
        }

        if (PathIsAbsolute(virtualPath)) {
            Path path(virtualPath);
            return PathExists(path) ? path : Path{};
        }

        if (!AcceptVirtualPath(virtualPath)) {
            return {};
        }

        const MountedName name = SplitMountPoint(virtualPath);

        // Heterogeneous lookup: no allocation for the mount point; layers from the top
        auto it = _mountPoints.Find(name.mountPoint);

        if (it != _mountPoints.end()) {
            for (auto layer = it->second.rbegin(); layer != it->second.rend(); ++layer) {
                Path fullPath = JoinLayer(*layer, name.remainder);

                if (PathExists(fullPath)) {
                    return fullPath;
                }
            }
        }

        for (auto root = _roots.rbegin(); root != _roots.rend(); ++root) {
            Path rootFullPath = *root / virtualPath;
            if (PathExists(rootFullPath)) {
                return rootFullPath;
            }
        }

        std::expected<String, String> current = CurrentDirectory();
        if (current) {
            Path currentPath = Path(eastl::move(*current)) / virtualPath;
            if (PathExists(currentPath)) {
                return currentPath;
            }
        }

        return {};
    }

    bool FileSystem::Exists(StringView virtualPath) const {
        const Path resolved = ResolvePath(virtualPath);
        return !resolved.Empty() && PathExists(resolved);
    }

    Path FileSystem::PathInTopLayer(StringView virtualPath) const {
        if (virtualPath.empty() || !AcceptVirtualPath(virtualPath)) {
            return {};
        }
        const MountedName name = SplitMountPoint(virtualPath);
        auto it = _mountPoints.Find(name.mountPoint);
        if (it == _mountPoints.end() || it->second.empty()) {
            return {};
        }
        return JoinLayer(it->second.back(), name.remainder);
    }

    Path FileSystem::GetMountedPath(PoolString virtualPath) const {
        auto it = _mountPoints.find(virtualPath);
        if (it != _mountPoints.end() && !it->second.empty()) {
            return it->second.back();
        }
        return {};
    }

    Vector<Path> FileSystem::GetMountLayers(PoolString virtualPath) const {
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

        for (const Path& layer : it->second) {
            const auto listed = WalkDirectory(layer.View(), WalkMode::Tree, [&](StringView relative, FileKind kind) {
                if (kind == FileKind::File && MatchesExtension(PathExtension(relative), extensions)) {
                    ArenaString& text = files.emplace_back();
                    text.reserve(mountText.size() + 1 + relative.size());
                    text.append(mountText.data(), mountText.size());
                    text.push_back('/');
                    text.append(relative.data(), relative.size());
                }
                return true;
            });
            if (!listed) {
                LOG_ERROR("FileSystem::EnumerateFiles: error opening '{}': {}", mountText, listed.error());
            }
        }

        const size_t relativeStart = mountText.size() + 1;
        const auto relativeOf = [relativeStart](const ArenaString& text) { return StringView(text.data() + relativeStart, text.size() - relativeStart); };
        eastl::sort(files.begin(), files.end(), [&](const ArenaString& left, const ArenaString& right) { return PathCompare(relativeOf(left), relativeOf(right)) < 0; });
        files.erase(eastl::unique(files.begin(), files.end(), [&](const ArenaString& left, const ArenaString& right) { return PathCompare(relativeOf(left), relativeOf(right)) == 0; }), files.end());

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

    Path FileSystem::FindRootDirectory() const {
        std::expected<String, String> working = CurrentDirectory();
        if (!working) {
            return {};
        }
        const Path current(eastl::move(*working));

        Path searchPath = current;
        int maxDepth = 10;

        for (int i = 0; i < maxDepth; ++i) {
            if (IsDirectory(searchPath / "config")) {
                return searchPath;
            }

            Path parent = searchPath.Parent();
            if (parent.Empty() || parent == searchPath) {
                break;
            }
            searchPath = eastl::move(parent);
        }

        return current;
    }

}  // namespace BECore
