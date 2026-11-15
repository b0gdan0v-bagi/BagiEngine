#include "TypeRegistry.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore {

    TypeRegistry& TypeRegistry::GetInstance() {
        static TypeRegistry instance;
        return instance;
    }

    bool TypeRegistry::Register(const TypeInfo& info) {
        const auto found = _byHash.find(info.Hash());
        if (found != _byHash.end()) {
            if (found->second != &info) {
                ++_refused;
                LOG_ERROR(Format("Reflection: type '{}' has the same hash as the registered type '{}'; it is not registered", info.Name(), found->second->Name()));
            }
            return false;
        }
        _byHash.emplace(info.Hash(), &info);
        _infos.push_back(&info);
        return true;
    }

    const TypeInfo* TypeRegistry::Find(StringView name) const {
        const TypeInfo* info = Find(StringUtils::GetHash(name));
        return info != nullptr && info->Name() == name ? info : nullptr;
    }

    const TypeInfo* TypeRegistry::Find(uint64_t hash) const {
        const auto found = _byHash.find(hash);
        return found != _byHash.end() ? found->second : nullptr;
    }

    const TypeInfo* TypeRegistry::Parent(const TypeInfo& info) const {
        return info.parentHash != 0 ? Find(info.parentHash) : nullptr;
    }

    Vector<const TypeInfo*> TypeRegistry::Bases(const TypeInfo& info) const {
        Vector<const TypeInfo*> bases;
        const TypeInfo* current = &info;
        // The bound stops a parent cycle (two registrations naming each other) from looping
        for (size_t step = 0; step < _infos.size(); ++step) {
            current = Parent(*current);
            if (current == nullptr) {
                break;
            }
            bases.push_back(current);
        }
        return bases;
    }

    void TypeRegistry::EnsureIndex() const {
        if (_indexedCount.load(std::memory_order_acquire) == _infos.size()) {
            return;
        }
        LockGuard lock(_indexMutex);
        if (_indexedCount.load(std::memory_order_relaxed) == _infos.size()) {
            return;
        }
        _direct.clear();
        _all.clear();
        _factoryBases.clear();
        for (const TypeInfo* info : _infos) {
            if (info->factoryBase != 0) {
                const TypeInfo* base = Find(info->factoryBase);
                if (base != nullptr && eastl::find(_factoryBases.begin(), _factoryBases.end(), base) == _factoryBases.end()) {
                    _factoryBases.push_back(base);
                }
            }
            if (const TypeInfo* parent = Parent(*info); parent != nullptr) {
                _direct[parent->Hash()].push_back(info);
            }
            for (const TypeInfo* base : Bases(*info)) {
                _all[base->Hash()].push_back(info);
            }
        }
        const auto byName = [](const TypeInfo* a, const TypeInfo* b) { return a->Name() < b->Name(); };
        eastl::sort(_factoryBases.begin(), _factoryBases.end(), byName);
        for (DerivedMap* map : {&_direct, &_all}) {
            for (auto& entry : *map) {
                eastl::sort(entry.second.begin(), entry.second.end(), byName);
            }
        }
        _indexedCount.store(_infos.size(), std::memory_order_release);
    }

    const Vector<const TypeInfo*>& TypeRegistry::DerivedFrom(const DerivedMap& map, const TypeInfo& info) const {
        const auto found = map.find(info.Hash());
        return found != map.end() ? found->second : _none;
    }

    const Vector<const TypeInfo*>& TypeRegistry::Derived(const TypeInfo& info, bool recursive) const {
        EnsureIndex();
        return DerivedFrom(recursive ? _all : _direct, info);
    }

    bool TypeRegistry::IsDerivedFrom(const TypeInfo& derived, const TypeInfo& base) const {
        const TypeInfo* current = &derived;
        // The bound stops a parent cycle from looping
        for (size_t step = 0; current != nullptr && step <= _infos.size(); ++step) {
            if (current->Hash() == base.Hash()) {
                return true;
            }
            current = Parent(*current);
        }
        return false;
    }

    Optional<size_t> TypeRegistry::OffsetOfBase(const TypeInfo& derived, const TypeInfo& base) const {
        const TypeInfo* current = &derived;
        size_t offset = 0;
        for (size_t step = 0; current != nullptr && step <= _infos.size(); ++step) {
            if (current->Hash() == base.Hash()) {
                return offset;
            }
            offset += current->parentOffset;
            current = Parent(*current);
        }
        return std::nullopt;
    }

    const Vector<const TypeInfo*>& TypeRegistry::FactoryBases() const {
        EnsureIndex();
        return _factoryBases;
    }

    OwnedObject TypeRegistry::Create(StringView name) const {
        const TypeInfo* info = Find(name);
        return info != nullptr ? Create(*info) : OwnedObject{};
    }

    OwnedObject TypeRegistry::Create(const TypeInfo& info) const {
        return info.create != nullptr ? info.create() : OwnedObject{};
    }

    Vector<FieldRef> TypeRegistry::AllFields(const TypeInfo& info) const {
        // Walk up collecting each ancestor with the offset of its subobject, then emit the root's fields first
        Vector<FieldRef> chain;
        const TypeInfo* current = &info;
        size_t offset = 0;
        for (size_t step = 0; current != nullptr && step <= _infos.size(); ++step) {
            chain.push_back(FieldRef{nullptr, current, offset});
            offset += current->parentOffset;
            current = Parent(*current);
        }

        Vector<FieldRef> fields;
        for (auto level = chain.rbegin(); level != chain.rend(); ++level) {
            for (const FieldDesc& field : level->owner->fields) {
                fields.push_back(FieldRef{&field, level->owner, level->offset + field.offset});
            }
        }
        return fields;
    }

}  // namespace BECore
