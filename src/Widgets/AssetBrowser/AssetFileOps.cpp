#include "AssetFileOps.h"

#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <EASTL/algorithm.h>
#include <Widgets/AssetBrowser/AssetCatalog.h>

namespace BECore {

    namespace {

        /// The extensions of two parts: a rename that kept only the last one would make another kind of file
        constexpr Array<StringView, 2> kDoubleExtensions = {".ui.xml", ".ui.html"};

        /// The mount point of a content path and the path below it
        struct ContentPath {
            PoolString mount;
            StringView relative;
        };

        /// @p path as a file of an asset mount, written the one way that leaves no doubt where it is
        Optional<ContentPath> SplitContentPath(StringView path) {
            if (path.find('\\') != StringView::npos || Path(path).LexicallyNormal().View() != path) {
                return std::nullopt;
            }
            for (const PoolString mount : GetAssetMounts()) {
                const StringView name = mount.ToStringView();
                if (path.size() > name.size() + 1 && path.starts_with(name) && path[name.size()] == '/') {
                    return ContentPath{mount, path.substr(name.size() + 1)};
                }
            }
            return std::nullopt;
        }

        /// A link on the way from the content root to the file leads a change to wherever it points: another repository's file, for one
        bool IsReachedThroughLink(const Path& file, const Path& root) {
            for (Path step = file; step != root && !step.Empty(); step = step.Parent()) {
                if (IsSymbolicLink(step.View())) {
                    return true;
                }
            }
            return false;
        }

        /// The name of the cooked form of the node file @p fileName
        String CookedName(StringView fileName) {
            return Format("{}{}", fileName.substr(0, fileName.size() - PathExtension(fileName).size()), kNodeBinaryExtension);
        }

        bool IsInALowerLayer(const FileSystem& fileSystem, const ContentPath& content, AssetKind kind) {
            const Path ownLayer = fileSystem.GetRootPath() / content.mount.ToStringView();
            for (const Path& layer : fileSystem.GetMountLayers(content.mount)) {
                if (layer != ownLayer && GetFileInfo((layer / content.relative).View())) {
                    return true;
                }
                // The cooked form answers to a node's reference from any layer, the file's own too
                if (kind == AssetKind::Node && GetFileInfo((layer / PathParent(content.relative) / CookedName(PathFilename(content.relative))).View())) {
                    return true;
                }
            }
            return false;
        }

        /// Fails when a layer holds, in the folder @p folder of the mount, a name that is @p name on a file system that ignores case
        std::expected<void, String> CheckNameFree(const FileSystem& fileSystem, PoolString mount, StringView folder, StringView name) {
            for (const Path& layer : fileSystem.GetMountLayers(mount)) {
                const Path directory = folder.empty() ? layer : layer / folder;
                if (!GetFileInfo(directory.View())) {
                    continue;
                }
                bool taken = false;
                const auto listed = WalkDirectory(directory.View(), WalkMode::Folder, [&](StringView entry, FileKind) {
                    taken = EqualsIgnoreAsciiCase(entry, name);
                    return !taken;
                });
                if (!listed) {
                    return std::unexpected(listed.error());
                }
                if (taken) {
                    return std::unexpected(Format("'{}' is taken: {} has it", name, directory.View()));
                }
            }
            return {};
        }

