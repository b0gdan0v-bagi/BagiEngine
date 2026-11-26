#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/Singleton.h>
#include <Widgets/EditorLayout/EditorPanelIds.h>
#include <cstdint>

namespace BECore {

    /// A docked editor panel as a workspace sees it: whether it is open, and a way to open or close it
    class IEditorPanel {
    public:
        virtual ~IEditorPanel() = default;

        [[nodiscard]] virtual bool IsOpen() const = 0;

        /// Opens or closes the panel at once, with no fade: the layout around it changes in the same frame
        virtual void SetOpen(bool open) = 0;
    };

    class EditorPanelRegistry;

    /// The registration of a panel; the panel is unregistered when the handle ends. Move-only
    class EditorPanelHandle {
    public:
        EditorPanelHandle() = default;
        ~EditorPanelHandle();

        EditorPanelHandle(const EditorPanelHandle&) = delete;
        EditorPanelHandle& operator=(const EditorPanelHandle&) = delete;
        EditorPanelHandle(EditorPanelHandle&& other) noexcept;
        EditorPanelHandle& operator=(EditorPanelHandle&& other) noexcept;

        /// Unregisters now (a no-op for an empty handle)
        void Reset();

        [[nodiscard]] explicit operator bool() const {
            return _token != 0;
        }

    private:
        friend class EditorPanelRegistry;
        explicit EditorPanelHandle(uint64_t token) : _token(token) {}

        uint64_t _token = 0;
    };

    /**
     * @brief The docked panels that are alive, found by the layout widget when it switches workspaces
     *
     * Floating windows, dialog hosts, bars and a game's debug panels do not register. Main thread only, so it takes no lock.
     */
    class EditorPanelRegistry : public Singleton<EditorPanelRegistry> {
    public:
        struct Entry {
            PoolString id;
            IEditorPanel* panel = nullptr;
            uint64_t token = 0;
        };

        EditorPanelRegistry() = default;

        /// Lists @p panel under @p id; it must outlive the handle
        [[nodiscard]] EditorPanelHandle Register(PoolString id, IEditorPanel& panel);

        /// The panel registered first under @p id, or null: the one whose open flag counts when two share an id
        [[nodiscard]] IEditorPanel* Find(PoolString id) const;

        /// Every registration, in the order they were made; valid until one is added or removed
        [[nodiscard]] Span<const Entry> GetEntries() const {
            return {_entries.data(), _entries.size()};
        }

    private:
        friend class EditorPanelHandle;

        void Unregister(uint64_t token);

        Vector<Entry> _entries;
        uint64_t _nextToken = 1;
    };

}  // namespace BECore
