#include "SceneNodeInstance.h"

#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/XmlDeserializer.h>

namespace BECore {

    IntrusivePtr<SceneNode> DeserializeNode(pugi::xml_node nodeEl, eastl::string_view what) {
        auto node = New<SceneNode>();
        XmlDeserializer deserializer;
        if (deserializer.LoadFromXmlNode(XmlNode{nodeEl})) {
            node->Deserialize(deserializer);
            if (deserializer.HasErrors()) {
                LogDeserializeErrors(what, deserializer);
            }
        }
        return node;
    }

}  // namespace BECore
