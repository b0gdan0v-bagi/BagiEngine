#include "ReflectedConfigDocument.h"

#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlFieldDiff.h>
#include <BECore/Values/ValueLinkScope.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Widgets/EditorLayout/EditorLayoutWidget.h>
#include <Widgets/SceneEditor/SceneEditorWidget.h>
#include <Widgets/Viewport/ViewportWidget.h>

namespace BECore {

    namespace {

        constexpr const char* kTypeAttribute = "type";
        constexpr const char* kEnabledAttribute = "enabled";
        constexpr StringView kEnabledField = "enabled";

        /// The types whose destructor changes the running program (the pause, the editor flags of the scene, the safe area). By
        /// name: a use of their reflected names would link the widgets, and their registration, into every program that links this file
        constexpr Array<StringView, 3> kTypesThatChangeTheProgram{"EditorLayoutWidget", "SceneEditorWidget", "ViewportWidget"};
        static_assert(sizeof(EditorLayoutWidget) > 0 && sizeof(SceneEditorWidget) > 0 && sizeof(ViewportWidget) > 0, "a renamed widget is renamed in kTypesThatChangeTheProgram too");

        bool HoldsPointer(const TypeInfo& type, Vector<const TypeInfo*>& seen);

        /// Whether a value of @p desc holds a pointer at any depth, or an object of a type the registry cannot look into
        bool HoldsPointer(const TypeDesc& desc, Vector<const TypeInfo*>& seen) {
            if (desc.kind == TypeKind::Pointer) {
                return true;
            }
            if (desc.kind == TypeKind::Object) {
                const TypeInfo* type = TypeRegistry::GetInstance().Find(desc.ObjectHash());
                return type == nullptr || HoldsPointer(*type, seen);
            }
            if ((desc.element != nullptr && HoldsPointer(*desc.element, seen)) || (desc.key != nullptr && HoldsPointer(*desc.key, seen))) {
                return true;
            }
            return eastl::any_of(desc.alternatives.begin(), desc.alternatives.end(), [&seen](const TypeDesc* alternative) { return HoldsPointer(*alternative, seen); });
        }

        bool HoldsPointer(const TypeInfo& type, Vector<const TypeInfo*>& seen) {
            // A recursive type is looked into once
            if (eastl::find(seen.begin(), seen.end(), &type) != seen.end()) {
                return false;
            }
            seen.push_back(&type);
            const Vector<FieldRef> fields = TypeRegistry::GetInstance().AllFields(type);
            return eastl::any_of(fields.begin(), fields.end(), [&seen](const FieldRef& field) { return HoldsPointer(*field.field->type, seen); });
        }

        bool CanBeEdited(const TypeInfo& type) {
            if (type.serialize == nullptr || type.deserialize == nullptr || type.acceptVisitor == nullptr) {
                return false;
            }
            Vector<const TypeInfo*> seen;
            return eastl::find(kTypesThatChangeTheProgram.begin(), kTypesThatChangeTheProgram.end(), type.Name()) == kTypesThatChangeTheProgram.end() && !HoldsPointer(type, seen);
        }

        /// A list the reflected load fills by making an object per element
        bool IsFactoryList(const FieldDesc& field) {
            return !field.factoryElementName.empty() && field.type->kind == TypeKind::List && field.type->resizable && field.type->element->kind == TypeKind::Pointer;
        }

        /// Whether the loader makes an element of a list of @p base as @p type by its name
        bool IsMadeByName(const TypeInfo& type, const TypeInfo& base) {
            return type.factoryBase == base.Hash() && type.create != nullptr && TypeRegistry::GetInstance().IsDerivedFrom(type, base);
        }

        Vector<const TypeInfo*> EditableTypes(const TypeInfo& base) {
            Vector<const TypeInfo*> types = TypeRegistry::GetInstance().Derived(base, true);
            types.push_back(&base);
            types.erase(eastl::remove_if(types.begin(), types.end(), [&base](const TypeInfo* type) { return !IsMadeByName(*type, base) || !CanBeEdited(*type); }), types.end());
            eastl::sort(types.begin(), types.end(), [](const TypeInfo* a, const TypeInfo* b) { return a->Name() < b->Name(); });
            return types;
        }

        /// Why no element of a list of @p base can be made as @p name
        String RefusalOf(StringView name, const TypeInfo& base) {
            if (name.empty()) {
                return String("the element has no 'type'");
            }
            const TypeInfo* type = TypeRegistry::GetInstance().Find(name);
            if (type != nullptr && IsMadeByName(*type, base)) {
                return Format("the type '{}' cannot be edited as an object", name);
            }
            return Format("unknown type '{}'", name);
        }

