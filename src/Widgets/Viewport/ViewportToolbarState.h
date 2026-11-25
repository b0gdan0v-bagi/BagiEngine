#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIPreview.h>
#include <BECore/Utils/Singleton.h>
#include <Widgets/Viewport/ViewportInput.h>
#include <cstdint>

namespace BECore {

    class Scene;
    class SceneNode;

    /// The devices of `UIPreview::GetDevices()` the shapes overlay can check: the width of the mask of `IViewportToolbar::GetShapeMask()`
    inline constexpr size_t kMaxShapeDevices = 64;

    /// A canvas the viewport's pass found, for its switch in the Overlays popup. The node is compared, never dereferenced, by whoever keeps it
    struct CanvasSwitchRow {
        const SceneNode* node = nullptr;
        PoolString name;
    };

    /**
     * @brief The choices of the Viewport's toolbar as `ViewportWidget` reads them (the shape, the safe area, the overlays, the input mode, the forced view,
     * the per-canvas switches, the shapes of other devices), and what only the viewport knows, which it publishes back (the camera, the canvases)
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

        /// The view variant of @p scene the viewport's pass forces (`Scene::SetViewOverride`): -1 the default view, `i` the variant `i`. None (Auto) for a
        /// choice made in another scene or an index @p scene lacks
        [[nodiscard]] virtual Optional<int32_t> GetForcedView(const Scene* scene) const = 0;

        /// The image's size on the screen in percent of the target and whether the editor camera left its fit, for the zoom text and Reset. Called every frame
        virtual void PublishCamera(float zoomPercent, bool moved) = 0;

        /// The canvases of @p scene the pass found, in the order of the scene, for their switches. A list of another scene than the last one forgets the switches
        virtual void PublishCanvases(const Scene* scene, Span<const CanvasSwitchRow> canvases) = 0;

        /// False for a canvas whose node the user switched off; true for any node the toolbar does not know
        [[nodiscard]] virtual bool IsCanvasEnabled(const SceneNode* node) const = 0;

        /// Bit i: the device i of `UIPreview::GetDevices()` is checked in the shapes overlay
        [[nodiscard]] virtual uint64_t GetShapeMask() const = 0;

        /// The shapes overlay lays the devices out in landscape
        [[nodiscard]] virtual bool IsLandscapeShapes() const = 0;

        /// The user closed the "UI Findings" window, which is the Findings overlay: it is switched off as if its box were cleared
        virtual void ClearFindingsOverlay() = 0;
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

        [[nodiscard]] Optional<int32_t> GetForcedView(const Scene* /*scene*/) const override {
            return std::nullopt;
        }

        void PublishCamera(float /*zoomPercent*/, bool /*moved*/) override {}

        void PublishCanvases(const Scene* /*scene*/, Span<const CanvasSwitchRow> /*canvases*/) override {}

        [[nodiscard]] bool IsCanvasEnabled(const SceneNode* /*node*/) const override {
            return true;
        }

        [[nodiscard]] uint64_t GetShapeMask() const override {
            return 0;
        }

        [[nodiscard]] bool IsLandscapeShapes() const override {
            return false;
        }

        void ClearFindingsOverlay() override {}
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
