#include "SceneNodeInstance.h"

#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlFieldDiff.h>
#include <BECore/Reflection/XmlSerializer.h>

namespace BECore {

    namespace {

        constexpr StringView kChildrenElementName = "children";
        constexpr StringView kNodeTypeAttribute = "type";
        constexpr StringView kNodeNameAttribute = "name";

        /// What a node or a component serializes to: its type and the XML of its fields
        struct SerializedFields {
            String type;
            pugi::xml_document document;

            /// The element that holds the attributes and child elements of the fields
            pugi::xml_node Root() const {
                return document.document_element();
            }

            bool operator==(const SerializedFields& other) const {
                return type == other.type && XmlFieldDiff::CompactText(Root()) == XmlFieldDiff::CompactText(other.Root());
            }
        };

        /// Writes a node without its two tree lists, which AppendNodeElement writes in a shape of its own: no subtree is serialized once per node above it
        class NodeFieldsSerializer final : public XmlSerializer {
        public:
            bool BeginArray(StringView name, StringView elementName, size_t& count) override {
                if (IsAtRoot() && (name == kComponentsElementName || name == kChildrenElementName)) {
                    return false;
                }
                return XmlSerializer::BeginArray(name, elementName, count);
            }
        };

        void SerializeFields(const IComponent& component, SerializedFields& out) {
            out.type = component.GetTypeMeta().typeName.data();
            XmlSerializer serializer;
            component.Serialize(serializer);
            out.document.append_copy(serializer.GetRootNode());
        }

        /// The own fields of @p node: what its `Serialize` writes but the tree lists and `sourceRef` (a node file link is not part of the node's data)
        void SerializeFields(const SceneNode& node, SerializedFields& out) {
            out.type = node.GetTypeMeta().typeName.data();
            NodeFieldsSerializer serializer;
            node.Serialize(serializer);
            out.document.append_copy(serializer.GetRootNode()).remove_attribute(kNodeSourceAttribute.data());
        }

        Vector<SerializedFields> SerializeComponents(const SceneNode& node) {
            Vector<SerializedFields> result(node.GetComponents().size());
            for (size_t i = 0; i < result.size(); ++i) {
                SerializeFields(*node.GetComponents()[i], result[i]);
            }
            return result;
        }

        /// False when @p live differs from the node file in what a reference cannot override: the type, or an own field outside kNodeOverrideAttributes
        bool CanOverrideOwnFields(const SerializedFields& live, const SerializedFields& base, String& reason) {
            if (live.type != base.type) {
                reason = Format("its type '{}' is not the '{}' of the node file", live.type, base.type);
                return false;
            }
            for (const String& name : XmlFieldDiff::FieldNames(live.Root(), base.Root())) {
                const bool overridable = eastl::find(kNodeOverrideAttributes.begin(), kNodeOverrideAttributes.end(), StringView{name.c_str(), name.size()}) != kNodeOverrideAttributes.end();
                if (!overridable && XmlFieldDiff::ReadField(live.Root(), name.c_str()) != XmlFieldDiff::ReadField(base.Root(), name.c_str())) {
                    reason = Format("its field '{}' differs from the node file and a reference cannot override it", name);
                    return false;
                }
            }
            return true;
        }

        /// Writes the fields of @p live that differ from @p base into @p componentEl. False when a field is written in both spellings
        bool WriteFieldOverrides(pugi::xml_node live, pugi::xml_node base, pugi::xml_node componentEl, String& reason) {
            for (const String& name : XmlFieldDiff::FieldNames(live, base)) {
                const XmlFieldDiff::Field liveField = XmlFieldDiff::ReadField(live, name.c_str());
                if (liveField == XmlFieldDiff::ReadField(base, name.c_str())) {
                    continue;
                }
                if (liveField.hasAttribute && liveField.hasElements) {
                    reason = Format("field '{}' is written as an attribute and as an element", name);
                    return false;
                }
                if (liveField.hasAttribute) {
                    componentEl.append_attribute(name.c_str()).set_value(liveField.attribute.c_str());
                } else if (liveField.hasElements) {
                    for (pugi::xml_node child = live.child(name.c_str()); child; child = child.next_sibling(name.c_str())) {
                        componentEl.append_copy(child);
                    }
                } else {
                    // The baseline has it and the serializer left it out of the live node (a list that was emptied): an empty element replaces it
                    componentEl.append_child(name.c_str());
                }
            }
            return true;
        }

