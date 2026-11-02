#pragma once

#include <BECore/Resource/IResourceLoader.h>

namespace BECore {

    class UIDocument;

    /**
     * @brief Loads `.ui.html` files, the web-style front-end, into UIDocument resources.
     *
     * The HTML and the style sheets it links (`<link rel="stylesheet" href="hud.css">`, relative to the HTML file) are compiled to the XML of a `.ui.xml`
     * document by UIWebCompiler, and the document keeps that XML, so a canvas cannot tell which front-end wrote it. What the compile left out is logged as
     * warnings with the file and line. Reads text only (no element is created and no GPU is touched), so it may run on a pool thread.
     */
    class UIWebDocumentLoader : public IResourceLoader {
        BE_CLASS(UIWebDocumentLoader)
    public:
        UIWebDocumentLoader() = default;
        ~UIWebDocumentLoader() override = default;

        bool CanLoad(eastl::string_view extension) const override;
        Task<IntrusivePtr<IResource>> LoadAsync(PoolString path) override;
        IntrusivePtr<IResource> LoadSync(PoolString path) override;

    private:
        IntrusivePtr<UIDocument> LoadInternal(PoolString path);
    };

}  // namespace BECore
