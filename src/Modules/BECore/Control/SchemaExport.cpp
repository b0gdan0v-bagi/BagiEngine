#include "SchemaExport.h"

#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/JsonPayload.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        /// The texts of a schema answer are the registry's own (names of the program, literals), which outlive the answer
        TextView ToText(StringView view) {
            return TextView(view);
        }

        TypeSchemaAttributes MakeAttributes(const FieldAttributes& attributes) {
            TypeSchemaAttributes result;
            if (attributes.hasRange) {
                result.range = ArenaVector<double>{attributes.rangeMin, attributes.rangeMax};
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
            result.params.reserve(method.params.size());
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

        template <typename TInfoList>
        ArenaVector<const TypeInfo*> SortedByName(const TInfoList& infos) {
            ArenaVector<const TypeInfo*> sorted(infos.begin(), infos.end());
            eastl::sort(sorted.begin(), sorted.end(), [](const TypeInfo* a, const TypeInfo* b) { return a->Name() < b->Name(); });
            return sorted;
        }

        template <typename TInfoList>
        ArenaVector<TextView> NamesOf(const TInfoList& infos) {
            ArenaVector<TextView> names;
            names.reserve(infos.size());
            for (const TypeInfo* info : infos) {
                names.push_back(ToText(info->Name()));
            }
            return names;
        }

        ArenaVector<TypeSchemaFactory> MakeFactories(const TypeRegistry& registry) {
            ArenaVector<TypeSchemaFactory> factories;
            factories.reserve(registry.FactoryBases().size());
            for (const TypeInfo* base : registry.FactoryBases()) {
                const auto isMember = [&base](const TypeInfo* info) { return info->factoryBase == base->Hash(); };
                // Counted first: a doubling vector leaves every old block in the arena until the scope of the answer ends
                ArenaVector<const TypeInfo*> members;
                members.reserve(static_cast<size_t>(eastl::count_if(registry.All().begin(), registry.All().end(), isMember)));
                for (const TypeInfo* info : registry.All()) {
                    if (isMember(info)) {
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

        ArenaVector<TypeSchemaRoot> MakeRoots(const ControlRoots& roots) {
            const Vector<String> names = roots.Names();
            ArenaVector<TypeSchemaRoot> result;
            result.reserve(names.size());
            for (const String& name : names) {
                TypeSchemaRoot& entry = result.emplace_back();
                entry.name = ArenaString(name.data(), name.size());
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
            entry.fields.reserve(info.fields.size());
            for (const FieldDesc& field : info.fields) {
                entry.fields.push_back(MakeField(field));
            }
            entry.methods.reserve(info.methods.size());
            for (const MethodDesc& method : info.methods) {
                entry.methods.push_back(MakeMethod(method));
            }
        }

    }  // namespace

    IntrusivePtr<TypeSchemaDesc> MakeTypeSchemaDesc(const TypeDesc& desc) {
        IntrusivePtr<TypeSchemaDesc> result = New<TypeSchemaDesc>();
        result->kind = ToText(TypeKindName(desc.kind));
        if (desc.kind == TypeKind::Opaque && desc.opaqueHash == DescribeType<JsonPayload>().opaqueHash) {
            result->kind = TextView("any");  // a foreign JSON value: no kind of its own in TypeKind, which the layout hash and the `Values/` switches would all have to learn
        }
        switch (desc.kind) {
            case TypeKind::Object:
            case TypeKind::Pointer:
                if (!desc.ObjectName().empty()) {
                    result->object = ToText(desc.ObjectName());
                }
                break;
            case TypeKind::Enum: {
                ArenaVector<TextView> values;
                values.reserve(desc.names.size());
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
                ArenaVector<IntrusivePtr<TypeSchemaDesc>> alternatives;
                alternatives.reserve(desc.alternatives.size());
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
        const Vector<const TypeInfo*> bases = registry.Bases(info);
        described.bases.reserve(bases.size());
        for (const TypeInfo* base : bases) {
            described.bases.push_back(ToText(base->Name()));
        }
        described.derived = NamesOf(registry.Derived(info, false));
        const Vector<FieldRef> allFields = registry.AllFields(info);
        described.allFields.reserve(allFields.size());
        for (const FieldRef& ref : allFields) {
            TypeSchemaOwnedField& entry = described.allFields.emplace_back();
            static_cast<TypeSchemaField&>(entry) = MakeField(*ref.field);
            entry.owner = ToText(ref.owner->Name());
        }
        return described;
    }

    ListTypesAnswer MakeListTypesAnswer(const TypeRegistry& registry, const ListTypesFilter& filter) {
        const Vector<const TypeInfo*>& source = filter.base != nullptr ? registry.Derived(*filter.base, filter.recursive) : registry.All();

        ListTypesAnswer answer;
        const ArenaVector<const TypeInfo*> sorted = SortedByName(source);
        answer.types.reserve(sorted.size());
        for (const TypeInfo* info : sorted) {
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
        const ArenaVector<const TypeInfo*> sorted = SortedByName(registry.All());
        answer.types.reserve(sorted.size());
        for (const TypeInfo* info : sorted) {
            answer.types.push_back(MakeTypeSchemaEntry(*info, registry));
        }
        answer.factories = MakeFactories(registry);
        answer.roots = MakeRoots(roots);
        return answer;
    }

}  // namespace BECore
