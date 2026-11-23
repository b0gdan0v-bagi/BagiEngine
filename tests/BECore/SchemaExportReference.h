#pragma once

#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/SchemaExport.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <EASTL/sort.h>

namespace BECore::Tests {

    // The registry as JSON the way `SchemaExport.cpp` built it before the reflected answers: a `Json` DOM, copied word for word, with the two changes of the protocol
    // (a type with no parent has no `parent`, and the method flag is `isConst`). The text of the reflected answers is compared with `DumpJson` of these.

    inline Json ReferenceTypeDescJson(const TypeDesc& desc);

    inline Json ReferenceAttributesJson(const FieldAttributes& attributes) {
        Json json = Json::object();
        if (attributes.hasRange) {
            json["range"] = Json::array({attributes.rangeMin, attributes.rangeMax});
        }
        if (attributes.step != 0.0) {
            json["step"] = attributes.step;
        }
        if (!attributes.tooltip.empty()) {
            json["tooltip"] = MakeJsonString(attributes.tooltip);
        }
        if (attributes.readOnly) {
            json["readonly"] = true;
        }
        if (attributes.hidden) {
            json["hidden"] = true;
        }
        if (attributes.required) {
            json["required"] = true;
        }
        return json;
    }

    inline Json ReferenceFieldJson(const FieldDesc& field) {
        Json json = Json::object();
        json["name"] = MakeJsonString(field.name);
        json["type"] = field.type != nullptr ? ReferenceTypeDescJson(*field.type) : ReferenceTypeDescJson(TypeDesc{});
        json["attributes"] = ReferenceAttributesJson(field.attributes);
        return json;
    }

    inline Json ReferenceMethodJson(const MethodDesc& method) {
        Json json = Json::object();
        json["name"] = MakeJsonString(method.name);
        json["isConst"] = method.isConst;
        json["callable"] = method.callable;
        Json params = Json::array();
        for (const ParamDesc& param : method.params) {
            Json entry = Json::object();
            entry["name"] = MakeJsonString(param.name);
            entry["type"] = param.type != nullptr ? ReferenceTypeDescJson(*param.type) : ReferenceTypeDescJson(TypeDesc{});
            params.push_back(std::move(entry));
        }
        json["params"] = std::move(params);
        json["result"] = method.result != nullptr ? ReferenceTypeDescJson(*method.result) : ReferenceTypeDescJson(TypeDesc{});
        return json;
    }

    inline bool ReferenceIsCreatable(const TypeInfo& info) {
        return info.create != nullptr;
    }

    /// The members the type entry and the `list_types` entry share
    inline Json ReferenceTypeHeadJson(const TypeInfo& info, const TypeRegistry& registry) {
        Json json = Json::object();
        json["name"] = MakeJsonString(info.Name());
        if (const TypeInfo* parent = registry.Parent(info); parent != nullptr) {
            json["parent"] = MakeJsonString(parent->Name());
        }
        json["abstract"] = info.isAbstract;
        json["creatable"] = ReferenceIsCreatable(info);
        json["purpose"] = MakeJsonString(PurposeName(info.meta.purpose));
        json["icon"] = MakeJsonString(info.meta.icon);
        return json;
    }

    inline Vector<const TypeInfo*> ReferenceSortedByName(const Vector<const TypeInfo*>& infos) {
        Vector<const TypeInfo*> sorted = infos;
        eastl::sort(sorted.begin(), sorted.end(), [](const TypeInfo* a, const TypeInfo* b) { return a->Name() < b->Name(); });
        return sorted;
    }

    inline Json ReferenceNamesOf(const Vector<const TypeInfo*>& infos) {
        Json names = Json::array();
        for (const TypeInfo* info : infos) {
            names.push_back(MakeJsonString(info->Name()));
        }
        return names;
    }

    inline Json ReferenceFactoriesJson(const TypeRegistry& registry) {
        Json factories = Json::array();
        for (const TypeInfo* base : registry.FactoryBases()) {
            Vector<const TypeInfo*> members;
            for (const TypeInfo* info : registry.All()) {
                if (info->factoryBase == base->Hash()) {
                    members.push_back(info);
                }
            }
            Json entry = Json::object();
            entry["base"] = MakeJsonString(base->Name());
            const StringView elementName = registry.FactoryElementName(*base);
            if (!elementName.empty()) {
                entry["elementName"] = MakeJsonString(elementName);
            }
            entry["derived"] = ReferenceNamesOf(ReferenceSortedByName(members));
            factories.push_back(std::move(entry));
        }
        return factories;
    }

