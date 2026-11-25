#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UINodeFileSave.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore {

    class FileSystem;
    class Scene;
    class SceneNode;
    class UIElement;

    /// Where the edits of one node go on Save
    CORE_ENUM(UINodeFileTarget, uint8_t,
              Inline,  ///< the node is written in the scene file: the scene file holds its elements
              File,    ///< an instance of a node file that nothing else in the scene uses: the rect attributes are patched into the file
              Shared)  ///< a node file with several instances in the scene: the edit stays an override in the scene file

    /**
     * @brief The canvas elements the inspector changed since the last save, and the write of their rects into the node files
     *
     * The elements are held, so a tree edit cannot free one before the save; an element that is no longer in the scene is left out by the save itself. No ImGui.
     */
    class UINodeFileSession {
    public:
        struct Status {
            UINodeFileTarget target = UINodeFileTarget::Inline;
            PoolString ref;        ///< the node file; empty for Inline
            size_t instances = 0;  ///< the nodes of the scene that use @c ref
            size_t elements = 0;   ///< the pending elements of this node
        };

        /// Remembers @p element as changed; the same element twice is one
        void Note(IntrusivePtr<UIElement> element);

        [[nodiscard]] size_t PendingCount() const {
            return _elements.size();
        }

        /// What a Save would do with the pending elements of @p node
        [[nodiscard]] Status StatusFor(Scene& scene, const SceneNode& node) const;

        /**
         * @brief Writes the pending rects into the node files (resolved through the FileSystem like the Config editor does, written atomically), logs one warning per note and one line for the count,
         * forgets the elements
         *
         * Never stops the caller: a file that fails is a warning, and the scene is still saved by the caller.
         */
        UINodeFileReport Save(Scene& scene);

        /// Save() over @p fileSystem: reads the file where it resolves, writes it to `FileSystem::WriteTarget` (the content overlay, when one is set)
        UINodeFileReport Save(Scene& scene, const FileSystem& fileSystem);

        void Clear();

    private:
        Vector<IntrusivePtr<UIElement>> _elements;
    };

}  // namespace BECore
