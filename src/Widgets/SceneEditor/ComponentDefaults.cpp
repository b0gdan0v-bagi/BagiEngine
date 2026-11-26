#include "ComponentDefaults.h"

#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace {

        /// The longest default text, in bytes: it is one line of a tooltip
        constexpr size_t kMaxDefaultTextBytes = 96;
        constexpr size_t kInlineResets = 16;

        CORE_ENUM(HashedCall, uint8_t, Element, Attribute, Object, Array, ArrayElement, End)

        /// A writer that keeps only a hash of the calls made on it (their order, names and values), so two fields saved alike hash alike with nothing allocated
        class HashSerializer final : public ISerializer {
        public:
            [[nodiscard]] uint64_t Hash() const {
                return _hash;
            }

            void Write(StringView name, bool value) override {
                AddValue(HashedCall::Element, name, value);
            }
            void Write(StringView name, int8_t value) override {
                AddValue(HashedCall::Element, name, value);
            }
            void Write(StringView name, uint8_t value) override {
                AddValue(HashedCall::Element, name, value);
            }
            void Write(StringView name, int16_t value) override {
                AddValue(HashedCall::Element, name, value);
            }
            void Write(StringView name, uint16_t value) override {
                AddValue(HashedCall::Element, name, value);
            }
            void Write(StringView name, int32_t value) override {
                AddValue(HashedCall::Element, name, value);
            }
            void Write(StringView name, uint32_t value) override {
                AddValue(HashedCall::Element, name, value);
            }
            void Write(StringView name, int64_t value) override {
                AddValue(HashedCall::Element, name, value);
            }
            void Write(StringView name, uint64_t value) override {
                AddValue(HashedCall::Element, name, value);
            }
            void Write(StringView name, float value) override {
                AddValue(HashedCall::Element, name, value);
            }
            void Write(StringView name, double value) override {
                AddValue(HashedCall::Element, name, value);
            }
            void Write(StringView name, StringView value) override {
                AddText(HashedCall::Element, name, value);
            }
            void Write(StringView name, const PoolString& value) override {
                AddText(HashedCall::Element, name, value.ToStringView());
            }

            void WriteAttribute(StringView name, bool value) override {
                AddValue(HashedCall::Attribute, name, value);
            }
            void WriteAttribute(StringView name, int8_t value) override {
                AddValue(HashedCall::Attribute, name, value);
            }
            void WriteAttribute(StringView name, uint8_t value) override {
                AddValue(HashedCall::Attribute, name, value);
            }
            void WriteAttribute(StringView name, int16_t value) override {
                AddValue(HashedCall::Attribute, name, value);
            }
            void WriteAttribute(StringView name, uint16_t value) override {
                AddValue(HashedCall::Attribute, name, value);
            }
            void WriteAttribute(StringView name, int32_t value) override {
                AddValue(HashedCall::Attribute, name, value);
            }
            void WriteAttribute(StringView name, uint32_t value) override {
                AddValue(HashedCall::Attribute, name, value);
            }
            void WriteAttribute(StringView name, int64_t value) override {
                AddValue(HashedCall::Attribute, name, value);
            }
            void WriteAttribute(StringView name, uint64_t value) override {
                AddValue(HashedCall::Attribute, name, value);
            }
            void WriteAttribute(StringView name, float value) override {
                AddValue(HashedCall::Attribute, name, value);
            }
            void WriteAttribute(StringView name, double value) override {
                AddValue(HashedCall::Attribute, name, value);
            }
            void WriteAttribute(StringView name, StringView value) override {
                AddText(HashedCall::Attribute, name, value);
            }
            void WriteAttribute(StringView name, const PoolString& value) override {
                AddText(HashedCall::Attribute, name, value.ToStringView());
            }

            bool BeginObject(StringView name) override {
                AddCall(HashedCall::Object, name);
                return true;
            }
            void EndObject() override {
                AddCall(HashedCall::End, {});
            }
            /// @p count may be an upper bound, so the elements that are written are what counts
            bool BeginArray(StringView name, StringView /*elementName*/, size_t& /*count*/) override {
                AddCall(HashedCall::Array, name);
                return true;
            }
            void EndArray() override {
                AddCall(HashedCall::End, {});
            }
            bool BeginArrayElement() override {
                AddCall(HashedCall::ArrayElement, {});
                return true;
            }
            void EndArrayElement() override {
                AddCall(HashedCall::End, {});
            }

        private:
            void AddBytes(const void* data, size_t size) {
                const auto* bytes = static_cast<const uint8_t*>(data);
                for (size_t i = 0; i < size; ++i) {
                    _hash ^= bytes[i];
                    _hash *= StringUtils::hashPrime;
                }
            }

            /// The length goes first, so that a name and the text after it cannot trade characters
            void AddString(StringView text) {
                const size_t size = text.size();
                AddBytes(&size, sizeof(size));
                AddBytes(text.data(), size);
            }

            void AddCall(HashedCall call, StringView name) {
                AddBytes(&call, sizeof(call));
                AddString(name);
            }

            template <typename T>
            void AddValue(HashedCall call, StringView name, T value) {
                AddCall(call, name);
                AddBytes(&value, sizeof(value));
            }

            void AddText(HashedCall call, StringView name, StringView value) {
                AddCall(call, name);
                AddString(value);
            }

            uint64_t _hash = StringUtils::hashOffset;
        };

        /// Whether a value of this type is, or holds at any depth, a pointer to an object; @p walked ends the walk of a type that holds itself
        bool HoldsObjectPointer(const TypeDesc& desc, Vector<uint64_t>& walked) {
            if (desc.kind == TypeKind::Pointer) {
                return true;
            }
            if ((desc.element != nullptr && HoldsObjectPointer(*desc.element, walked)) || (desc.key != nullptr && HoldsObjectPointer(*desc.key, walked))) {
                return true;
            }
            for (const TypeDesc* alternative : desc.alternatives) {
                if (HoldsObjectPointer(*alternative, walked)) {
                    return true;
                }
            }
            if (desc.kind != TypeKind::Object || eastl::find(walked.begin(), walked.end(), desc.ObjectHash()) != walked.end()) {
                return false;
            }
            walked.push_back(desc.ObjectHash());
            const TypeRegistry& registry = TypeRegistry::GetInstance();
            if (const TypeInfo* info = registry.Find(desc.ObjectHash())) {
                for (const FieldRef& field : registry.AllFields(*info)) {
                    if (HoldsObjectPointer(*field.field->type, walked)) {
                        return true;
                    }
                }
            }
            return false;
        }

        String JoinAttributes(pugi::xml_node element) {
            String text;
            for (const pugi::xml_attribute attribute : element.attributes()) {
                if (!text.empty()) {
                    text += ", ";
                }
                text += "{}={}"_format(attribute.name(), attribute.value());
            }
            return text;
        }

        /// What the default's save of one field wrote under @p root, as one line
        String DescribeSaved(const TypeDesc& type, StringView name, pugi::xml_node root) {
            for (const pugi::xml_attribute attribute : root.attributes()) {
                if (name == attribute.name()) {
                    return *attribute.value() != '\0' ? String(attribute.value()) : String("empty");
                }
            }
            for (const pugi::xml_node child : root.children()) {
                if (name != child.name()) {
                    continue;
                }
                const TypeDesc& shown = type.kind == TypeKind::Optional && type.element != nullptr ? *type.element : type;
                if (shown.kind == TypeKind::List || shown.kind == TypeKind::Map) {
                    size_t items = 0;
                    for (pugi::xml_node item = child.first_child(); item; item = item.next_sibling()) {
                        ++items;
                    }
                    return items == 0 ? String("empty") : "{} items"_format(items);
                }
                String text = JoinAttributes(child);
                if (child.first_child()) {
                    text += text.empty() ? "..." : ", ...";
                }
                return text.empty() ? String("empty") : text;
            }
            return "none";
        }

        /// Cuts @p text to the longest default text, never inside a UTF-8 sequence
        void CutToOneTooltipLine(String& text) {
            if (text.size() <= kMaxDefaultTextBytes) {
                return;
            }
            constexpr uint8_t kContinuationMask = 0xC0;
            constexpr uint8_t kContinuationBits = 0x80;
            size_t size = kMaxDefaultTextBytes;
            while (size > 0 && (static_cast<uint8_t>(text[size]) & kContinuationMask) == kContinuationBits) {
                --size;
            }
            text.resize(size);
            text += "...";
        }

    }  // namespace

    struct ComponentDefaults::Field {
        const FieldDesc* desc = nullptr;
        /// From the address of the component's own type to the subobject that declares the field: what `save` and `load` take
        size_t ownerOffset = 0;
        uint64_t defaultHash = 0;
        String defaultText;

        [[nodiscard]] void* OwnerIn(const TypedRef& object) const {
            return static_cast<char*>(object.object) + ownerOffset;
        }

        [[nodiscard]] uint64_t HashIn(const TypedRef& object) const {
            HashSerializer hash;
            desc->save(OwnerIn(object), hash);
            return hash.Hash();
        }

        [[nodiscard]] bool Resettable() const {
            return desc->load != nullptr && !desc->attributes.readOnly;
        }
    };

    struct ComponentDefaults::Prototype {
        /// Empty for a type the factory cannot make, which has no field here
        IntrusivePtr<IComponent> component;
        Vector<Field> fields;

        [[nodiscard]] const Field* Find(StringView name) const {
            for (const Field& field : fields) {
                if (field.desc->name == name) {
                    return &field;
                }
            }
            return nullptr;
        }

        /// Writes the default of @p field into @p target through the XML of the one and the load of the other
        [[nodiscard]] bool Restore(const Field& field, const TypedRef& target) const {
            XmlSerializer saved;
            field.desc->save(field.OwnerIn(TypedRef::Of(*component)), saved);
            XmlDeserializer reader;
            if (!reader.LoadFromXmlNode(XmlNode{saved.GetRootNode()})) {
                return false;
            }
            field.desc->load(field.OwnerIn(target), reader);
            if (reader.HasErrors()) {
                LogDeserializeErrors("ComponentDefaults", reader);
                return false;
            }
            return true;
        }
    };

    ComponentDefaults::ComponentDefaults() = default;
    ComponentDefaults::~ComponentDefaults() = default;

    const ComponentDefaults::Prototype& ComponentDefaults::PrototypeOf(const ClassMeta& meta) {
        if (const auto found = _prototypes.find(meta.typeHash); found != _prototypes.end()) {
            return *found->second;
        }
        UniquePtr<Prototype> prototype = MakeUnique<Prototype>();
        prototype->component = AbstractFactory<IComponent>::GetInstance().Create(meta);
        if (prototype->component) {
            if (const TypedRef defaults = TypedRef::Of(*prototype->component)) {
                const TypeRegistry& registry = TypeRegistry::GetInstance();
                for (const FieldRef& ref : registry.AllFields(*defaults.type)) {
                    const FieldDesc& desc = *ref.field;
                    Vector<uint64_t> walked;
                    if (desc.attributes.hidden || desc.save == nullptr || HoldsObjectPointer(*desc.type, walked)) {
                        continue;
                    }
                    Field field{&desc, ref.offset - desc.offset};
                    field.defaultHash = field.HashIn(defaults);
                    XmlSerializer saved;
                    desc.save(field.OwnerIn(defaults), saved);
                    field.defaultText = DescribeSaved(*desc.type, desc.name, saved.GetRootNode());
                    CutToOneTooltipLine(field.defaultText);
                    prototype->fields.push_back(eastl::move(field));
                }
            }
        }
        return *_prototypes.emplace(meta.typeHash, eastl::move(prototype)).first->second;
    }

    Span<const ComponentFieldDifference> ComponentDefaults::Compare(IComponent& component) {
        const Prototype& prototype = PrototypeOf(component.GetTypeMeta());
        _differences.clear();
        _differences.reserve(prototype.fields.size());
        if (const TypedRef live = TypedRef::Of(component)) {
            for (const Field& field : prototype.fields) {
                if (field.HashIn(live) != field.defaultHash) {
                    _differences.push_back({field.desc->name, StringView(field.defaultText.data(), field.defaultText.size()), field.Resettable()});
                }
            }
        }
        return {_differences.data(), _differences.size()};
    }

    Optional<StringView> ComponentDefaults::DefaultText(IComponent& component, StringView field) {
        return DefaultText(component.GetTypeMeta(), field);
    }

    Optional<StringView> ComponentDefaults::DefaultText(const ClassMeta& type, StringView field) {
        const Field* const found = PrototypeOf(type).Find(field);
        if (found == nullptr) {
            return {};
        }
        return StringView(found->defaultText.data(), found->defaultText.size());
    }

    bool ComponentDefaults::Reset(IComponent& component, StringView field) {
        const Prototype& prototype = PrototypeOf(component.GetTypeMeta());
        const Field* const found = prototype.Find(field);
        const TypedRef live = TypedRef::Of(component);
        if (found == nullptr || !live || !found->Resettable() || found->HashIn(live) == found->defaultHash || !prototype.Restore(*found, live)) {
            return false;
        }
        component.OnPropertyWritten(found->desc->name);
        return true;
    }

    bool ComponentDefaults::ResetAll(IComponent& component) {
        const Prototype& prototype = PrototypeOf(component.GetTypeMeta());
        const TypedRef live = TypedRef::Of(component);
        if (!live) {
            return false;
        }
        SmallVector<StringView, kInlineResets> written;
        for (const Field& field : prototype.fields) {
            if (field.Resettable() && field.HashIn(live) != field.defaultHash && prototype.Restore(field, live)) {
                written.push_back(field.desc->name);
            }
        }
        for (const StringView name : written) {
            component.OnPropertyWritten(name);
        }
        return !written.empty();
    }

}  // namespace BECore