    inline Json ReferenceRootsJson(const ControlRoots& roots) {
        Json json = Json::array();
        for (const String& name : roots.Names()) {
            Json entry = Json::object();
            entry["name"] = MakeJsonString(name);
            const TypedRef object = roots.Resolve(name);
            if (object) {
                entry["type"] = MakeJsonString(object.type->Name());
            }
            json.push_back(std::move(entry));
        }
        return json;
    }

    Json ReferenceTypeDescJson(const TypeDesc& desc) {
        Json json = Json::object();
        json["kind"] = MakeJsonString(TypeKindName(desc.kind));
        switch (desc.kind) {
            case TypeKind::Object:
            case TypeKind::Pointer:
                if (!desc.ObjectName().empty()) {
                    json["object"] = MakeJsonString(desc.ObjectName());
                }
                break;
            case TypeKind::Enum: {
                Json values = Json::array();
                for (const StringView name : desc.names) {
                    values.push_back(MakeJsonString(name));
                }
                json["values"] = std::move(values);
                break;
            }
            case TypeKind::List:
            case TypeKind::Map:
                json["resizable"] = desc.resizable;
                break;
            case TypeKind::Variant: {
                Json alternatives = Json::array();
                for (const TypeDesc* alternative : desc.alternatives) {
                    alternatives.push_back(ReferenceTypeDescJson(*alternative));
                }
                json["alternatives"] = std::move(alternatives);
                break;
            }
            default:
                break;
        }
        if (desc.element != nullptr) {
            json["element"] = ReferenceTypeDescJson(*desc.element);
        }
        if (desc.key != nullptr) {
            json["key"] = ReferenceTypeDescJson(*desc.key);
        }
        if (desc.capacity != 0) {
            json["capacity"] = desc.capacity;
        }
        return json;
    }

    Json ReferenceTypeEntryJson(const TypeInfo& info, const TypeRegistry& registry) {
        Json json = ReferenceTypeHeadJson(info, registry);
        if (info.factoryBase != 0) {
            const TypeInfo* base = registry.Find(info.factoryBase);
            if (base != nullptr) {
                json["factoryBase"] = MakeJsonString(base->Name());
            }
        }
        Json fields = Json::array();
        for (const FieldDesc& field : info.fields) {
            fields.push_back(ReferenceFieldJson(field));
        }
        json["fields"] = std::move(fields);
        Json methods = Json::array();
        for (const MethodDesc& method : info.methods) {
            methods.push_back(ReferenceMethodJson(method));
        }
        json["methods"] = std::move(methods);
        return json;
    }

    Json ReferenceDescribeTypeJson(const TypeInfo& info, const TypeRegistry& registry) {
        Json json = ReferenceTypeEntryJson(info, registry);

        Json bases = Json::array();
        for (const TypeInfo* base : registry.Bases(info)) {
            bases.push_back(MakeJsonString(base->Name()));
        }
        json["bases"] = std::move(bases);
        json["derived"] = ReferenceNamesOf(registry.Derived(info, false));

        Json allFields = Json::array();
        for (const FieldRef& ref : registry.AllFields(info)) {
            Json entry = ReferenceFieldJson(*ref.field);
            entry["owner"] = MakeJsonString(ref.owner->Name());
            allFields.push_back(std::move(entry));
        }
        json["allFields"] = std::move(allFields);
        return json;
    }

    Json ReferenceListTypesJson(const TypeRegistry& registry, const ListTypesFilter& filter) {
        const Vector<const TypeInfo*>& source = filter.base != nullptr ? registry.Derived(*filter.base, filter.recursive) : registry.All();

        Json types = Json::array();
        for (const TypeInfo* info : ReferenceSortedByName(source)) {
            if (filter.creatableOnly && !ReferenceIsCreatable(*info)) {
                continue;
            }
            Json entry = ReferenceTypeHeadJson(*info, registry);
            entry["fields"] = info->fields.size();
            entry["methods"] = info->methods.size();
            types.push_back(std::move(entry));
        }
        Json json = Json::object();
        json["types"] = std::move(types);
        return json;
    }

    Json ReferenceBuildSchemaJson(const TypeRegistry& registry, const ControlRoots& roots) {
        Json types = Json::array();
        for (const TypeInfo* info : ReferenceSortedByName(registry.All())) {
            types.push_back(ReferenceTypeEntryJson(*info, registry));
        }
        Json json = Json::object();
        json["version"] = kSchemaVersion;
        json["types"] = std::move(types);
        json["factories"] = ReferenceFactoriesJson(registry);
        json["roots"] = ReferenceRootsJson(roots);
        return json;
    }

}  // namespace BECore::Tests
