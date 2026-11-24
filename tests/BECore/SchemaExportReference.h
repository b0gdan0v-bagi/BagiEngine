#pragma once

#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/JsonPayload.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Control/SchemaExport.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <EASTL/sort.h>

namespace BECore::Tests {

    // The registry as JSON text the way `SchemaExport.cpp` built it before the reflected answers (a `Json` DOM, copied word for word, with the two changes of the
    // protocol: a type with no parent has no `parent`, and the method flag is `isConst`), now written by hand with a `JsonWriter`, every object with its keys in byte
    // order as the DOM sorted them. The text of the reflected answers is compared with these, so the generated writer has an oracle that shares none of its code.

    inline void WriteReferenceTypeDesc(JsonWriter& writer, const TypeDesc& desc);

    inline void WriteReferenceAttributes(JsonWriter& writer, const FieldAttributes& attributes) {
        writer.BeginObject();
        if (attributes.hidden) {
            writer.Member("hidden", true);
        }
        if (attributes.hasRange) {
            writer.Key("range");
            writer.BeginArray();
            writer.Value(attributes.rangeMin);
            writer.Value(attributes.rangeMax);
            writer.EndArray();
        }
        if (attributes.readOnly) {
            writer.Member("readonly", true);
        }
        if (attributes.required) {
            writer.Member("required", true);
        }
        if (attributes.step != 0.0) {
            writer.Member("step", attributes.step);
        }
        if (!attributes.tooltip.empty()) {
            writer.Member("tooltip", StringView(attributes.tooltip));
        }
        writer.EndObject();
    }

    inline void WriteReferenceTypeOrEmpty(JsonWriter& writer, const TypeDesc* type) {
        WriteReferenceTypeDesc(writer, type != nullptr ? *type : TypeDesc{});
    }

    /// A field entry; @p owner is the type that declares it, written for the fields of `describe_type`
    inline void WriteReferenceField(JsonWriter& writer, const FieldDesc& field, const TypeInfo* owner = nullptr) {
        writer.BeginObject();
        writer.Key("attributes");
        WriteReferenceAttributes(writer, field.attributes);
        writer.Member("name", StringView(field.name));
        if (owner != nullptr) {
            writer.Member("owner", owner->Name());
        }
        writer.Key("type");
        WriteReferenceTypeOrEmpty(writer, field.type);
        writer.EndObject();
    }

    inline void WriteReferenceMethod(JsonWriter& writer, const MethodDesc& method) {
        writer.BeginObject();
        writer.Member("callable", method.callable);
        writer.Member("isConst", method.isConst);
        writer.Member("name", StringView(method.name));
        writer.Key("params");
        writer.BeginArray();
        for (const ParamDesc& param : method.params) {
            writer.BeginObject();
            writer.Member("name", StringView(param.name));
            writer.Key("type");
            WriteReferenceTypeOrEmpty(writer, param.type);
            writer.EndObject();
        }
        writer.EndArray();
        writer.Key("result");
        WriteReferenceTypeOrEmpty(writer, method.result);
        writer.EndObject();
    }

    inline bool ReferenceIsCreatable(const TypeInfo& info) {
        return info.create != nullptr;
    }

    inline Vector<const TypeInfo*> ReferenceSortedByName(const Vector<const TypeInfo*>& infos) {
        Vector<const TypeInfo*> sorted = infos;
        eastl::sort(sorted.begin(), sorted.end(), [](const TypeInfo* a, const TypeInfo* b) { return a->Name() < b->Name(); });
        return sorted;
    }

    inline void WriteReferenceNames(JsonWriter& writer, const Vector<const TypeInfo*>& infos) {
        writer.BeginArray();
        for (const TypeInfo* info : infos) {
            writer.Value(info->Name());
        }
        writer.EndArray();
    }

    /// The members of a type entry that come after `creatable`: `factoryBase`, `fields`, `icon`, `methods`, `name`, `parent`, `purpose`
    inline void WriteReferenceTypeEntryMembers(JsonWriter& writer, const TypeInfo& info, const TypeRegistry& registry) {
        if (info.factoryBase != 0) {
            if (const TypeInfo* base = registry.Find(info.factoryBase); base != nullptr) {
                writer.Member("factoryBase", base->Name());
            }
        }
        writer.Key("fields");
        writer.BeginArray();
        for (const FieldDesc& field : info.fields) {
            WriteReferenceField(writer, field);
        }
        writer.EndArray();
        writer.Member("icon", StringView(info.meta.icon));
        writer.Key("methods");
        writer.BeginArray();
        for (const MethodDesc& method : info.methods) {
            WriteReferenceMethod(writer, method);
        }
        writer.EndArray();
        writer.Member("name", info.Name());
        if (const TypeInfo* parent = registry.Parent(info); parent != nullptr) {
            writer.Member("parent", parent->Name());
        }
        writer.Member("purpose", PurposeName(info.meta.purpose));
    }

    inline void WriteReferenceTypeEntry(JsonWriter& writer, const TypeInfo& info, const TypeRegistry& registry) {
        writer.BeginObject();
        writer.Member("abstract", info.isAbstract);
        writer.Member("creatable", ReferenceIsCreatable(info));
        WriteReferenceTypeEntryMembers(writer, info, registry);
        writer.EndObject();
    }

