#pragma once

#include <BECore/Control/JsonMemberMap.h>
#include <BECore/File/Path.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// The editor's workspaces; the names are the keys of `workspaces.json`
    CORE_ENUM(EditorWorkspace, uint8_t, Scene, UI, Animation, Profiling)

    /// Where a ready layout docks a window: the middle of the dock space or a column beside it, each with a part below it
    CORE_ENUM(WorkspaceSlot, uint8_t, Left, LeftBottom, Center, CenterBottom, Right, RightBottom)

    /// An engine panel that a ready layout opens or closes
    struct WorkspacePanel {
        PoolString id;      // as registered in EditorPanelRegistry
        StringView window;  // the ImGui name its window is docked by
    };

    /// A panel a ready layout opens, and where
    struct WorkspaceDock {
        PoolString panel;
        WorkspaceSlot slot = WorkspaceSlot::Center;
    };

    /// The ready definition of a workspace. A column and a lower part exist only when a window is docked in them
    struct ReadyWorkspace {
        float rightWidth = 0.0f;          // of the dock space
        float leftWidth = 0.0f;           // of what the right column leaves
        float leftBottomHeight = 0.0f;    // of the left column
        float centerBottomHeight = 0.0f;  // of the middle
        float rightBottomHeight = 0.0f;   // of the right column
        Span<const WorkspaceDock> docks;  // every engine panel it opens; the others are closed
    };

    /// Which panels are open, by id
    using WorkspacePanelStates = HashMap<PoolString, bool>;

    /// What a workspace puts on screen
    struct WorkspaceState {
        String layout;              // ImGui's ini text; empty stands for the ready layout
        WorkspacePanelStates open;  // a panel it does not list stays as it is
    };

    /// Every engine panel a ready layout controls
    [[nodiscard]] Span<const WorkspacePanel> GetWorkspacePanels();

    [[nodiscard]] const ReadyWorkspace& GetReadyWorkspace(EditorWorkspace workspace);

    /// The ready definition as a state: no layout text, every engine panel listed
    [[nodiscard]] WorkspaceState MakeReadyState(EditorWorkspace workspace);

    /// Name of the user file in the editor folder (EditorUserDirectory())
    inline constexpr StringView kWorkspacesFileName = "workspaces.json";

    /**
     * @brief The active workspace and what the user left in each, stored per user in `workspaces.json`. No ImGui
     *
     * A call that changes what the screen must show (SwitchTo, Reset, LoadLayout, Restore) leaves the model **pending** until MarkApplied: the screen still
     * shows the old state then, so nothing it reports is stored.
     *
     * The file follows the rules of the other per-user files (EditorSettings): atomic writes, unknown keys kept and written back, a value of a wrong type gives
     * the default with a warning, a file that does not parse gives the defaults with one warning and is moved to `.bad` by the first Save, Load never writes.
     * Without a folder (an empty path) the model works in memory.
     */
    class Workspaces {
    public:
        explicit Workspaces(Path userDirectory);

        /// Reads the file; a missing one gives the Scene workspace with nothing stored
        void Load();

        /// False when nothing was written: no folder, or a failed write (logged)
        bool Save();

        [[nodiscard]] EditorWorkspace GetActive() const {
            return _active;
        }

        [[nodiscard]] bool IsPending() const {
            return _pending;
        }

        /// What the user left in @p workspace, or null
        [[nodiscard]] const WorkspaceState* FindStored(EditorWorkspace workspace) const;

        /**
         * @brief Makes @p target active and answers what to put on screen: its stored state, else the ready one
         *
         * @p shown, what is on screen now, is stored into the workspace left unless it is null or the model is pending (then only the target changes).
         * Nothing for the active workspace. A stored layout with no dock tree is a warning and the ready state.
         */
        [[nodiscard]] Optional<WorkspaceState> SwitchTo(EditorWorkspace target, const WorkspaceState* shown);

        /// Forgets what is stored of the active workspace and answers its ready state
        [[nodiscard]] WorkspaceState Reset();

        /// A saved layout becomes the active workspace's; the caller puts it on screen
        void LoadLayout(StringView text);

        /// What to put on screen for the active workspace, as SwitchTo answers it: for a start that has no dock tree
        [[nodiscard]] WorkspaceState Restore();

        /// The last answer is on screen
        void MarkApplied() {
            _pending = false;
        }

        /// Stores the screen's layout text as the active workspace's; nothing while pending
        void StoreLayout(StringView text);

        /**
         * @brief Notes whether @p panel is open on screen; true when that differs from what the active workspace had
         *
         * A panel noted for the first time is no change. Nothing while pending. Allocates only for a panel the workspace did not list.
         */
        [[nodiscard]] bool NoteOpen(PoolString panel, bool open);

    private:
        struct Record {
            Optional<WorkspaceState> state;
            JsonMemberMap extra;  // members of the workspace's object that this build does not read
        };

        [[nodiscard]] Record& RecordOf(EditorWorkspace workspace) {
            return _records[static_cast<size_t>(workspace)];
        }
        [[nodiscard]] const Record& RecordOf(EditorWorkspace workspace) const {
            return _records[static_cast<size_t>(workspace)];
        }
        /// The stored state of the active workspace, made empty when there is none
        [[nodiscard]] WorkspaceState& ActiveState();
        [[nodiscard]] WorkspaceState Answer(EditorWorkspace workspace) const;

        Path _directory;
        Array<Record, EnumUtils<EditorWorkspace>::Count()> _records;
        JsonMemberMap _extra;            // unknown members of the root
        JsonMemberMap _extraWorkspaces;  // workspaces this build does not know
        EditorWorkspace _active = EditorWorkspace::Scene;
        bool _pending = false;
        bool _unreadable = false;
    };

}  // namespace BECore
