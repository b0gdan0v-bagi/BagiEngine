#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/Singleton.h>
#include <Widgets/EditorLayout/EditorPlayModel.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <Widgets/EditorLayout/LayoutStore.h>
#include <Widgets/EditorLayout/Workspaces.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /**
     * @brief What the panels made of assets need from the layout widget: its per-user settings and layouts, the layout load and save, and the "preferences changed" call
     *
     * `EditorLayoutWidget` owns the `EditorSettings` and the `LayoutStore`, applies the font scale and tells the other panels what the
     * preferences hold; a model of a panel asset reaches it through `EditorLayoutHostRegistry` instead of a pointer to the widget, and a test
     * serves it with a fake. Main thread only.
     */
    class IEditorLayoutHost {
    public:
        virtual ~IEditorLayoutHost() = default;

        /// The preferences and the hotkeys; null when the host has none
        [[nodiscard]] virtual EditorSettings* GetSettings() = 0;

        /// The named layouts; null when the system gives no per-user folder
        [[nodiscard]] virtual LayoutStore* GetLayouts() = 0;

        /// The state behind the main toolbar's buttons; null when the host has none. The host owns it and changes it by the commands `game.play`, `game.pause`
        /// and `game.step`, so a model only reads it
        [[nodiscard]] virtual const EditorPlayModel* GetPlayModel() const = 0;

        /// The workspace the editor is in, also while it is still on its way to the screen; nothing when the host has none. The host changes it by the commands
        /// `workspace.*`, so a model only reads it
        [[nodiscard]] virtual Optional<EditorWorkspace> GetActiveWorkspace() const = 0;

        /// Writes the current dock layout under @p name. The error is a sentence for the dialog
        virtual std::expected<void, String> SaveCurrentLayoutAs(StringView name) = 0;

        /// Reads the saved layout @p name and applies it at the start of the next ImGui frame (a loaded ini cannot be swapped in the middle of one). A name that
        /// cannot be read, or a text with no dock tree, is a warning and the current layout stays
        virtual void LoadLayout(StringView name) = 0;

        /// The preferences of `GetSettings()` were changed by the caller: the host applies them (the font scale) and tells the panels, in its next
        /// `Update` at the latest, so the call may come every frame of a drag and costs a flag
        virtual void NotifyPreferencesChanged() = 0;
    };

    class EditorLayoutHostRegistry;

    /**
     * @brief The registration of a host; the host is unregistered when the handle ends
     *
     * Move-only. A host registered again replaces the first, whose handle then unregisters nothing.
     */
    class EditorLayoutHostHandle {
    public:
        EditorLayoutHostHandle() = default;
        ~EditorLayoutHostHandle();

        EditorLayoutHostHandle(const EditorLayoutHostHandle&) = delete;
        EditorLayoutHostHandle& operator=(const EditorLayoutHostHandle&) = delete;
        EditorLayoutHostHandle(EditorLayoutHostHandle&& other) noexcept;
        EditorLayoutHostHandle& operator=(EditorLayoutHostHandle&& other) noexcept;

        /// Unregisters now (a no-op for an empty handle or one that a later registration replaced)
        void Reset();

        [[nodiscard]] explicit operator bool() const {
            return _token != 0;
        }

    private:
        friend class EditorLayoutHostRegistry;
        explicit EditorLayoutHostHandle(uint64_t token) : _token(token) {}

        uint64_t _token = 0;
    };

    /// The one host of the editor, found by the models of the panel assets. Main thread only, so it takes no lock
    class EditorLayoutHostRegistry : public Singleton<EditorLayoutHostRegistry> {
    public:
        EditorLayoutHostRegistry() = default;

        /// Makes @p host the one models find; it must outlive the handle. A host registered before is replaced (logged once)
        [[nodiscard]] EditorLayoutHostHandle Register(IEditorLayoutHost& host);

        /// The registered host, or null
        [[nodiscard]] IEditorLayoutHost* Get() const {
            return _host;
        }

    private:
        friend class EditorLayoutHostHandle;

        void Unregister(uint64_t token);

        IEditorLayoutHost* _host = nullptr;
        uint64_t _token = 0;
        uint64_t _nextToken = 1;
    };

}  // namespace BECore
