#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/Singleton.h>
#include <cstdint>

namespace BECore {

    /// A panel asset of the window kind `Bar`: draws its elements into the window the caller has begun
    class IPanelBar {
    public:
        virtual ~IPanelBar() = default;

        /// Draws the bar into the current window and emits the click it noted after the walk. Main thread, inside the caller's frame
        virtual void DrawBar() = 0;
    };

    class PanelBarsRegistry;

    /**
     * @brief The registration of a bar; the bar is unregistered when the handle ends
     *
     * Move-only. A name registered again replaces the first, whose handle then unregisters nothing.
     */
    class PanelBarHandle {
    public:
        PanelBarHandle() = default;
        ~PanelBarHandle();

        PanelBarHandle(const PanelBarHandle&) = delete;
        PanelBarHandle& operator=(const PanelBarHandle&) = delete;
        PanelBarHandle(PanelBarHandle&& other) noexcept;
        PanelBarHandle& operator=(PanelBarHandle&& other) noexcept;

        /// Unregisters now (a no-op for an empty handle or one that a later registration replaced)
        void Reset();

        [[nodiscard]] explicit operator bool() const {
            return _token != 0;
        }

    private:
        friend class PanelBarsRegistry;
        PanelBarHandle(String name, uint64_t token) : _name(std::move(name)), _token(token) {}

        String _name;
        uint64_t _token = 0;
    };

    /**
     * @brief The bars of the editor by the name of their asset, found by the C++ windows that draw them
     *
     * A menu bar and a toolbar sit inside a window that C++ begins, so the asset cannot own a window for them: the widget of the asset registers itself here
     * and the window calls `Draw("<asset name>")` where the bar belongs (`BeginMenuBar` around it for menus). A name nobody registered draws nothing, so a game
     * whose `WidgetsConfig.xml` lacks the asset has an empty bar. Main thread only, so it takes no lock; names are held as text and never interned.
     */
    class PanelBarsRegistry : public Singleton<PanelBarsRegistry> {
    public:
        PanelBarsRegistry() = default;

        /// Makes @p bar the one `Draw(name)` finds; it must outlive the handle. A bar registered under the name before is replaced (logged once)
        [[nodiscard]] PanelBarHandle Register(StringView name, IPanelBar& bar);

        /// Draws the bar of @p name into the current window; false, and nothing drawn, when none is registered
        static bool Draw(StringView name);

    private:
        friend class PanelBarHandle;

        struct Entry {
            IPanelBar* bar = nullptr;
            uint64_t token = 0;
        };

        void Unregister(StringView name, uint64_t token);

        Map<String, Entry> _bars;
        uint64_t _nextToken = 1;
    };

}  // namespace BECore
