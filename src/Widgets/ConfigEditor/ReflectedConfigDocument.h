#pragma once

#include <BECore/Cook/ContentCooker.h>
#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Types/Types.h>
#include <expected>
#include <pugixml.hpp>

namespace BECore {

    /**
     * @brief A config opened as live reflected objects over its XML document, and the writer of their changes back into it
     *
     * A config opens so when its stem has a cooker that makes its root object (`CookableConfig`). It is read as the cook reads it
     * (strict values, a `ValueLinkScope` of the stem), and the elements of the root's factory lists are objects of their own,
     * disabled ones included.
     *
     * **Loading runs no hooks.** The objects are made and deserialized, never initialized: `Initialize` would start what the program
     * already runs (a sink subscribes to the log, a `FileSink` truncates its file). So a config that holds a pointer anywhere but in
     * those lists does not open (the reflected load of a pointer initializes what it makes), nor does an element of a type whose
     * destructor changes the program.
     *
     * An object is edited in place, through its `acceptVisitor`; `NoteChange` follows an edit. The document holds handles into the
     * DOM it was opened over: drop it before that DOM is replaced.
     */
    class ReflectedConfigDocument {
    public:
        /// An object read from an element of the DOM
        struct Object {
            pugi::xml_node node;
            const TypeInfo* type = nullptr;
            OwnedObject object;
            /// What the object wrote right after its load: @ref node loads as this
            UniquePtr<XmlSerializer> base;
        };

        /// An element of a factory list
        struct Element : Object {
            const TypeInfo* writtenType = nullptr;  ///< What `type` of the node names
            bool enabled = true;                    ///< `enabled` of the node as it will be saved; not used with @ref hasEnabledField
            bool writtenEnabled = true;             ///< `enabled` of the node as it is
            bool hasEnabledField = false;           ///< The type reflects `enabled`: that field is the one control, and it writes the attribute
        };

        /// A factory list of the root
        struct List {
            StringView name;         ///< The field of the root, also the element of the document that holds the list
            StringView elementName;  ///< The elements of it the loader reads
            const TypeInfo* base = nullptr;
            Vector<const TypeInfo*> types;  ///< What an element can be, sorted by name
            Vector<Element> elements;       ///< In the order of the document
        };

        /**
         * @brief Opens the config of stem @p stem whose root element is @p root
         *
         * The error is why the config cannot be edited as objects: no reflected type for the stem, a type this document cannot hold,
         * or the first error of the read (an unknown or missing `type`, a malformed value). Unknown keys are no error.
         */
        [[nodiscard]] static std::expected<ReflectedConfigDocument, String> Open(StringView stem, pugi::xml_node root, const ContentCookRegistry& registry = ContentCookRegistry::GetInstance());

        /// The object of the root's own fields (its factory lists stay empty in it); null when the root has lists only
        [[nodiscard]] const Object* GetRoot() const {
            return _root ? &*_root : nullptr;
        }

        [[nodiscard]] const Vector<List>& GetLists() const {
            return _lists;
        }

        /// Sets whether the element is enabled; for an element without @ref Element::hasEnabledField
        void SetEnabled(size_t list, size_t element, bool enabled);

        /**
         * @brief Makes the element an object of @p type, one of the list's `types`, read from the same DOM element
         *
         * The edits of the old object are gone. An error (the new type cannot read what the element holds) leaves the element as it
         * was. Not while a visitor walks the element's object, which this destroys.
         */
        std::expected<void, String> ChangeType(size_t list, size_t element, const TypeInfo& type);

        /// Compares every object with its load again: after an edit, not once a frame (it serializes them all)
        void NoteChange();

        /// Whether a save would change the DOM, as of the last change noted: an edit undone is no change
        [[nodiscard]] bool IsDirty() const {
            return _dirty;
        }

        /// Writes into the DOM what differs from it, object by object; the rest of the document stays as written. Not dirty after
        void Apply();

    private:
        ReflectedConfigDocument() = default;

        /// The element @p node as a new object of @p type
        static std::expected<Element, String> LoadElement(pugi::xml_node node, const TypeInfo& type);

        String _stem;
        MakeRootFunction* _makeRoot = nullptr;
        Optional<Object> _root;
        Vector<List> _lists;
        bool _dirty = false;
    };

}  // namespace BECore
