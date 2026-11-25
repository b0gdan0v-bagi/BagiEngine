#include "SceneNodeInstance.h"

#include <BECore/Config/StringXmlWriter.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>

namespace BECore {

    namespace {

        /// What a component serializes to, as text: the type and the XML of its fields in the compact form
        struct SerializedComponent {
            String type;
            pugi::xml_document fields;  // the document element holds the attributes and child elements of the fields
        };

        String Printed(pugi::xml_node node) {
            StringXmlWriter writer;
            node.print(writer, "", pugi::format_raw);
            return writer.text;
        }

        void SerializeComponent(const IComponent& component, SerializedComponent& out) {
            out.type = component.GetTypeMeta().typeName.data();
            XmlSerializer serializer;
            component.Serialize(serializer);
            out.fields.append_copy(serializer.GetRootNode());
        }

        Vector<SerializedComponent> SerializeComponents(const SceneNode& node) {
            Vector<SerializedComponent> result(node.GetComponents().size());
            for (size_t i = 0; i < result.size(); ++i) {
                SerializeComponent(*node.GetComponents()[i], result[i]);
            }
            return result;
        }

        /// One field of a serialized component: its attribute and the text of its child elements of that name
        struct FieldState {
            bool hasAttribute = false;
            String attribute;
            bool hasChildren = false;
            String children;

            bool operator==(const FieldState& other) const {
                return hasAttribute == other.hasAttribute && attribute == other.attribute && hasChildren == other.hasChildren && children == other.children;
            }
        };

        FieldState ReadField(pugi::xml_node fields, const char* name) {
            FieldState state;
            if (const pugi::xml_attribute attr = fields.attribute(name)) {
                state.hasAttribute = true;
                state.attribute = attr.value();
            }
            for (pugi::xml_node child = fields.child(name); child; child = child.next_sibling(name)) {
                state.hasChildren = true;
                state.children += Printed(child);
            }
            return state;
        }

        void AddName(Vector<String>& names, const char* name) {
            if (eastl::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(name);
            }
        }

        /// The names of the fields of both sides: their attributes and their child elements
        Vector<String> FieldNames(pugi::xml_node liveFields, pugi::xml_node baseFields) {
            Vector<String> names;
            for (const pugi::xml_node fields : {liveFields, baseFields}) {
                for (pugi::xml_attribute attr = fields.first_attribute(); attr; attr = attr.next_attribute()) {
                    AddName(names, attr.name());
                }
                for (pugi::xml_node child = fields.first_child(); child; child = child.next_sibling()) {
                    if (child.type() == pugi::node_element) {
                        AddName(names, child.name());
                    }
                }
            }
            return names;
        }