        UniquePtr<XmlSerializer> Written(const ReflectedConfigDocument::Object& object) {
            auto text = MakeUnique<XmlSerializer>();
            object.type->serialize(object.object.Get(), *text);
            return text;
        }

        bool DiffersFromLoad(const ReflectedConfigDocument::Object& object) {
            const UniquePtr<XmlSerializer> current = Written(object);
            return XmlFieldDiff::CompactText(current->GetRootNode()) != XmlFieldDiff::CompactText(object.base->GetRootNode());
        }

        bool IsChanged(const ReflectedConfigDocument::Element& element) {
            return element.type != element.writtenType || (!element.hasEnabledField && element.enabled != element.writtenEnabled) || DiffersFromLoad(element);
        }

        /// Reads @p target's object from @p source as the cook reads a config, and `enabled` of it when asked; the first error is the reason
        std::expected<void, String> Load(ReflectedConfigDocument::Object& target, pugi::xml_node source, bool* enabled = nullptr) {
            XmlDeserializer reader;
            reader.SetStrictValues(true);
            if (reader.LoadFromXmlNode(XmlNode{source})) {
                if (enabled != nullptr) {
                    reader.ReadAttribute(kEnabledAttribute, *enabled);
                }
                target.type->deserialize(target.object.Get(), reader);
                reader.FinishLoad();
            }
            if (reader.HasErrors()) {
                const DeserializeError& error = reader.GetErrors().front();
                return std::unexpected(error.path.empty() ? error.errorMessage : Format("{}: {}", error.path, error.errorMessage));
            }
            target.base = Written(target);
            return {};
        }

        /// The root's own fields: read without the elements of @p lists, which the reflected load would initialize
        std::expected<void, String> LoadRoot(ReflectedConfigDocument::Object& root, const Vector<ReflectedConfigDocument::List>& lists) {
            pugi::xml_document copy;
            pugi::xml_node fields = copy.append_copy(root.node);
            for (const ReflectedConfigDocument::List& list : lists) {
                const String name(list.name);
                while (fields.remove_child(name.c_str())) {
                }
            }
            return Load(root, fields);
        }

        void SetAttribute(pugi::xml_node node, const char* name, StringView value) {
            pugi::xml_attribute attribute = node.attribute(name);
            if (!attribute) {
                attribute = node.append_attribute(name);
            }
            attribute.set_value(value.data(), value.size());
        }

        void Patch(ReflectedConfigDocument::Object& object) {
            UniquePtr<XmlSerializer> current = Written(object);
            XmlFieldDiff::PatchChangedFields(current->GetRootNode(), object.base->GetRootNode(), object.node);
            object.base = std::move(current);
        }

    }  // namespace

    std::expected<ReflectedConfigDocument::Element, String> ReflectedConfigDocument::LoadElement(pugi::xml_node node, const TypeInfo& type) {
        Element element;
        element.node = node;
        element.type = &type;
        element.writtenType = &type;
        element.object = TypeRegistry::GetInstance().Create(type);
        const Vector<FieldRef> fields = TypeRegistry::GetInstance().AllFields(type);
        element.hasEnabledField = eastl::any_of(fields.begin(), fields.end(), [](const FieldRef& field) { return field.field->name == kEnabledField; });
        if (const auto loaded = Load(element, node, &element.enabled); !loaded) {
            return std::unexpected(loaded.error());
        }
        element.writtenEnabled = element.enabled;
        return element;
    }

