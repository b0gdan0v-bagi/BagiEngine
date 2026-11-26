#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>

namespace BECore {

    /// What the inspector's component list shows: the components of one node, of this process or of an attached one
    class IComponentListSource {
    public:
        virtual ~IComponentListSource() = default;

        [[nodiscard]] virtual size_t GetCount() const = 0;
        [[nodiscard]] virtual const char* GetTypeName(size_t index) const = 0;
        /// The whole list is drawn disabled
        [[nodiscard]] virtual bool IsReadOnly() const = 0;
        /// A header has a button that asks for its component to be removed
        [[nodiscard]] virtual bool IsRemovable() const = 0;
        /// The body of the open header of a component
        virtual void DrawFields(size_t index) = 0;
    };

    /**
     * @brief Draws a collapsing header per component with its fields under it into the current ImGui window. Needs a frame in progress
     *
     * @return the component whose remove button was pressed: the headers after it are not drawn in this frame, so the caller may remove it at once
     */
    [[nodiscard]] Optional<size_t> DrawComponentList(IComponentListSource& source);

}  // namespace BECore