        /// True when @p other has the type, the own fields and the components of the live node (the round trip check)
        bool SameAsLive(const SerializedFields& liveNode, const Vector<SerializedFields>& liveComponents, const SceneNode& other) {
            SerializedFields otherNode;
            SerializeFields(other, otherNode);
            return liveNode == otherNode && liveComponents == SerializeComponents(other);
        }

        /// The serialized fields of a freshly made object per type name: the state the loader builds it in before it reads its fields.
        /// A null entry is a type the factory cannot create
        using DefaultFieldsCache = HashMap<String, UniquePtr<SerializedFields>>;

        /// One per call of the writers, not global, so a tree makes each default once
        struct WriteDefaults {
            DefaultFieldsCache nodes;
            DefaultFieldsCache components;
        };

        /// The element with the default fields of the type of @p object, empty (so that every field differs from it) when there is none
        template <typename Base>
        pugi::xml_node DefaultFieldsOf(const Base& object, DefaultFieldsCache& cache) {
            const String type = object.GetTypeMeta().typeName.data();
            auto found = cache.find(type);
            if (found == cache.end()) {
                UniquePtr<SerializedFields> defaults;
                if (const IntrusivePtr<Base> fresh = AbstractFactory<Base>::GetInstance().Create(StringView{type.c_str(), type.size()})) {
                    defaults = MakeUnique<SerializedFields>();
                    SerializeFields(*fresh, *defaults);
                }
                found = cache.emplace(type, eastl::move(defaults)).first;
            }
            return found->second ? found->second->Root() : pugi::xml_node{};
        }

    }  // namespace

    IntrusivePtr<SceneNode> DeserializeNode(IDeserializer& deserializer, StringView what) {
        IntrusivePtr<SceneNode> node;
        StringView type;
        if (deserializer.ReadAttributeView(kNodeTypeAttribute, type)) {
            node = AbstractFactory<SceneNode>::GetInstance().Create(type);
            if (!node) {
                deserializer.ReportError(kNodeTypeAttribute, Format("Unknown factory type: '{}'", type));
            }
        }
        if (!node) {
            node = New<SceneNode>();
        }
        node->Deserialize(deserializer);
        if (deserializer.HasErrors()) {
            LogDeserializeErrors(what, deserializer);
        }
        return node;
    }

    IntrusivePtr<SceneNode> DeserializeNode(pugi::xml_node nodeEl, StringView what) {
        XmlDeserializer deserializer;
        if (!deserializer.LoadFromXmlNode(XmlNode{nodeEl})) {
            return New<SceneNode>();
        }
        return DeserializeNode(deserializer, what);
    }

    namespace {

        void AppendComponentElementWith(pugi::xml_node parent, const IComponent& component, const NodeWriteOptions& options, DefaultFieldsCache& defaults) {
            pugi::xml_node componentEl = parent.append_child(kComponentElementName.data());
            componentEl.append_attribute(kComponentTypeAttribute.data()).set_value(component.GetTypeMeta().typeName.data());

            XmlSerializer serializer;
            component.Serialize(serializer);
            const pugi::xml_node base = options.skipDefaults ? DefaultFieldsOf(component, defaults) : pugi::xml_node{};
            XmlFieldDiff::AppendChangedFields(serializer.GetRootNode(), base, componentEl);
        }

        void AppendNodeElementWith(pugi::xml_node parent, const SceneNode& node, const NodeWriteOptions& options, WriteDefaults& defaults) {
            pugi::xml_node nodeEl = parent.append_child(kNodeElementName.data());
            if (node.GetTypeMeta() != SceneNode::GetStaticTypeMeta()) {
                nodeEl.append_attribute(kNodeTypeAttribute.data()).set_value(node.GetTypeMeta().typeName.data());
            }
            // Apart from the other fields: it is written even when it is the default
            nodeEl.append_attribute(kNodeNameAttribute.data()).set_value(node.GetName().CStr());
            SerializedFields own;
            SerializeFields(node, own);
            own.Root().remove_attribute(kNodeNameAttribute.data());
            XmlFieldDiff::AppendChangedFields(own.Root(), DefaultFieldsOf(node, defaults.nodes), nodeEl);

            if (!node.GetComponents().empty()) {
                pugi::xml_node componentsEl = nodeEl.append_child(kComponentsElementName.data());
                for (const auto& component : node.GetComponents()) {
                    AppendComponentElementWith(componentsEl, *component, options, defaults.components);
                }
            }

            pugi::xml_node childrenEl;
            for (const auto& child : node.GetChildren()) {
                if (options.skipGenerated && child->IsGenerated()) {
                    continue;
                }
                if (!childrenEl) {
                    childrenEl = nodeEl.append_child(kChildrenElementName.data());
                }
                AppendNodeElementWith(childrenEl, *child, options, defaults);
            }
        }

    }  // namespace

