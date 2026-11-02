#pragma once

#include <BECore/Resource/IResource.h>
#include <BECore/UI/UIScreenContent.h>

namespace BECore {

    /**
     * @brief A screen in a file of its own (`ui/hud.ui.xml`), loaded through ResourceManager like any other resource.
     *
     * The document keeps the parsed XML and nothing else. Every canvas that uses it builds its own elements with Instantiate(): the layout
     * results (`rect`, `active`) live in the elements, so two canvases must not share them. UICanvasComponent takes a document through its `source` field.
     *
     * Immutable after loading, safe to read from several threads.
     */
    class UIDocument : public IResource {
        BE_CLASS(UIDocument)
    public:
        UIDocument() : _config(XmlConfig::Create()) {}
        ~UIDocument() override = default;

        ResourceState GetState() const override;
        PoolString GetPath() const override;
        uint64_t GetMemoryUsage() const override;
        PoolString GetTypeName() const override;

        /**
         * @brief Builds new elements and variants from the file into @p content (replacing its lists).
         *
         * A part that cannot be read is logged with its attribute and line and left out or at its default, and the rest is still built.
         * @return true when the document is loaded and read without a single error
         */
        bool Instantiate(UIScreenContent& content) const;

        [[nodiscard]] static IntrusivePtr<UIDocument> Create();

    private:
        friend class UIDocumentLoader;

        void SetLoaded(PoolString path, XmlConfig config);
        void SetFailed(PoolString path);

        PoolString _path;
        XmlConfig _config;
        ResourceState _state = ResourceState::Unloaded;
    };

}  // namespace BECore
