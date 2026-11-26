#pragma once

#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Types/Types.h>
#include <pugixml.hpp>

/**
 * @brief Which fields of an object differ from those of another one, compared as XmlSerializer writes them
 *
 * The opt-in way to leave default fields out of a file, chosen per write: a writer that does not call it writes every field.
 *
 * A **field** is an attribute of the element that holds the object, or all of its child elements of one name. It is compared and
 * written as a whole: a nested object or a list with one changed member is written complete, never pruned inside. The loader reads it
 * into the value the owner's default holds, which need not be the default of the field's own type, so a member left out against the
 * latter would load as another value.
 *
 * The **base** is an object of the same dynamic type in the state the loader reads into (a factory-made object, the owner's default),
 * never a `T{}` of a field's type.
 */
namespace BECore::XmlFieldDiff {

    /// One field of a serialized object
    struct Field {
        bool hasAttribute = false;
        String attribute;
        bool hasElements = false;
        String elements;  // CompactText of every child element of the name, in order

        bool operator==(const Field&) const = default;
    };

    /// The text of @p node and its subtree without indentation and line breaks
    String CompactText(pugi::xml_node node);

    /// The field @p name of the element @p fields; an empty Field when it has none
    Field ReadField(pugi::xml_node fields, const char* name);

    /// The names of the fields of both elements, each once, those of @p current first
    Vector<String> FieldNames(pugi::xml_node current, pugi::xml_node base);

    /**
     * @brief Appends to @p target the fields of @p current that differ from those of @p base, in the order of @p current
     *
     * A field that only @p base has, as elements (a list that was emptied and that the type then does not write), is appended as an
     * empty element: the loader would otherwise keep the base's. An attribute that only @p base has has no such spelling and is not
     * written, as in a full write. An empty @p base appends every field.
     *
     * @param current,base The elements that hold the fields (XmlSerializer::GetRootNode() of a serialized object)
     */
    void AppendChangedFields(pugi::xml_node current, pugi::xml_node base, pugi::xml_node target);

    /// The same for two objects, each written by its `Serialize` (the virtual one, so a base reference compares the derived fields)
    template <typename T>
    requires requires(const T& object, ISerializer& serializer) {
        object.Serialize(serializer);
    }
    void AppendChangedFields(const T& object, const T& base, pugi::xml_node target) {
        XmlSerializer current;
        object.Serialize(current);
        XmlSerializer baseFields;
        base.Serialize(baseFields);
        AppendChangedFields(current.GetRootNode(), baseFields.GetRootNode(), target);
    }

}  // namespace BECore::XmlFieldDiff
