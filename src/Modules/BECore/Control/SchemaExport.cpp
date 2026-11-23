#include "SchemaExport.h"

#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/JsonPayload.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        String ToText(StringView view) {
            return String(view.data(), view.size());
        }

        TypeSchemaAttributes MakeAttributes(const FieldAttributes& attributes) {
            TypeSchemaAttributes result;
            if (attributes.hasRange) {
                result.range = Vector<double>{attributes.rangeMin, attributes.rangeMax};
            }
            if (attributes.step != 0.0) {
                result.step = attributes.step;
            }
            if (!attributes.tooltip.empty()) {
                result.tooltip = ToText(attributes.tooltip);
            }
            if (attributes.readOnly) {
                result.readonly = true;
            }
            if (attributes.hidden) {
                result.hidden = true;
            }
            if (attributes.required) {
                result.required = true;
            }
            return result;
        }

        /// A type the reflection could not read is `unknown`, the descriptor a default `TypeDesc` is
        IntrusivePtr<TypeSchemaDesc> MakeTypeOrUnknown(const TypeDesc* desc) {
            return MakeTypeSchemaDesc(desc != nullptr ? *desc : TypeDesc{});
        }

        TypeSchemaField MakeField(const FieldDesc& field) {
            TypeSchemaField result;
            result.name = ToText(field.name);
            result.type = MakeTypeOrUnknown(field.type);
            result.attributes = MakeAttributes(field.attributes);
            return result;
        }

        TypeSchemaMethod MakeMethod(const MethodDesc& method) {
            TypeSchemaMethod result;
            result.name = ToText(method.name);
            result.isConst = method.isConst;
            result.callable = method.callable;
            for (const ParamDesc& param : method.params) {
                TypeSchemaParam& entry = result.params.emplace_back();
                entry.name = ToText(param.name);
                entry.type = MakeTypeOrUnknown(param.type);
            }
            result.result = MakeTypeOrUnknown(method.result);
            return result;
        }

        bool IsCreatable(const TypeInfo& info) {
            return info.create != nullptr;
        }

        /// The members the type entry and the `list_types` row share
        void FillHead(TypeSchemaHead& head, const TypeInfo& info, const TypeRegistry& registry) {
            head.name = ToText(info.Name());
            const TypeInfo* parent = registry.Parent(info);
            if (parent != nullptr) {
                head.parent = ToText(parent->Name());
            }
            head.abstract = info.isAbstract;
            head.creatable = IsCreatable(info);
            head.purpose = ToText(PurposeName(info.meta.purpose));
            head.icon = ToText(info.meta.icon);
        }

        Vector<const TypeInfo*> SortedByName(const Vector<const TypeInfo*>& infos) {
            Vector<const TypeInfo*> sorted = infos;
            eastl::sort(sorted.begin(), sorted.end(), [](const TypeInfo* a, const TypeInfo* b) { return a->Name() < b->Name(); });
            return sorted;
        }

        Vector<String> NamesOf(const Vector<const TypeInfo*>& infos) {
            Vector<String> names;
            names.reserve(infos.size());
            for (const TypeInfo* info : infos) {
                names.push_back(ToText(info->Name()));
            }
            return names;
        }

        Vector<TypeSchemaFactory> MakeFactories(const TypeRegistry& registry) {
            Vector<TypeSchemaFactory> factories;
            for (const TypeInfo* base : registry.FactoryBases()) {
                Vector<const TypeInfo*> members;
                for (const TypeInfo* info : registry.All()) {
                    if (info->factoryBase == base->Hash()) {
                        members.push_back(info);
                    }
                }
                TypeSchemaFactory& entry = factories.emplace_back();
                entry.base = ToText(base->Name());
                const StringView elementName = registry.FactoryElementName(*base);
                if (!elementName.empty()) {
                    entry.elementName = ToText(elementName);
                }
                entry.derived = NamesOf(SortedByName(members));
            }
            return factories;
        }

        Vector<TypeSchemaRoot> MakeRoots(const ControlRoots& roots) {
            Vector<TypeSchemaRoot> result;
            for (const String& name : roots.Names()) {
                TypeSchemaRoot& entry = result.emplace_back();
                entry.name = name;
                const TypedRef object = roots.Resolve(name);
                if (object) {
                    entry.type = ToText(object.type->Name());
                }
            }
            return result;
        }

        void FillEntry(TypeSchemaEntry& entry, const TypeInfo& info, const TypeRegistry& registry) {
            FillHead(entry, info, registry);
            if (info.factoryBase != 0) {
                const TypeInfo* base = registry.Find(info.factoryBase);
                if (base != nullptr) {
                    entry.factoryBase = ToText(base->Name());
                }
            }
            for (const FieldDesc& field : info.fields) {
                entry.fields.push_back(MakeField(field));
            }
            for (const MethodDesc& method : info.methods) {
                entry.methods.push_back(MakeMethod(method));
            }
        }

    }  // namespace

    IntrusivePtr<TypeSchemaDesc> MakeTypeSchemaDesc(const TypeDesc& desc) {
        IntrusivePtr<TypeSchemaDesc> result = New<TypeSchemaDesc>();
        result->kind = ToText(TypeKindName(desc.kind));
        if (desc.kind == TypeKind::Opaque && desc.opaqueHash == DescribeType<JsonPayload>().opaqueHash) {
            result->kind = "any";  // a foreign JSON value: no kind of its own in TypeKind, which the layout hash and the `Values/` switches would all have to learn
        }
        switch (desc.kind) {
            case TypeKind::Object:
            case TypeKind::Pointer:
                if (!desc.ObjectName().empty()) {
                    result->object = ToText(desc.ObjectName());
                }
                break;
            case TypeKind::Enum: {
                Vector<String> values;
                for (const StringView name : desc.names) {
                    values.push_back(ToText(name));
                }
                result->values = std::move(values);
                break;
            }
            case TypeKind::List:
            case TypeKind::Map:
                result->resizable = desc.resizable;
                break;
            case TypeKind::Variant: {
                Vector<IntrusivePtr<TypeSchemaDesc>> alternatives;
                for (const TypeDesc* alternative : desc.alternatives) {
                    alternatives.push_back(MakeTypeSchemaDesc(*alternative));
                }
                result->alternatives = std::move(alternatives);
                break;
            }
            default:
                break;
        }
        if (desc.element != nullptr) {
            result->element = MakeTypeSchemaDesc(*desc.element);
        }
        if (desc.key != nullptr) {
            result->key = MakeTypeSchemaDesc(*desc.key);
        }
        if (desc.capacity != 0) {
            result->capacity = static_cast<uint64_t>(desc.capacity);
        }
        return result;
    }

    TypeSchemaEntry MakeTypeSchemaEntry(const TypeInfo& info, const TypeRegistry& registry) {
        TypeSchemaEntry entry;
        FillEntry(entry, info, registry);
        return entry;
    }

    TypeSchemaDescribed DescribeTypeSchema(const TypeInfo& info, const TypeRegistry& registry) {
        TypeSchemaDescribed described;
        FillEntry(described, info, registry);
        for (const TypeInfo* base : registry.Bases(info)) {
            described.bases.push_back(ToText(base->Name()));
        }
        described.derived = NamesOf(registry.Derived(info, false));
        for (const FieldRef& ref : registry.AllFields(info)) {
            TypeSchemaOwnedField& entry = described.allFields.emplace_back();
            static_cast<TypeSchemaField&>(entry) = MakeField(*ref.field);
            entry.owner = ToText(ref.owner->Name());
        }
        return described;
    }

    ListTypesAnswer MakeListTypesAnswer(const TypeRegistry& registry, const ListTypesFilter& filter) {
        const Vector<const TypeInfo*>& source = filter.base != nullptr ? registry.Derived(*filter.base, filter.recursive) : registry.All();

        ListTypesAnswer answer;
        for (const TypeInfo* info : SortedByName(source)) {
            if (filter.creatableOnly && !IsCreatable(*info)) {
                continue;
            }
            TypeSchemaListed& row = answer.types.emplace_back();
            FillHead(row, *info, registry);
            row.fields = static_cast<uint64_t>(info->fields.size());
            row.methods = static_cast<uint64_t>(info->methods.size());
        }
        return answer;
    }

    SchemaAnswer MakeSchemaAnswer(const TypeRegistry& registry, const ControlRoots& roots) {
        SchemaAnswer answer;
        answer.version = kSchemaVersion;
        for (const TypeInfo* info : SortedByName(registry.All())) {
            answer.types.push_back(MakeTypeSchemaEntry(*info, registry));
        }
        answer.factories = MakeFactories(registry);
        answer.roots = MakeRoots(roots);
        return answer;
    }

}  // namespace BECore
