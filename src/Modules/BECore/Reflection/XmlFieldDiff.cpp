#include "XmlFieldDiff.h"

#include <BECore/Config/StringXmlWriter.h>

namespace BECore::XmlFieldDiff {

    namespace {

        void AddName(Vector<String>& names, const char* name) {
            if (eastl::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(name);
            }
        }

    }  // namespace

    String CompactText(pugi::xml_node node) {
        StringXmlWriter writer;
        node.print(writer, "", pugi::format_raw);
        return writer.text;
    }

    Field ReadField(pugi::xml_node fields, const char* name) {
        Field field;
        if (const pugi::xml_attribute attr = fields.attribute(name)) {
            field.hasAttribute = true;
            field.attribute = attr.value();
        }
        for (pugi::xml_node child = fields.child(name); child; child = child.next_sibling(name)) {
            field.hasElements = true;
            field.elements += CompactText(child);
        }
        return field;
    }

    Vector<String> FieldNames(pugi::xml_node current, pugi::xml_node base) {
        Vector<String> names;
        for (const pugi::xml_node fields : {current, base}) {
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

    void AppendChangedFields(pugi::xml_node current, pugi::xml_node base, pugi::xml_node target) {
        const auto differs = [&](const char* name) { return ReadField(current, name) != ReadField(base, name); };
        for (pugi::xml_attribute attr = current.first_attribute(); attr; attr = attr.next_attribute()) {
            if (differs(attr.name())) {
                target.append_attribute(attr.name()).set_value(attr.value());
            }
        }
        for (pugi::xml_node child = current.first_child(); child; child = child.next_sibling()) {
            if (differs(child.name())) {
                target.append_copy(child);
            }
        }
        for (pugi::xml_node child = base.first_child(); child; child = child.next_sibling()) {
            const bool firstOfItsName = child.type() == pugi::node_element && child == base.child(child.name());
            if (firstOfItsName && !current.child(child.name()) && !current.attribute(child.name())) {
                target.append_child(child.name());
            }
        }
    }

}  // namespace BECore::XmlFieldDiff
