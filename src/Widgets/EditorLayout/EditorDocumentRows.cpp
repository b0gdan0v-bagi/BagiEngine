#include "EditorDocumentRows.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Scene/SceneNodeFiles.h>

namespace BECore {

    namespace {

        /// The virtual path of a file of the config mount point: below its deepest layer, as the file watcher reports it
        String ConfigVirtualPath(const FileSystem& fileSystem, const Path& realFile) {
            const Path file = AbsoluteNormalPath(realFile);
            const Path* best = nullptr;
            const Vector<Path> layers = fileSystem.GetMountLayers(MountPoints::config);
            for (const Path& layer : layers) {
                if (file.IsUnder(layer) && !file.Relative(layer).Empty() && (best == nullptr || layer.View().size() > best->View().size())) {
                    best = &layer;
                }
            }
            if (best == nullptr) {
                return {};
            }
            return Format("{}/{}", PoolString(MountPoints::config).ToStringView(), file.Relative(*best).View());
        }

        EditorDocumentState MakeRow(EditorDocumentKind kind, StringView name, bool dirty) {
            EditorDocumentState state;
            state.kind = kind;
            state.name = String(name);
            state.dirty = dirty;
            state.id = Format("{}:{}", EditorDocumentKindName(kind).ToStringView(), name);
            return state;
        }

    }  // namespace

    EditorDocumentState MakeConfigDocument(const ConfigManager& manager, EditorDocumentKind kind, StringView name, bool dirty, PoolString config) {
        EditorDocumentState state = MakeRow(kind, name, dirty);
        const Path file = manager.GetConfigFilePath(config);
        if (!file.Empty()) {
            const FileSystem& fileSystem = manager.GetFileSystem();
            state.path = ConfigVirtualPath(fileSystem, file);
            state.file = String(fileSystem.WriteTarget(file).View());
        }
        return state;
    }

    EditorDocumentState MakeNodeFileDocument(const FileSystem& fileSystem, PoolString ref) {
        EditorDocumentState state = MakeRow(EditorDocumentKind::NodeFile, ref.ToStringView(), true);
        state.path = Format("{}/{}{}", kNodeFilesMountPoint, ref.ToStringView(), kNodeFileExtension);
        const Path resolved = fileSystem.ResolvePath(state.path);
        if (!resolved.Empty()) {
            state.file = String(fileSystem.WriteTarget(resolved).View());
        }
        return state;
    }

}  // namespace BECore
