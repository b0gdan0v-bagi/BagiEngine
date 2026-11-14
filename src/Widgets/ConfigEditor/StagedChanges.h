#pragma once

#include <EASTL/unordered_map.h>

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
        void Set(const String& key, pugi::xml_attribute attr, StringView newValue);

        /**
         * @brief Stage a new value for an XML text node
         * @param key Unique key identifying the field
         * @param textNode Pugixml text node handle (must remain valid until ApplyTo/Clear)
         * @param newValue New string value to apply on save
         */
        void SetText(const String& key, pugi::xml_node textNode, StringView newValue);

        /**
         * @brief Get staged value for a key, or nullptr if not staged
         */
        const String* Get(const String& key) const;

        /**
         * @brief Check if a key has a staged change
         */
        bool Has(const String& key) const;

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
        void ForEach(eastl::function<void(const String&, const String&)> fn) const;

    private:
        struct Entry {
            String value;
            pugi::xml_attribute attr;
            pugi::xml_node textNode;
        };

        HashMap<String, Entry> _changes;
    };

}  // namespace BECore
