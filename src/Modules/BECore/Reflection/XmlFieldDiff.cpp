#include "XmlFieldDiff.h"

#include <BECore/Config/StringXmlWriter.h>

namespace BECore::XmlFieldDiff {

    namespace {

        // The element of a map, whose elements are told apart by the text of their key
        constexpr StringView kMapElement = "entry";
        constexpr const char* kMapKey = "key";
        // The attributes that name the type the rest of a list element is read into (a factory element, a variant)
        constexpr Array<const char*, 2> kElementTypeAttributes{"type", "index"};

        void AddName(Vector<String>& names, const char* name) {
            if (eastl::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(name);
            }
        }

        /// The element name of the list that @p field holds; empty when it does not look like one (`PatchChangedFields`)
        StringView ListElementName(pugi::xml_node field) {
            if (field.first_attribute()) {
                return {};
            }
            StringView name;
            for (pugi::xml_node child = field.first_child(); child; child = child.next_sibling()) {
                if (child.type() != pugi::node_element || (!name.empty() && name != child.name())) {
                    return {};
                }
                name = child.name();
            }
            return name;
        }

        /// The elements of @p list that a loader reads as elements of a list written under @p name
        Vector<pugi::xml_node> ListElements(pugi::xml_node list, StringView name) {
            Vector<pugi::xml_node> elements;
            for (pugi::xml_node child = list.first_child(); child; child = child.next_sibling()) {
                if (child.type() == pugi::node_element && (name == kGenericElementName || name == child.name())) {
                    elements.push_back(child);
                }
            }
            return elements;
        }

        bool SameAttribute(pugi::xml_node left, pugi::xml_node right, const char* name) {
            return StringView{left.attribute(name).value()} == right.attribute(name).value();
        }

        /// Patches @p targetList element by element; false, with nothing changed, when its elements cannot be paired with those of @p baseList
        bool PatchList(pugi::xml_node currentList, pugi::xml_node baseList, pugi::xml_node targetList) {
            const StringView name = ListElementName(baseList);
            if (name.empty() || ListElementName(currentList) != name) {
                return false;
            }
            const Vector<pugi::xml_node> current = ListElements(currentList, name);
            const Vector<pugi::xml_node> base = ListElements(baseList, name);
            const Vector<pugi::xml_node> target = ListElements(targetList, name);
            if (current.size() != base.size() || target.size() != base.size()) {
                return false;
            }
            // A hash map is written in the order of its keys, whatever the order of the file
            for (size_t i = 0; name == kMapElement && i < base.size(); ++i) {
                if (!SameAttribute(base[i], target[i], kMapKey)) {
                    return false;
                }
            }
            for (size_t i = 0; i < base.size(); ++i) {
                const bool sameType = eastl::all_of(kElementTypeAttributes.begin(), kElementTypeAttributes.end(), [&](const char* attribute) { return SameAttribute(current[i], base[i], attribute); });
                if (sameType) {
                    PatchChangedFields(current[i], base[i], target[i]);
                } else {
                    targetList.insert_copy_before(current[i], target[i]).set_name(target[i].name());
                    targetList.remove_child(target[i]);
                }
            }
            return true;
        }

        /// Gives the elements of @p list, written anew in place of @p old, the name of the first element of @p old when any name loads
        void KeepElementName(pugi::xml_node old, pugi::xml_node list) {
            const pugi::xml_node first = old.find_child([](pugi::xml_node child) { return child.type() == pugi::node_element; });
            // An `item` among the old ones may be a member of that name of an object that is no list
            if (!first || old.child(kGenericElementName.data()) || ListElementName(list) != kGenericElementName) {
                return;
            }
            for (pugi::xml_node child = list.first_child(); child; child = child.next_sibling()) {
                child.set_name(first.name());
            }
        }

        /// Puts the elements of @p name that @p current holds where the first one of @p target stands, at the end when it has none
        void ReplaceElements(pugi::xml_node current, pugi::xml_node target, const char* name) {
            const pugi::xml_node first = target.child(name);
            for (pugi::xml_node field = current.child(name); field; field = field.next_sibling(name)) {
                const pugi::xml_node copy = first ? target.insert_copy_before(field, first) : target.append_copy(field);
                KeepElementName(first, copy);
            }
            for (pugi::xml_node old = first; old;) {
                const pugi::xml_node next = old.next_sibling(name);
                target.remove_child(old);
                old = next;
            }
        }

        void PatchField(pugi::xml_node current, pugi::xml_node base, pugi::xml_node target, const char* name) {
            if (const pugi::xml_attribute value = current.attribute(name)) {
                pugi::xml_attribute attr = target.attribute(name);
                if (!attr) {
                    attr = target.append_attribute(name);
                }
                attr.set_value(value.value());
            } else {
                target.remove_attribute(name);
            }

            const pugi::xml_node field = current.child(name);
            const pugi::xml_node baseField = base.child(name);
            const bool single = field && !field.next_sibling(name) && !baseField.next_sibling(name);
            if (!single || !PatchList(field, baseField, target.child(name))) {
                // Removes the elements of the target when the object no longer has any
                ReplaceElements(current, target, name);
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

    void PatchChangedFields(pugi::xml_node current, pugi::xml_node base, pugi::xml_node target) {
        for (const String& name : FieldNames(current, base)) {
            if (ReadField(current, name.c_str()) != ReadField(base, name.c_str())) {
                PatchField(current, base, target, name.c_str());
            }
        }
    }

}  // namespace BECore::XmlFieldDiff
