#include "UIWebDocumentLoader.h"

#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIDocument.h>
#include <BECore/UI/UIWebCompiler.h>
#include <Generated/UIWebDocumentLoader.gen.hpp>
#include <TaskSystem/Awaitables.h>

namespace BECore {

    namespace {

        constexpr StringView HtmlExtension = ".ui.html";

        Optional<String> ReadVirtualFile(StringView virtualPath) {
            const Path resolved = CoreManager::GetFileSystem().ResolvePath(virtualPath);
            if (resolved.Empty()) {
                return std::nullopt;
            }
            auto text = ReadTextFile(resolved);
            if (!text) {
                return std::nullopt;
            }
            return std::move(*text);
        }

        /**
         * The path of @p href next to the document at @p documentPath, lexically normal: `FileSystem` refuses a `..` in a virtual path, so
         * only one that climbs above the first name is left for it. Empty for an absolute @p href that no folder of the document precedes:
         * a reference of a document never names a real file.
         */
        String SiblingPath(StringView documentPath, StringView href) {
            const size_t slash = documentPath.rfind('/');
            if (slash == StringView::npos && PathIsAbsolute(href)) {
                return {};
            }
            const String joined = slash == StringView::npos ? String(href) : Format("{}/{}", documentPath.substr(0, slash), href);
            return String(Path(joined).LexicallyNormal().View());
        }

    }  // namespace

    bool UIWebDocumentLoader::CanLoad(StringView extension) const {
        return extension == HtmlExtension;
    }

    Task<IntrusivePtr<IResource>> UIWebDocumentLoader::LoadAsync(PoolString path) {
        co_await SwitchToBackground(TaskPriority::Normal, BE_TASK_LABEL("Load web UI document"));
        co_return LoadInternal(path);
    }

    IntrusivePtr<IResource> UIWebDocumentLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    IntrusivePtr<UIDocument> UIWebDocumentLoader::LoadInternal(PoolString path) {
        auto document = UIDocument::Create();

        const auto html = ReadVirtualFile(path.ToStringView());
        if (!html) {
            LOG_ERROR("UIWebDocumentLoader: cannot load '{}'", path.ToStringView());
            document->SetFailed(path);
            return document;
        }

        UIWebReport report;
        const String xml = UIWebCompiler::Compile(*html, [&](StringView href) { return ReadVirtualFile(SiblingPath(path.ToStringView(), href)); }, report);
        for (const UIWebIssue& issue : report.issues) {
            LOG_WARNING("UIWebDocumentLoader: '{}' {} line {}: {}", path.ToStringView(), issue.source, issue.line, issue.message);
        }

        auto config = XmlConfig::Create();
        if (!config.LoadFromString(xml)) {
            LOG_ERROR("UIWebDocumentLoader: the compiled '{}' is not valid XML", path.ToStringView());
            document->SetFailed(path);
            return document;
        }

        document->SetLoaded(path, std::move(config));
        LOG_INFO("UIWebDocumentLoader: loaded '{}' ({} elements, {} variants, {} issues)", path.ToStringView(), report.elements, report.variants, report.issues.size());
        return document;
    }

}  // namespace BECore
