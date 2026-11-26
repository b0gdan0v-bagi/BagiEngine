#pragma once

#include <Widgets/SceneEditor/HierarchySource.h>

namespace BECore {

    class RemoteSession;
    struct RemoteTreeNode;

    /**
     * @brief The tree of the program a RemoteSession is attached to, for the hierarchy panel: read-only, its selection the session's
     *
     * The root row is made here, as `scene_tree` lists only what is below it; no scene method names it, so a click on it selects nothing.
     * A row is named to ImGui by its path, so open rows stay open over the reads of the tree. An icon is that of the class of the same
     * name in this program.
     */
    class RemoteHierarchySource final : public IHierarchySource {
    public:
        explicit RemoteHierarchySource(RemoteSession& session) : _session(session) {}

        /// Asks the walk to open the rows above the node at @p path and to scroll to its row; that row, or nothing when the tree does not list the node.
        /// The tree must not be read anew while the source is used
        Optional<HierarchyRow> Reveal(StringView path);

        [[nodiscard]] HierarchyRow GetRoot() const override {
            return {};
        }

        [[nodiscard]] size_t GetChildCount(HierarchyRow row) const override;
        [[nodiscard]] HierarchyRow GetChild(HierarchyRow row, size_t index) const override;
        [[nodiscard]] const char* GetName(HierarchyRow row) const override;
        [[nodiscard]] const char* GetIcon(HierarchyRow row) const override;
        [[nodiscard]] const void* GetId(HierarchyRow row) const override;

        [[nodiscard]] bool IsHidden(HierarchyRow /*row*/) const override {
            return false;
        }

        [[nodiscard]] bool HasComponent(HierarchyRow row, StringView type) const override;
        void CollectComponentTypes(Vector<String>& out) const override;
        [[nodiscard]] bool IsSelected(HierarchyRow row) const override;
        void Click(HierarchyRow row, uint32_t modifiers) override;

        [[nodiscard]] SceneNode* EditableNode(HierarchyRow /*row*/) const override {
            return nullptr;
        }

        [[nodiscard]] bool IsOnRevealPath(HierarchyRow row) const override;

        [[nodiscard]] bool IsRevealTarget(HierarchyRow row) const override {
            return _revealed != nullptr && row.handle == _revealed;
        }

        [[nodiscard]] bool IsTruncated() const override;

    private:
        RemoteSession& _session;
        const RemoteTreeNode* _revealed = nullptr;
    };

}  // namespace BECore
