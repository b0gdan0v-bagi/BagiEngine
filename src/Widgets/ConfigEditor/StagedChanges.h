#pragma once

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
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
        void Set(const eastl::string& key, pugi::xml_attribute attr, eastl::string_view newValue);

        /**
         * @brief Stage a new value for an XML text node
         * @param key Unique key identifying the field
         * @param textNode Pugixml text node handle (must remain valid until ApplyTo/Clear)
         * @param newValue New string value to apply on save
         */
        void SetText(const eastl::string& key, pugi::xml_node textNode, eastl::string_view newValue);

        /**
         * @brief Get staged value for a key, or nullptr if not staged
         */
        const eastl::string* Get(const eastl::string& key) const;

        /**
         * @brief Check if a key has a staged change
         */
        bool Has(const eastl::string& key) const;

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
        void ForEach(eastl::function<void(const eastl::string&, const eastl::string&)> fn) const;

    private:
        struct Entry {
            eastl::string value;
            pugi::xml_attribute attr;
            pugi::xml_node textNode;
        };

        eastl::unordered_map<eastl::string, Entry> _changes;
    };

} // namespace BECore
