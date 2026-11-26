#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    class Scene;
    class SceneNode;
    class SceneSelection;
    struct HierarchyState;

    /// A row of one source: only the source that gave it reads it, and it dies with the tree it was taken from
    struct HierarchyRow {
        void* handle = nullptr;
    };

    /**
     * @brief The tree the hierarchy panel walks, searches and filters: the local scene, or the tree of another program
     *
     * The panel asks at every frame, so no method but CollectComponentTypes() and Click() allocates.
     */
    class IHierarchySource {
    public:
        virtual ~IHierarchySource() = default;

        [[nodiscard]] virtual HierarchyRow GetRoot() const = 0;
        [[nodiscard]] virtual size_t GetChildCount(HierarchyRow row) const = 0;
        [[nodiscard]] virtual HierarchyRow GetChild(HierarchyRow row, size_t index) const = 0;
        [[nodiscard]] virtual const char* GetName(HierarchyRow row) const = 0;
        [[nodiscard]] virtual const char* GetIcon(HierarchyRow row) const = 0;
        /// What names the row to ImGui below the rows above it: the same for the same node from frame to frame, so an open row stays open
        [[nodiscard]] virtual const void* GetId(HierarchyRow row) const = 0;
        /// The row's own hidden flag; the panel dims what is below it too
        [[nodiscard]] virtual bool IsHidden(HierarchyRow row) const = 0;
        [[nodiscard]] virtual bool HasComponent(HierarchyRow row, StringView type) const = 0;
        /// Replaces @p out with the component types of the tree, sorted and without repeats; copies, which outlive the tree
        virtual void CollectComponentTypes(Vector<String>& out) const = 0;
        [[nodiscard]] virtual bool IsSelected(HierarchyRow row) const = 0;
        /// @p modifiers are the kSelectionModifier* bits held with the click
        virtual void Click(HierarchyRow row, uint32_t modifiers) = 0;
        /// The node the panel may change (rename, drag, the context menu, the eye and the lock); null for a row that cannot be changed here
        [[nodiscard]] virtual SceneNode* EditableNode(HierarchyRow row) const = 0;

        /// The tree lacks nodes of the scene it shows
        [[nodiscard]] virtual bool IsTruncated() const {
            return false;
        }
    };

    /// The tree of a scene of this process. A click is left in the HierarchyState for the widget that owns the selection
    class LocalHierarchySource final : public IHierarchySource {
    public:
        LocalHierarchySource(Scene& scene, HierarchyState& state, const SceneSelection& selection) : _scene(scene), _state(state), _selection(selection) {}

        [[nodiscard]] static HierarchyRow RowOf(SceneNode& node) {
            return {&node};
        }

        [[nodiscard]] HierarchyRow GetRoot() const override;
        [[nodiscard]] size_t GetChildCount(HierarchyRow row) const override;
        [[nodiscard]] HierarchyRow GetChild(HierarchyRow row, size_t index) const override;
        [[nodiscard]] const char* GetName(HierarchyRow row) const override;
        [[nodiscard]] const char* GetIcon(HierarchyRow row) const override;
        [[nodiscard]] const void* GetId(HierarchyRow row) const override;
        [[nodiscard]] bool IsHidden(HierarchyRow row) const override;
        [[nodiscard]] bool HasComponent(HierarchyRow row, StringView type) const override;
        void CollectComponentTypes(Vector<String>& out) const override;
        [[nodiscard]] bool IsSelected(HierarchyRow row) const override;
        void Click(HierarchyRow row, uint32_t modifiers) override;
        [[nodiscard]] SceneNode* EditableNode(HierarchyRow row) const override;

    private:
        Scene& _scene;
        HierarchyState& _state;
        const SceneSelection& _selection;
    };

}  // namespace BECore
