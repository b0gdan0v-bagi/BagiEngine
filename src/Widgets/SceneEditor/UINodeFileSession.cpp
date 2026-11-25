#include "UINodeFileSession.h"

#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/UI/UIElement.h>

namespace BECore {

    namespace {

        Path NodeFilePath(const FileSystem& fileSystem, StringView ref) {
            const String virtualPath = Format("{}/{}{}", kNodeFilesMountPoint, ref, kNodeFileExtension);
            return fileSystem.ResolvePath(virtualPath);
        }

    }  // namespace

    void UINodeFileSession::Note(IntrusivePtr<UIElement> element) {
        if (element && eastl::find(_elements.begin(), _elements.end(), element) == _elements.end()) {
            _elements.push_back(eastl::move(element));
        }
    }

    UINodeFileSession::Status UINodeFileSession::StatusFor(Scene& scene, const SceneNode& node) const {
        Status status;
        status.ref = node.GetSourceRef();
        if (status.ref.Empty()) {
            return status;
        }
        status.instances = CountNodeInstances(scene, status.ref);
        status.target = status.instances > 1 ? UINodeFileTarget::Shared : UINodeFileTarget::File;
        status.elements = CountEditedElements(node, Span<const IntrusivePtr<UIElement>>(_elements.data(), _elements.size()));
        return status;
    }

    Vector<PoolString> UINodeFileSession::PendingFiles(Scene& scene) const {
        return NodeFilesToPatch(scene, Span<const IntrusivePtr<UIElement>>(_elements.data(), _elements.size()));
    }

    UINodeFileReport UINodeFileSession::Save(Scene& scene) {
        return Save(scene, CoreManager::GetFileSystem());
    }

    UINodeFileReport UINodeFileSession::Save(Scene& scene, const FileSystem& fileSystem) {
        // Named, because the FunctionRefs of `io` only point at them
        const auto read = [&fileSystem](StringView ref, String& text, String& error) {
            auto content = ReadTextFile(NodeFilePath(fileSystem, ref));
            if (!content) {
                error = content.error();
                return false;
            }
            text = eastl::move(*content);
            return true;
        };
        const auto write = [&fileSystem](StringView ref, StringView text, String& error) {
            const auto written = WriteFileAtomically(fileSystem.WriteTarget(NodeFilePath(fileSystem, ref)), text);
            if (!written) {
                error = written.error();
                return false;
            }
            return true;
        };
        const UINodeFileIo io{read, write};
        UINodeFileReport report = SaveUIRectsToNodeFiles(scene, Span<const IntrusivePtr<UIElement>>(_elements.data(), _elements.size()), io);
        for (const String& note : report.notes) {
            LOG_WARNING("SceneEditor: {}", note);
        }
        if (report.filesWritten > 0) {
            LOG_INFO("SceneEditor: wrote the rects of {} UI element(s) into {} node file(s)", report.elementsPatched, report.filesWritten);
        }
        _elements.clear();
        return report;
    }

    void UINodeFileSession::Clear() {
        _elements.clear();
    }

}  // namespace BECore
