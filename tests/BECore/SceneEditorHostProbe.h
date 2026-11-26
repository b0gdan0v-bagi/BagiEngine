#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <Widgets/SceneEditor/ISceneEditorHost.h>
#include <cstdint>

namespace BECore::Tests {

    /// An ISceneEditorHost whose inspected node is a plain value the test sets; the edits are recorded and applied to it as the widget would (none for a locked node)
    class ProbeSceneEditorHost : public ISceneEditorHost {
    public:
        [[nodiscard]] SceneInspectedNode GetInspected() const override {
            return inspected;
        }

        void RenameInspected(StringView name) override {
            renames.emplace_back(name.data(), name.size());
            if (inspected.hasSelection && !inspected.locked) {
                inspected.name = PoolString::Intern(name).ToStringView();
                inspected.dirty = true;
            }
        }

        void SetInspectedLayer(int32_t layer) override {
            layers.push_back(layer);
            if (inspected.hasSelection && !inspected.locked) {
                inspected.layer = layer;
                inspected.dirty = true;
            }
        }

        /// Makes @p name the one selected node of a scene with no unsaved changes
        void SelectOne(StringView name) {
            inspected = SceneInspectedNode{};
            inspected.hasScene = true;
            inspected.hasSelection = true;
            inspected.selectionCount = 1;
            inspected.name = PoolString::Intern(name).ToStringView();
        }

        SceneInspectedNode inspected;
        /// Every call, in order, also the ones the host refused
        Vector<String> renames;
        Vector<int32_t> layers;
    };

}  // namespace BECore::Tests