    inline void WriteReferenceDescribeType(JsonWriter& writer, const TypeInfo& info, const TypeRegistry& registry) {
        writer.BeginObject();
        writer.Member("abstract", info.isAbstract);
        writer.Key("allFields");
        writer.BeginArray();
        for (const FieldRef& ref : registry.AllFields(info)) {
            WriteReferenceField(writer, *ref.field, ref.owner);
        }
        writer.EndArray();
        writer.Key("bases");
        writer.BeginArray();
        for (const TypeInfo* base : registry.Bases(info)) {
            writer.Value(base->Name());
        }
        writer.EndArray();
        writer.Member("creatable", ReferenceIsCreatable(info));
        writer.Key("derived");
        WriteReferenceNames(writer, registry.Derived(info, false));
        WriteReferenceTypeEntryMembers(writer, info, registry);
        writer.EndObject();
    }

    inline void WriteReferenceListTypes(JsonWriter& writer, const TypeRegistry& registry, const ListTypesFilter& filter) {
        const Vector<const TypeInfo*>& source = filter.base != nullptr ? registry.Derived(*filter.base, filter.recursive) : registry.All();

        writer.BeginObject();
        writer.Key("types");
        writer.BeginArray();
        for (const TypeInfo* info : ReferenceSortedByName(source)) {
            if (filter.creatableOnly && !ReferenceIsCreatable(*info)) {
                continue;
            }
            writer.BeginObject();
            writer.Member("abstract", info->isAbstract);
            writer.Member("creatable", ReferenceIsCreatable(*info));
            writer.Member("fields", info->fields.size());
            writer.Member("icon", StringView(info->meta.icon));
            writer.Member("methods", info->methods.size());
            writer.Member("name", info->Name());
            if (const TypeInfo* parent = registry.Parent(*info); parent != nullptr) {
                writer.Member("parent", parent->Name());
            }
            writer.Member("purpose", PurposeName(info->meta.purpose));
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
    }

    inline void WriteReferenceTypeDesc(JsonWriter& writer, const TypeDesc& desc) {
        writer.BeginObject();
        if (desc.kind == TypeKind::Variant) {
            writer.Key("alternatives");
            writer.BeginArray();
            for (const TypeDesc* alternative : desc.alternatives) {
                WriteReferenceTypeDesc(writer, *alternative);
            }
            writer.EndArray();
        }
        if (desc.capacity != 0) {
            writer.Member("capacity", desc.capacity);
        }
        if (desc.element != nullptr) {
            writer.Key("element");
            WriteReferenceTypeDesc(writer, *desc.element);
        }
        if (desc.key != nullptr) {
            writer.Key("key");
            WriteReferenceTypeDesc(writer, *desc.key);
        }
        const bool isAny = desc.kind == TypeKind::Opaque && desc.opaqueHash == DescribeType<JsonPayload>().opaqueHash;
        writer.Member("kind", isAny ? StringView("any") : TypeKindName(desc.kind));
        if ((desc.kind == TypeKind::Object || desc.kind == TypeKind::Pointer) && !desc.ObjectName().empty()) {
            writer.Member("object", desc.ObjectName());
        }
        if (desc.kind == TypeKind::List || desc.kind == TypeKind::Map) {
            writer.Member("resizable", desc.resizable);
        }
        if (desc.kind == TypeKind::Enum) {
            writer.Key("values");
            writer.BeginArray();
            for (const StringView name : desc.names) {
                writer.Value(name);
            }
            writer.EndArray();
        }
        writer.EndObject();
    }

    inline void WriteReferenceSchema(JsonWriter& writer, const TypeRegistry& registry, const ControlRoots& roots) {
        writer.BeginObject();

        writer.Key("factories");
        writer.BeginArray();
        for (const TypeInfo* base : registry.FactoryBases()) {
            Vector<const TypeInfo*> members;
            for (const TypeInfo* info : registry.All()) {
                if (info->factoryBase == base->Hash()) {
                    members.push_back(info);
                }
            }
            writer.BeginObject();
            writer.Member("base", base->Name());
            writer.Key("derived");
            WriteReferenceNames(writer, ReferenceSortedByName(members));
            const StringView elementName = registry.FactoryElementName(*base);
            if (!elementName.empty()) {
                writer.Member("elementName", elementName);
            }
            writer.EndObject();
        }
        writer.EndArray();

        writer.Key("roots");
        writer.BeginArray();
        for (const String& name : roots.Names()) {
            writer.BeginObject();
            writer.Member("name", StringView(name));
            if (const TypedRef object = roots.Resolve(name)) {
                writer.Member("type", object.type->Name());
            }
            writer.EndObject();
        }
        writer.EndArray();

        writer.Key("types");
        writer.BeginArray();
        for (const TypeInfo* info : ReferenceSortedByName(registry.All())) {
            WriteReferenceTypeEntry(writer, *info, registry);
        }
        writer.EndArray();

        writer.Member("version", kSchemaVersion);
        writer.EndObject();
    }

    /// The reference documents as text; the `String` is what `WrittenText` of the answer is compared with
    inline String ReferenceSchemaText(const TypeRegistry& registry, const ControlRoots& roots) {
        String text;
        JsonWriter writer(text);
        WriteReferenceSchema(writer, registry, roots);
        return text;
    }

    inline String ReferenceDescribeTypeText(const TypeInfo& info, const TypeRegistry& registry) {
        String text;
        JsonWriter writer(text);
        WriteReferenceDescribeType(writer, info, registry);
        return text;
    }

    inline String ReferenceListTypesText(const TypeRegistry& registry, const ListTypesFilter& filter) {
        String text;
        JsonWriter writer(text);
        WriteReferenceListTypes(writer, registry, filter);
        return text;
    }

}  // namespace BECore::Tests