        /// Letters, digits, `_` and `-`; with @p allowDots also single dots between them
        bool IsAllowedStem(StringView stem, bool allowDots) {
            for (size_t i = 0; i < stem.size(); ++i) {
                const char c = stem[i];
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
                    continue;
                }
                if (c != '.' || !allowDots || i == 0 || i + 1 == stem.size() || stem[i - 1] == '.') {
                    return false;
                }
            }
            return !stem.empty();
        }

    }  // namespace

    StringView AssetRenameStem(StringView virtualPath) {
        const StringView name = PathFilename(virtualPath);
        for (const StringView extension : kDoubleExtensions) {
            if (name.size() > extension.size() && EqualsIgnoreAsciiCase(name.substr(name.size() - extension.size()), extension)) {
                return name.substr(0, name.size() - extension.size());
            }
        }
        return name.substr(0, name.size() - PathExtension(name).size());
    }

    bool IsChangeableAssetKind(AssetKind kind) {
        return kind == AssetKind::Texture || kind == AssetKind::Font || kind == AssetKind::Sound || kind == AssetKind::UiDocument || kind == AssetKind::Node || kind == AssetKind::Other;
    }

    std::expected<Path, String> ResolveContentRootFile(const FileSystem& fileSystem, StringView path) {
        const Optional<ContentPath> content = SplitContentPath(path);
        if (!content) {
            return std::unexpected(Format("'{}' is not a content file", path));
        }
        const Path file = fileSystem.ResolvePath(path);
        const Optional<FileInfo> info = file.Empty() ? Optional<FileInfo>() : GetFileInfo(file.View());
        if (!info || info->kind != FileKind::File) {
            return std::unexpected(Format("'{}' is not a file on disk", path));
        }
        // The layer itself and not only "below the root": a game's root holds the engine's checkout
        const Path& root = fileSystem.GetRootPath();
        if (file != root / content->mount.ToStringView() / content->relative) {
            return std::unexpected(Format("Only a file of the content root {} can be changed here; this one is {}", root.View(), file.View()));
        }
        if (IsReachedThroughLink(file, root)) {
            return std::unexpected(Format("'{}' is reached through a symbolic link: change it on disk", path));
        }
        return file;
    }

    std::expected<AssetFileTarget, String> CheckAssetChange(const FileSystem& fileSystem, StringView path) {
        const Optional<ContentPath> content = SplitContentPath(path);
        if (content && !fileSystem.GetContentOverlay().Empty()) {
            return std::unexpected(String("A content overlay is on (--content-overlay), and it cannot express a removal"));
        }
        auto file = ResolveContentRootFile(fileSystem, path);
        if (!file) {
            return std::unexpected(std::move(file.error()));
        }
        const AssetKind kind = AssetKindOfFile(fileSystem, path);
        if (!IsChangeableAssetKind(kind)) {
            return std::unexpected(kind == AssetKind::None ? Format("'{}' is not an asset", path) : String("A config and a scene file stay as they are: code reads them by name"));
        }
        return AssetFileTarget{std::move(*file), kind, IsInALowerLayer(fileSystem, *content, kind)};
    }

    std::expected<AssetRename, String> CheckAssetRename(const FileSystem& fileSystem, StringView path, StringView newStem) {
        auto source = CheckAssetChange(fileSystem, path);
        if (!source) {
            return std::unexpected(std::move(source.error()));
        }
        const ContentPath content = *SplitContentPath(path);
        const bool isNode = source->kind == AssetKind::Node;
        if (!IsAllowedStem(newStem, !isNode)) {
            return std::unexpected(String(isNode ? "A node file's name is made of A-Z a-z 0-9 _ -" : "A name is made of A-Z a-z 0-9 _ - and single dots between them"));
        }
        const StringView oldName = PathFilename(path);
        const String newName = Format("{}{}", newStem, oldName.substr(AssetRenameStem(path).size()));
        if (EqualsIgnoreAsciiCase(oldName, newName)) {
            return std::unexpected(String(oldName == newName ? "The file has this name already" : "A name that differs only in case is the same file on macOS and Windows"));
        }
        const StringView folder = PathParent(content.relative);
        String newPath = Format("{}/{}", PathParent(path), newName);
        if (AssetKindOfName(newPath) != AssetKindOfName(path)) {
            return std::unexpected(Format("'{}' would be another kind of file", newName));
        }
        if (isNode) {
            const StringView ref = StringView(newPath).substr(content.mount.ToStringView().size() + 1, newPath.size() - content.mount.ToStringView().size() - 1 - PathExtension(newName).size());
            if (!IsValidNodeRef(ref)) {
                return std::unexpected(Format("'{}' is no node reference: the folders too are made of A-Z a-z 0-9 _ -", ref));
            }
        }
        if (auto free = CheckNameFree(fileSystem, content.mount, folder, newName); !free) {
            return std::unexpected(std::move(free.error()));
        }
        // A cooked node of the new reference would load beside the renamed file, which the loader refuses
        if (isNode) {
            if (auto free = CheckNameFree(fileSystem, content.mount, folder, CookedName(newName)); !free) {
                return std::unexpected(std::move(free.error()));
            }
        }
        Path target = source->file.Parent() / newName;
        return AssetRename{std::move(*source), std::move(target), std::move(newPath)};
    }

    std::expected<AssetFileChange, String> DeleteAsset(const FileSystem& fileSystem, StringView path) {
        const auto target = CheckAssetChange(fileSystem, path);
        if (!target) {
            return std::unexpected(target.error());
        }
        if (auto removed = RemoveFile(target->file.View()); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
        return AssetFileChange{String(), target->lowerLayerShows};
    }

    std::expected<AssetFileChange, String> RenameAsset(const FileSystem& fileSystem, StringView path, StringView newStem) {
        auto rename = CheckAssetRename(fileSystem, path, newStem);
        if (!rename) {
            return std::unexpected(std::move(rename.error()));
        }
        if (auto renamed = RenameFile(rename->source.file.View(), rename->target.View()); !renamed) {
            return std::unexpected(std::move(renamed.error()));
        }
        return AssetFileChange{std::move(rename->newPath), rename->source.lowerLayerShows};
    }

}  // namespace BECore
