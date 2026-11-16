#include "UINodeFileSession.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/UI/UIElement.h>
#include <Widgets/EditorLayout/UserFileIo.h>

namespace BECore {

    namespace {

        std::filesystem::path NodeFilePath(StringView ref) {  //-NoStd: the path of the file system's resolver
            const String virtualPath = Format("{}/{}{}", kNodeFilesMountPoint, ref, kNodeFileExtension);
            return CoreManager::GetFileSystem().ResolvePath(virtualPath);
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

    UINodeFileReport UINodeFileSession::Save(Scene& scene) {
        // Named, because the FunctionRefs of `io` only point at them
        const auto read = [](StringView ref, String& text, String& error) {
            auto content = ReadTextFile(NodeFilePath(ref));
            if (!content) {
                error = content.error();
                return false;
            }
            text = eastl::move(*content);
            return true;
        };
        const auto write = [](StringView ref, StringView text, String& error) {
            const auto written = WriteTextFileAtomically(NodeFilePath(ref), text);
            if (!written) {
                error = written.error();
                return false;
            }
            return true;
        };
        const UINodeFileIo io{read, write};
        UINodeFileReport report = SaveUIRectsToNodeFiles(scene, Span<const IntrusivePtr<UIElement>>(_elements.data(), _elements.size()), io);
        for (const String& note : report.notes) {
            LOG_WARNING(Format("SceneEditor: {}", note).c_str());
        }
        if (report.filesWritten > 0) {
            LOG_INFO(Format("SceneEditor: wrote the rects of {} UI element(s) into {} node file(s)", report.elementsPatched, report.filesWritten).c_str());
        }
        _elements.clear();
        return report;
    }

    void UINodeFileSession::Clear() {
        _elements.clear();
    }

}  // namespace BECore
