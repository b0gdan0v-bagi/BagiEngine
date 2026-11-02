#include "UIWebDocumentLoader.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIDocument.h>
#include <BECore/UI/UIWebCompiler.h>
#include <Generated/UIWebDocumentLoader.gen.hpp>
#include <TaskSystem/Awaitables.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view HtmlExtension = ".ui.html";
        constexpr eastl::string_view CurrentDirectoryPrefix = "./";

        std::optional<eastl::string> ReadVirtualFile(eastl::string_view virtualPath) {
            const std::filesystem::path resolved = CoreManager::GetFileSystem().ResolvePath(virtualPath);
            if (resolved.empty()) {
                return std::nullopt;
            }
            std::ifstream stream(resolved, std::ios::binary);
            if (!stream) {
                return std::nullopt;
            }
            std::ostringstream buffer;
            buffer << stream.rdbuf();
            const std::string text = buffer.str();
            return eastl::string(text.data(), text.size());
        }

        /// The path of @p href next to the document at @p documentPath
        eastl::string SiblingPath(eastl::string_view documentPath, eastl::string_view href) {
            while (href.substr(0, CurrentDirectoryPrefix.size()) == CurrentDirectoryPrefix) {
                href.remove_prefix(CurrentDirectoryPrefix.size());
            }
            const size_t slash = documentPath.rfind('/');
            if (slash == eastl::string_view::npos) {
                return eastl::string(href);
            }
            return Format("{}/{}", eastl::string(documentPath.substr(0, slash)).c_str(), eastl::string(href).c_str());
        }

    }  // namespace

    bool UIWebDocumentLoader::CanLoad(eastl::string_view extension) const {
        return extension == HtmlExtension;
    }

    Task<IntrusivePtr<IResource>> UIWebDocumentLoader::LoadAsync(PoolString path) {
        co_await SwitchToBackground();
        co_return LoadInternal(path);
    }

    IntrusivePtr<IResource> UIWebDocumentLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    IntrusivePtr<UIDocument> UIWebDocumentLoader::LoadInternal(PoolString path) {
        auto document = UIDocument::Create();

        const auto html = ReadVirtualFile(path.ToStringView());
        if (!html) {
            LOG_ERROR("UIWebDocumentLoader: cannot load '{}'"_format(path.ToStringView()));
            document->SetFailed(path);
            return document;
        }

        UIWebReport report;
        const eastl::string xml = UIWebCompiler::Compile(*html, [&](eastl::string_view href) { return ReadVirtualFile(SiblingPath(path.ToStringView(), href)); }, report);
        for (const UIWebIssue& issue : report.issues) {
            LOG_WARNING("UIWebDocumentLoader: '{}' {} line {}: {}"_format(path.ToStringView(), issue.source.c_str(), issue.line, issue.message.c_str()));
        }

        auto config = XmlConfig::Create();
        if (!config.LoadFromString(xml)) {
            LOG_ERROR("UIWebDocumentLoader: the compiled '{}' is not valid XML"_format(path.ToStringView()));
            document->SetFailed(path);
            return document;
        }

        document->SetLoaded(path, std::move(config));
        LOG_INFO("UIWebDocumentLoader: loaded '{}' ({} elements, {} variants, {} issues)"_format(path.ToStringView(), report.elements, report.variants, report.issues.size()));
        return document;
    }

}  // namespace BECore
