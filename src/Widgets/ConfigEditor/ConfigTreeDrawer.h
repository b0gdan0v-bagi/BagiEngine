#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>

// Forward declaration - avoid including pugixml in header
namespace pugi {
    class xml_node;
}

namespace BECore {

    class StagedChanges;
    class XmlNode;

    /**
     * @brief Draws the XML tree of one config as ImGui tree nodes with a typed field inspector per attribute and text
     *
     * The part of the Config Editor that stays C++ (the slot `configTree`): the tree is as deep as the file and each leaf picks its control from the
     * field schema, which a format of elements cannot say. Edits go to the staged changes, never to the document. Every ImGui id comes from the path of
     * the node, so the ids of its controls sit under whatever the caller pushed.
     */
    class ConfigTreeDrawer {
    public:
        explicit ConfigTreeDrawer(StagedChanges& staged) : _staged(staged) {}

        /// Draws @p root, the document element of @p config; the node handles live for this call only
        void Draw(const XmlNode& root, PoolString config);

    private:
        void RenderXmlNode(pugi::xml_node node, const ArenaString& nodePath, PoolString config);

        StagedChanges& _staged;
    };

}  // namespace BECore