    void AppendComponentElement(pugi::xml_node parent, const IComponent& component, const NodeWriteOptions& options) {
        DefaultFieldsCache defaults;
        AppendComponentElementWith(parent, component, options, defaults);
    }

    void AppendNodeElement(pugi::xml_node parent, const SceneNode& node, const NodeWriteOptions& options) {
        WriteDefaults defaults;
        AppendNodeElementWith(parent, node, options, defaults);
    }

    bool BuildNodeReference(const SceneNode& live, NodeFileLoaderRef loader, pugi::xml_node nodesEl, String& reason) {
        const String ref = live.GetSourceRef().CStr();
        if (ref.empty()) {
            reason = "the node has no node file";
            return false;
        }

        pugi::xml_document bare;
        bare.append_child(kNodeElementName.data()).append_attribute(kNodeRefAttribute.data()).set_value(ref.c_str());
        pugi::xml_document expanded;
        if (!ExpandNodeReference(bare.document_element(), loader, false, expanded, reason)) {
            return false;
        }
        const IntrusivePtr<SceneNode> baseline = DeserializeNode(expanded.document_element(), ref);

        const auto& liveList = live.GetComponents();
        const auto& baseList = baseline->GetComponents();
        bool sameTypes = liveList.size() == baseList.size();
        for (size_t i = 0; sameTypes && i < liveList.size(); ++i) {
            sameTypes = liveList[i]->GetTypeMeta() == baseList[i]->GetTypeMeta();
        }
        if (!sameTypes) {
            reason = "its components are not those of the node file (one was added, removed or moved)";
            return false;
        }

        SerializedFields liveNode;
        SerializeFields(live, liveNode);
        SerializedFields baseNode;
        SerializeFields(*baseline, baseNode);
        if (!CanOverrideOwnFields(liveNode, baseNode, reason)) {
            return false;
        }

        // Built apart, so a refusal appends nothing
        pugi::xml_document candidate;
        pugi::xml_node refEl = candidate.append_child(kNodeElementName.data());
        refEl.append_attribute(kNodeRefAttribute.data()).set_value(ref.c_str());
        if (live.GetName() != baseline->GetName()) {
            refEl.append_attribute("name").set_value(live.GetName().CStr());
        }
        if (live.GetLayer() != baseline->GetLayer()) {
            refEl.append_attribute("layer").set_value(live.GetLayer());
        }

        const Vector<SerializedFields> liveComponents = SerializeComponents(live);
        const Vector<SerializedFields> baseComponents = SerializeComponents(*baseline);
        pugi::xml_node componentsEl;
        for (size_t i = 0; i < liveComponents.size(); ++i) {
            pugi::xml_document overrides;
            pugi::xml_node componentEl = overrides.append_child(kComponentElementName.data());
            if (!WriteFieldOverrides(liveComponents[i].Root(), baseComponents[i].Root(), componentEl, reason)) {
                reason = Format("component '{}': {}", liveComponents[i].type, reason);
                return false;
            }
            if (!componentEl.first_attribute() && !componentEl.first_child()) {
                continue;
            }
            componentEl.prepend_attribute(kComponentTypeAttribute.data()).set_value(liveComponents[i].type.c_str());
            if (!componentsEl) {
                componentsEl = refEl.append_child(kComponentsElementName.data());
            }
            componentsEl.append_copy(componentEl);
        }

        // Whatever is written must load back as the live node
        pugi::xml_document reloaded;
        String error;
        if (!ExpandNodeReference(refEl, loader, false, reloaded, error)) {
            reason = Format("the reference does not expand again: {}", error);
            return false;
        }
        if (!SameAsLive(liveNode, liveComponents, *DeserializeNode(reloaded.document_element(), ref))) {
            reason = "the instance cannot be written as a reference (it loads differently)";
            return false;
        }

        nodesEl.append_copy(refEl);
        return true;
    }

}  // namespace BECore
