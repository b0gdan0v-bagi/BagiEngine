#pragma once

#include <BECore/Types/Types.h>

// Forward declaration - avoid including pugixml in header
namespace pugi {
    class xml_node;
}

namespace BECore {

    class StagedChanges;
    class XmlNode;

    /**
     * @brief Draws the XML tree of one config as ImGui tree nodes with a text field per attribute and text
     *
     * The text mode of the Config Editor (the slot `configTree`): the tree is as deep as the file, which a format of elements cannot say. A value
     * is staged when the person confirms it with Enter; nothing goes to the document. Every ImGui id comes from the path of the node, so the ids of
     * its controls sit under whatever the caller pushed.
     */
    class ConfigTreeDrawer {
    public:
        explicit ConfigTreeDrawer(StagedChanges& staged) : _staged(staged) {}

        /// Draws @p root, the document element of a config; the node handles live for this call only
        void Draw(const XmlNode& root);

        /// Forgets what the text fields hold: for another document, or the same one read again
        void Clear() {
            _textBuffers.clear();
        }

    private:
        /// The longest value a field holds, with its terminator
        static constexpr size_t kTextBufferSize = 256;
        using TextBuffer = Array<char, kTextBufferSize>;

        void RenderXmlNode(pugi::xml_node node, const ArenaString& nodePath);

        /// A text field under the id @p key, filled with @p value when first drawn; the entered text on the frame Enter confirms it, null otherwise
        const char* InputValue(StringView key, StringView value);

        StagedChanges& _staged;
        HashMap<String, TextBuffer> _textBuffers;  // by key: a field keeps what is typed into it between frames
    };

}  // namespace BECore