        /// Writes the fields of @p live that differ from @p base into @p componentEl. False when a field is written in both spellings
        bool WriteFieldOverrides(pugi::xml_node live, pugi::xml_node base, pugi::xml_node componentEl, String& reason) {
            for (const String& name : FieldNames(live, base)) {
                const FieldState liveField = ReadField(live, name.c_str());
                if (liveField == ReadField(base, name.c_str())) {
                    continue;
                }
                if (liveField.hasAttribute && liveField.hasChildren) {
                    reason = Format("field '{}' is written as an attribute and as an element", name);
                    return false;
                }
                if (liveField.hasAttribute) {
                    componentEl.append_attribute(name.c_str()).set_value(liveField.attribute.c_str());
                } else if (liveField.hasChildren) {
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

        /// True when @p other has the name, the layer and the serialized components of @p live (the round trip check)
        bool SameAsLive(const SceneNode& live, const Vector<SerializedComponent>& liveComponents, const SceneNode& other) {
            if (live.GetName() != other.GetName() || live.GetLayer() != other.GetLayer()) {
                return false;
            }
            const Vector<SerializedComponent> otherComponents = SerializeComponents(other);
            if (liveComponents.size() != otherComponents.size()) {
                return false;
            }
            for (size_t i = 0; i < liveComponents.size(); ++i) {
                if (liveComponents[i].type != otherComponents[i].type || Printed(liveComponents[i].fields.document_element()) != Printed(otherComponents[i].fields.document_element())) {
                    return false;
                }
            }
            return true;
        }

        /// The serialized fields of a freshly created component per type name: the state the loader builds a component in before it reads its fields.
        /// A null entry is a type the factory cannot create. One per AppendNodeElement call, not global, so a tree makes each default once
        using DefaultFieldsCache = HashMap<String, UniquePtr<SerializedComponent>>;

        const SerializedComponent* DefaultFieldsOf(const IComponent& component, DefaultFieldsCache& cache) {
            const String type = component.GetTypeMeta().typeName.data();
            if (const auto found = cache.find(type); found != cache.end()) {
                return found->second.get();
            }
            UniquePtr<SerializedComponent> defaults;
            if (const IntrusivePtr<IComponent> fresh = AbstractFactory<IComponent>::GetInstance().Create(StringView{type.c_str(), type.size()})) {
                defaults = MakeUnique<SerializedComponent>();
                SerializeComponent(*fresh, *defaults);
            }
            return cache.emplace(type, eastl::move(defaults)).first->second.get();
        }

        /// Writes the fields of @p live into @p componentEl in their order, leaving out each one the default has the same way (equal attribute and
        /// equal text of its elements). A field the default has and @p live does not (a list that was emptied) is written as an empty element:
        /// the loader would otherwise keep the default's. @p defaults null writes every field
        void AppendComponentFields(pugi::xml_node live, const SerializedComponent* defaults, pugi::xml_node componentEl) {
            const pugi::xml_node base = defaults ? defaults->fields.document_element() : pugi::xml_node{};
            const auto isDefault = [&](const char* name) { return defaults && ReadField(live, name) == ReadField(base, name); };
            for (pugi::xml_attribute attr = live.first_attribute(); attr; attr = attr.next_attribute()) {
                if (!isDefault(attr.name())) {
                    componentEl.append_attribute(attr.name()).set_value(attr.value());
                }
            }
            for (pugi::xml_node child = live.first_child(); child; child = child.next_sibling()) {
                if (!isDefault(child.name())) {
                    componentEl.append_copy(child);
                }
            }
            if (!defaults) {
                return;
            }
            for (pugi::xml_node child = base.first_child(); child; child = child.next_sibling()) {
                if (child.type() == pugi::node_element && !live.child(child.name()) && !live.attribute(child.name()) && !componentEl.child(child.name())) {
                    componentEl.append_child(child.name());
                }
            }
        }

    }  // namespace

    IntrusivePtr<SceneNode> DeserializeNode(IDeserializer& deserializer, StringView what) {
        auto node = New<SceneNode>();
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

        void AppendNodeElementWith(pugi::xml_node parent, const SceneNode& node, const NodeWriteOptions& options, DefaultFieldsCache& defaults) {
            pugi::xml_node nodeEl = parent.append_child(kNodeElementName.data());
            nodeEl.append_attribute("name").set_value(node.GetName().CStr());
            if (node.GetLayer() != 0) {
                nodeEl.append_attribute("layer").set_value(node.GetLayer());
            }

            if (!node.GetComponents().empty()) {
                pugi::xml_node componentsEl = nodeEl.append_child(kComponentsElementName.data());
                for (const auto& component : node.GetComponents()) {
                    pugi::xml_node componentEl = componentsEl.append_child(kComponentElementName.data());
                    componentEl.append_attribute(kComponentTypeAttribute.data()).set_value(component->GetTypeMeta().typeName.data());

                    XmlSerializer serializer;
                    component->Serialize(serializer);
                    AppendComponentFields(serializer.GetRootNode(), options.skipDefaults ? DefaultFieldsOf(*component, defaults) : nullptr, componentEl);
                }
            }

            pugi::xml_node childrenEl;
            for (const auto& child : node.GetChildren()) {
                if (options.skipGenerated && child->IsGenerated()) {
                    continue;
                }
                if (!childrenEl) {
                    childrenEl = nodeEl.append_child("children");
                }
                AppendNodeElementWith(childrenEl, *child, options, defaults);
            }
        }

    }  // namespace

    void AppendNodeElement(pugi::xml_node parent, const SceneNode& node, const NodeWriteOptions& options) {
        DefaultFieldsCache defaults;
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

        const Vector<SerializedComponent> liveComponents = SerializeComponents(live);
        const Vector<SerializedComponent> baseComponents = SerializeComponents(*baseline);
        pugi::xml_node componentsEl;
        for (size_t i = 0; i < liveComponents.size(); ++i) {
            pugi::xml_document overrides;
            pugi::xml_node componentEl = overrides.append_child(kComponentElementName.data());
            if (!WriteFieldOverrides(liveComponents[i].fields.document_element(), baseComponents[i].fields.document_element(), componentEl, reason)) {
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
        if (!SameAsLive(live, liveComponents, *DeserializeNode(reloaded.document_element(), ref))) {
            reason = "the instance cannot be written as a reference (it loads differently)";
            return false;
        }

        nodesEl.append_copy(refEl);
        return true;
    }

}  // namespace BECore
