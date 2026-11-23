#include "StagedChanges.h"

namespace BECore {

    StagedChanges::Entry& StagedChanges::EntryFor(StringView key) {
        if (const auto it = _changes.find(key); it != _changes.end()) {
            return it->second;
        }
        return _changes[String(key.data(), key.size())];
    }

    void StagedChanges::Set(StringView key, pugi::xml_attribute attr, StringView newValue) {
        auto& entry = EntryFor(key);
        entry.value.assign(newValue.data(), newValue.size());
        entry.attr = attr;
        entry.textNode = pugi::xml_node{};
    }

    void StagedChanges::SetText(StringView key, pugi::xml_node textNode, StringView newValue) {
        auto& entry = EntryFor(key);
        entry.value.assign(newValue.data(), newValue.size());
        entry.attr = pugi::xml_attribute{};
        entry.textNode = textNode;
    }

    const String* StagedChanges::Get(StringView key) const {
        const auto it = _changes.find(key);
        return it != _changes.end() ? &it->second.value : nullptr;
    }

    bool StagedChanges::Has(StringView key) const {
        return _changes.find(key) != _changes.end();
    }

    void StagedChanges::Clear() {
        _changes.clear();
    }

    bool StagedChanges::IsEmpty() const {
        return _changes.empty();
    }

    void StagedChanges::ApplyTo(pugi::xml_node /*root*/) {
        for (auto& [key, entry] : _changes) {
            if (!entry.attr.empty()) {
                entry.attr.set_value(entry.value.c_str());
            } else if (!entry.textNode.empty()) {
                entry.textNode.set_value(entry.value.c_str());
            }
        }
    }

    void StagedChanges::ForEach(FunctionRef<void(const String&, const String&)> fn) const {
        for (const auto& [key, entry] : _changes) {
            fn(key, entry.value);
        }
    }

}  // namespace BECore
