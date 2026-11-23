#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <EASTL/unordered_map.h>
#include <pugixml.hpp>

namespace BECore {

    /**
     * @brief Container for pending XML edits that have not yet been saved to disk
     *
     * Holds new attribute/text values without modifying the live pugixml DOM.
     * Stores pugixml handles so that ApplyTo() can flush all staged edits at once.
     *
     * Key format: "<nodePath>@<attrName>" for attributes, "<nodePath>#text" for text nodes.
     */
    class StagedChanges {
    public:
        /**
         * @brief Stage a new value for an XML attribute
         * @param key Unique key identifying the field
         * @param attr Pugixml attribute handle (must remain valid until ApplyTo/Clear)
         * @param newValue New string value to apply on save
         */
        void Set(StringView key, pugi::xml_attribute attr, StringView newValue);

        /**
         * @brief Stage a new value for an XML text node
         * @param key Unique key identifying the field
         * @param textNode Pugixml text node handle (must remain valid until ApplyTo/Clear)
         * @param newValue New string value to apply on save
         */
        void SetText(StringView key, pugi::xml_node textNode, StringView newValue);

        /**
         * @brief Get staged value for a key, or nullptr if not staged
         *
         * Get and Has look the key up as a view and allocate nothing: the editor asks them for every attribute every frame.
         */
        const String* Get(StringView key) const;

        /**
         * @brief Check if a key has a staged change
         */
        bool Has(StringView key) const;

        /**
         * @brief Clear all staged changes (does NOT write to DOM)
         */
        void Clear();

        /**
         * @brief Check if there are no staged changes
         */
        bool IsEmpty() const;

        /**
         * @brief Apply all staged changes to the pugixml DOM
         * @param root Root node (unused - handles are stored directly; kept for API clarity)
         */
        void ApplyTo(pugi::xml_node root);

        /**
         * @brief Iterate all staged entries
         * @param fn Callback receiving (key, newValue) for each staged change
         */
        void ForEach(FunctionRef<void(const String&, const String&)> fn) const;

    private:
        struct Entry {
            String value;
            pugi::xml_attribute attr;
            pugi::xml_node textNode;
        };

        Entry& EntryFor(StringView key);

        HashMap<String, Entry> _changes;
    };

}  // namespace BECore
