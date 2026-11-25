#pragma once

#include <BECore/Types/Types.h>
#include <BECore/UI/UIPreview.h>
#include <BECore/Utils/Singleton.h>
#include <Widgets/Viewport/ViewportInput.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief The choices of the Viewport's toolbar as `ViewportWidget` reads them: the shape, the safe area, the overlays and the input mode
     *
     * The toolbar is a panel asset whose model owns the choices and lives inside its `AssetPanelWidget`, so the viewport reaches it through
     * `ViewportToolbarRegistry` instead of a pointer to another widget. Main thread only.
     */
    class IViewportToolbar {
    public:
        virtual ~IViewportToolbar() = default;

        /// The size, the safe area and the overlays the image is drawn with
        [[nodiscard]] virtual PreviewSettings GetPreview() const = 0;

        /// Where a press on the image goes
        [[nodiscard]] virtual ViewportInputMode GetInputMode() const = 0;
    };

    /// The choices of a viewport whose editor has no toolbar model (a game's own `WidgetsConfig.xml` without the asset line): every default
    class DefaultViewportToolbar : public IViewportToolbar {
    public:
        [[nodiscard]] PreviewSettings GetPreview() const override {
            return {};
        }

        [[nodiscard]] ViewportInputMode GetInputMode() const override {
            return ViewportInputMode::Play;
        }
    };

    class ViewportToolbarRegistry;

    /**
     * @brief The registration of a toolbar; it is unregistered when the handle ends
     *
     * Move-only. A toolbar registered again replaces the first, whose handle then unregisters nothing.
     */
    class ViewportToolbarHandle {
    public:
        ViewportToolbarHandle() = default;
        ~ViewportToolbarHandle();

        ViewportToolbarHandle(const ViewportToolbarHandle&) = delete;
        ViewportToolbarHandle& operator=(const ViewportToolbarHandle&) = delete;
        ViewportToolbarHandle(ViewportToolbarHandle&& other) noexcept;
        ViewportToolbarHandle& operator=(ViewportToolbarHandle&& other) noexcept;

        /// Unregisters now (a no-op for an empty handle or one that a later registration replaced)
        void Reset();

        [[nodiscard]] explicit operator bool() const {
            return _token != 0;
        }

    private:
        friend class ViewportToolbarRegistry;
        explicit ViewportToolbarHandle(uint64_t token) : _token(token) {}

        uint64_t _token = 0;
    };

    /// The one toolbar of the editor's viewport. Main thread only, so it takes no lock
    class ViewportToolbarRegistry : public Singleton<ViewportToolbarRegistry> {
    public:
        ViewportToolbarRegistry() = default;

        /// Makes @p toolbar the one the viewport reads; it must outlive the handle. A toolbar registered before is replaced (logged once)
        [[nodiscard]] ViewportToolbarHandle Register(IViewportToolbar& toolbar);

        /// The registered toolbar, or null
        [[nodiscard]] IViewportToolbar* Get() const {
            return _toolbar;
        }

        /// The registered toolbar, or the defaults when none is
        [[nodiscard]] IViewportToolbar& GetOrDefault() {
            return _toolbar != nullptr ? *_toolbar : _default;
        }

    private:
        friend class ViewportToolbarHandle;

        void Unregister(uint64_t token);

        IViewportToolbar* _toolbar = nullptr;
        DefaultViewportToolbar _default;
        uint64_t _token = 0;
        uint64_t _nextToken = 1;
    };

}  // namespace BECore
