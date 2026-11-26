#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/Singleton.h>
#include <cstdint>

namespace BECore {

    /// What the inspector's frame shows of the node the Scene Editor inspects: a copy, valid until the selection or the node changes
    struct SceneInspectedNode {
        bool hasScene = false;
        /// A primary node is selected and alive in the active scene's tree; the fields below about the node are defaults otherwise
        bool hasSelection = false;
        size_t selectionCount = 0;
        bool locked = false;
        /// Read-only on their own, besides `locked`: the node's name, its layer, and what it holds (its components)
        bool nameReadOnly = false;
        bool layerReadOnly = false;
        bool structureReadOnly = false;
        /// A text of this process or of the host: it lasts as long as the copy does (a node of an attached program is not interned)
        StringView name;
        int32_t layer = 0;
        /// The node file the node is an instance of; empty for a node written in the scene file
        PoolString sourceRef;
        /// The scene shown has unsaved changes: the active one, or in remote mode the attached program's
        bool dirty = false;
        /// The code and the message of the last edit an attached program refused; empty when it refused none
        StringView refusalCode;
        StringView refusalMessage;
    };

    /**
     * @brief What the panel assets of the Scene Editor need of `SceneEditorWidget`: the inspected node and the two edits of its frame
     *
     * The widget owns the selection and the unsaved flag; `SceneEditorWidget::Initialize` registers it in `SceneEditorHostRegistry` for its lifetime. A model
     * asks the registry for the host in each call and keeps neither the host nor a node between calls (the node may die with the next tree edit), and a
     * test serves it with a fake (`tests/BECore/SceneEditorHostProbe.h`). Main thread only.
     */
    class ISceneEditorHost {
    public:
        virtual ~ISceneEditorHost() = default;

        /// The primary node of the selection, checked to be alive in the active scene at this call
        [[nodiscard]] virtual SceneInspectedNode GetInspected() const = 0;

        /// Renames the inspected node and marks the scene unsaved; nothing for no selection, a locked node or a read-only name
        virtual void RenameInspected(StringView name) = 0;

        /// Sets the draw layer of the inspected node and marks the scene unsaved; nothing for no selection, a locked node or a read-only layer
        virtual void SetInspectedLayer(int32_t layer) = 0;
    };

    class SceneEditorHostRegistry;

    /**
     * @brief The registration of a host; the host is unregistered when the handle ends
     *
     * Move-only. A host registered again replaces the first, whose handle then unregisters nothing.
     */
    class SceneEditorHostHandle {
    public:
        SceneEditorHostHandle() = default;
        ~SceneEditorHostHandle();

        SceneEditorHostHandle(const SceneEditorHostHandle&) = delete;
        SceneEditorHostHandle& operator=(const SceneEditorHostHandle&) = delete;
        SceneEditorHostHandle(SceneEditorHostHandle&& other) noexcept;
        SceneEditorHostHandle& operator=(SceneEditorHostHandle&& other) noexcept;

        /// Unregisters now (a no-op for an empty handle or one that a later registration replaced)
        void Reset();

        [[nodiscard]] explicit operator bool() const {
            return _token != 0;
        }

    private:
        friend class SceneEditorHostRegistry;
        explicit SceneEditorHostHandle(uint64_t token) : _token(token) {}

        uint64_t _token = 0;
    };

    /// The one Scene Editor of the editor, found by the models of its panel assets. Main thread only, so it takes no lock
    class SceneEditorHostRegistry : public Singleton<SceneEditorHostRegistry> {
    public:
        SceneEditorHostRegistry() = default;

        /// Makes @p host the one models find; it must outlive the handle. A host registered before is replaced (logged once)
        [[nodiscard]] SceneEditorHostHandle Register(ISceneEditorHost& host);

        /// The registered host, or null
        [[nodiscard]] ISceneEditorHost* Get() const {
            return _host;
        }

    private:
        friend class SceneEditorHostHandle;

        void Unregister(uint64_t token);

        ISceneEditorHost* _host = nullptr;
        uint64_t _token = 0;
        uint64_t _nextToken = 1;
    };

}  // namespace BECore