    std::expected<ReflectedConfigDocument, String> ReflectedConfigDocument::Open(StringView stem, pugi::xml_node root, const ContentCookRegistry& registry) {
        const ContentCooker* cooker = registry.Find(stem);
        if (cooker == nullptr) {
            return std::unexpected(String("no reflected type is registered for this config"));
        }
        if (cooker->makeRoot == nullptr) {
            return std::unexpected(String("its reader is written by hand"));
        }
        const TypeRegistry& types = TypeRegistry::GetInstance();
        const TypeInfo* rootType = types.Find(cooker->rootType);
        if (rootType == nullptr || rootType->serialize == nullptr || rootType->deserialize == nullptr || rootType->acceptVisitor == nullptr) {
            return std::unexpected(Format("the type '{}' is not reflected in this program", cooker->rootType));
        }
        if (!root) {
            return std::unexpected(String("the config has no XML document"));
        }

        ReflectedConfigDocument document;
        document._stem = String(stem);
        document._makeRoot = cooker->makeRoot;

        bool hasOwnFields = false;
        for (const FieldRef& ref : types.AllFields(*rootType)) {
            const FieldDesc& field = *ref.field;
            if (IsFactoryList(field)) {
                const TypeInfo* base = types.Find(field.type->element->ObjectHash());
                if (base == nullptr) {
                    return std::unexpected(Format("the base type of the list '{}' is not reflected", field.name));
                }
                document._lists.push_back(List{field.name, field.factoryElementName, base, EditableTypes(*base), {}});
                continue;
            }
            Vector<const TypeInfo*> seen;
            if (HoldsPointer(*field.type, seen)) {
                return std::unexpected(Format("the field '{}' holds objects behind pointers", field.name));
            }
            hasOwnFields = true;
        }

        // The root first: the `<context>` its elements' values read is a field of it
        const ValueLinkScope scope(stem);
        if (hasOwnFields) {
            Object& object = document._root.emplace(Object{root, rootType, cooker->makeRoot(), nullptr});
            if (const auto loaded = LoadRoot(object, document._lists); !loaded) {
                return std::unexpected(loaded.error());
            }
        }

        for (List& list : document._lists) {
            const String listName(list.name);
            const String elementName(list.elementName);
            const pugi::xml_node container = root.child(listName.c_str());
            for (pugi::xml_node node = container.child(elementName.c_str()); node; node = node.next_sibling(elementName.c_str())) {
                const StringView typeName = node.attribute(kTypeAttribute).value();
                const auto type = eastl::find_if(list.types.begin(), list.types.end(), [typeName](const TypeInfo* candidate) { return candidate->Name() == typeName; });
                if (type == list.types.end()) {
                    return std::unexpected(Format("{}[{}]: {}", list.name, list.elements.size(), RefusalOf(typeName, *list.base)));
                }
                std::expected<Element, String> element = LoadElement(node, **type);
                if (!element) {
                    return std::unexpected(Format("{}[{}] ({}): {}", list.name, list.elements.size(), typeName, element.error()));
                }
                list.elements.push_back(std::move(*element));
            }
        }
        return document;
    }

    void ReflectedConfigDocument::SetEnabled(size_t list, size_t element, bool enabled) {
        _lists[list].elements[element].enabled = enabled;
        NoteChange();
    }

    std::expected<void, String> ReflectedConfigDocument::ChangeType(size_t list, size_t element, const TypeInfo& type) {
        List& owner = _lists[list];
        Element& old = owner.elements[element];
        if (old.type == &type) {
            return {};
        }
        if (eastl::find(owner.types.begin(), owner.types.end(), &type) == owner.types.end()) {
            return std::unexpected(RefusalOf(type.Name(), *owner.base));
        }
        const ValueLinkScope scope(_stem);
        if (_root) {
            // Read again for the `<context>` the values of the element may name
            Object root{_root->node, _root->type, _makeRoot(), nullptr};
            if (auto loaded = LoadRoot(root, _lists); !loaded) {
                return loaded;
            }
        }
        std::expected<Element, String> next = LoadElement(old.node, type);
        if (!next) {
            return std::unexpected(next.error());
        }
        next->writtenType = old.writtenType;
        if (!old.hasEnabledField) {
            next->enabled = old.enabled;
        }
        old = std::move(*next);
        NoteChange();
        return {};
    }

    void ReflectedConfigDocument::NoteChange() {
        _dirty =
            (_root && DiffersFromLoad(*_root)) || eastl::any_of(_lists.begin(), _lists.end(), [](const List& list) { return eastl::any_of(list.elements.begin(), list.elements.end(), &IsChanged); });
    }

    void ReflectedConfigDocument::Apply() {
        if (_root) {
            Patch(*_root);
        }
        for (List& list : _lists) {
            for (Element& element : list.elements) {
                if (element.type != element.writtenType) {
                    SetAttribute(element.node, kTypeAttribute, element.type->Name());
                    element.writtenType = element.type;
                }
                if (!element.hasEnabledField && element.enabled != element.writtenEnabled) {
                    SetAttribute(element.node, kEnabledAttribute, element.enabled ? "true" : "false");
                    element.writtenEnabled = element.enabled;
                }
                Patch(element);
            }
        }
        _dirty = false;
    }

}  // namespace BECore
