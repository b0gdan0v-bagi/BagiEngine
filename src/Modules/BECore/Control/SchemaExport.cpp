#include "SchemaExport.h"

#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/Json.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        Json AttributesJson(const FieldAttributes& attributes) {
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

        Json FieldJson(const FieldDesc& field) {
            Json json = Json::object();
            json["name"] = MakeJsonString(field.name);
            json["type"] = field.type != nullptr ? TypeDescJson(*field.type) : TypeDescJson(TypeDesc{});
            json["attributes"] = AttributesJson(field.attributes);
            return json;
        }

        Json MethodJson(const MethodDesc& method) {
            Json json = Json::object();
            json["name"] = MakeJsonString(method.name);
            json["const"] = method.isConst;
            json["callable"] = method.callable;
            Json params = Json::array();
            for (const ParamDesc& param : method.params) {
                Json entry = Json::object();
                entry["name"] = MakeJsonString(param.name);
                entry["type"] = param.type != nullptr ? TypeDescJson(*param.type) : TypeDescJson(TypeDesc{});
                params.push_back(std::move(entry));
            }
            json["params"] = std::move(params);
            json["result"] = method.result != nullptr ? TypeDescJson(*method.result) : TypeDescJson(TypeDesc{});
            return json;
        }

        bool IsCreatable(const TypeInfo& info) {
            return info.create != nullptr;
        }

        Json ParentJson(const TypeInfo& info, const TypeRegistry& registry) {
            const TypeInfo* parent = registry.Parent(info);
            return parent != nullptr ? MakeJsonString(parent->Name()) : Json();
        }

        Vector<const TypeInfo*> SortedByName(const Vector<const TypeInfo*>& infos) {
            Vector<const TypeInfo*> sorted = infos;
            eastl::sort(sorted.begin(), sorted.end(), [](const TypeInfo* a, const TypeInfo* b) { return a->Name() < b->Name(); });
            return sorted;
        }

        Json NamesOf(const Vector<const TypeInfo*>& infos) {
            Json names = Json::array();
            for (const TypeInfo* info : infos) {
                names.push_back(MakeJsonString(info->Name()));
            }
            return names;
        }

        Json FactoriesJson(const TypeRegistry& registry) {
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
                entry["derived"] = NamesOf(SortedByName(members));
                factories.push_back(std::move(entry));
            }
            return factories;
        }

        Json RootsJson(const ControlRoots& roots) {
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

    }  // namespace

    Json TypeDescJson(const TypeDesc& desc) {
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
                    alternatives.push_back(TypeDescJson(*alternative));
                }
                json["alternatives"] = std::move(alternatives);
                break;
            }
            default:
                break;
        }
        if (desc.element != nullptr) {
            json["element"] = TypeDescJson(*desc.element);
        }
        if (desc.key != nullptr) {
            json["key"] = TypeDescJson(*desc.key);
        }
        if (desc.capacity != 0) {
            json["capacity"] = desc.capacity;
        }
        return json;
    }

    Json TypeEntryJson(const TypeInfo& info, const TypeRegistry& registry) {
        Json json = Json::object();
        json["name"] = MakeJsonString(info.Name());
        json["parent"] = ParentJson(info, registry);
        json["abstract"] = info.isAbstract;
        json["creatable"] = IsCreatable(info);
        json["purpose"] = MakeJsonString(PurposeName(info.meta.purpose));
        json["icon"] = MakeJsonString(info.meta.icon);
        if (info.factoryBase != 0) {
            const TypeInfo* base = registry.Find(info.factoryBase);
            if (base != nullptr) {
                json["factoryBase"] = MakeJsonString(base->Name());
            }
        }
        Json fields = Json::array();
        for (const FieldDesc& field : info.fields) {
            fields.push_back(FieldJson(field));
        }
        json["fields"] = std::move(fields);
        Json methods = Json::array();
        for (const MethodDesc& method : info.methods) {
            methods.push_back(MethodJson(method));
        }
        json["methods"] = std::move(methods);
        return json;
    }

    Json DescribeTypeJson(const TypeInfo& info, const TypeRegistry& registry) {
        Json json = TypeEntryJson(info, registry);

        Json bases = Json::array();
        for (const TypeInfo* base : registry.Bases(info)) {
            bases.push_back(MakeJsonString(base->Name()));
        }
        json["bases"] = std::move(bases);
        json["derived"] = NamesOf(registry.Derived(info, false));

        Json allFields = Json::array();
        for (const FieldRef& ref : registry.AllFields(info)) {
            Json entry = FieldJson(*ref.field);
            entry["owner"] = MakeJsonString(ref.owner->Name());
            allFields.push_back(std::move(entry));
        }
        json["allFields"] = std::move(allFields);
        return json;
    }

    Json ListTypesJson(const TypeRegistry& registry, const ListTypesFilter& filter) {
        const Vector<const TypeInfo*>& source = filter.base != nullptr ? registry.Derived(*filter.base, filter.recursive) : registry.All();

        Json types = Json::array();
        for (const TypeInfo* info : SortedByName(source)) {
            if (filter.creatableOnly && !IsCreatable(*info)) {
                continue;
            }
            Json entry = Json::object();
            entry["name"] = MakeJsonString(info->Name());
            entry["parent"] = ParentJson(*info, registry);
            entry["abstract"] = info->isAbstract;
            entry["creatable"] = IsCreatable(*info);
            entry["purpose"] = MakeJsonString(PurposeName(info->meta.purpose));
            entry["icon"] = MakeJsonString(info->meta.icon);
            entry["fields"] = info->fields.size();
            entry["methods"] = info->methods.size();
            types.push_back(std::move(entry));
        }
        Json json = Json::object();
        json["types"] = std::move(types);
        return json;
    }

    Json BuildSchemaJson(const TypeRegistry& registry, const ControlRoots& roots) {
        Json types = Json::array();
        for (const TypeInfo* info : SortedByName(registry.All())) {
            types.push_back(TypeEntryJson(*info, registry));
        }
        Json json = Json::object();
        json["version"] = kSchemaVersion;
        json["types"] = std::move(types);
        json["factories"] = FactoriesJson(registry);
        json["roots"] = RootsJson(roots);
        return json;
    }

}  // namespace BECore
