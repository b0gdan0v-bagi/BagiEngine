#include "StagedChanges.h"

namespace BECore {

    void StagedChanges::Set(const String& key, pugi::xml_attribute attr, StringView newValue) {
        auto& entry = _changes[key];
        entry.value.assign(newValue.data(), newValue.size());
        entry.attr = attr;
        entry.textNode = pugi::xml_node{};
    }

    void StagedChanges::SetText(const String& key, pugi::xml_node textNode, StringView newValue) {
        auto& entry = _changes[key];
        entry.value.assign(newValue.data(), newValue.size());
        entry.attr = pugi::xml_attribute{};
        entry.textNode = textNode;
    }

    const String* StagedChanges::Get(const String& key) const {
        const auto it = _changes.find(key);
        return it != _changes.end() ? &it->second.value : nullptr;
    }

    bool StagedChanges::Has(const String& key) const {
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

    void StagedChanges::ForEach(eastl::function<void(const String&, const String&)> fn) const {
        for (const auto& [key, entry] : _changes) {
            fn(key, entry.value);
        }
    }

}  // namespace BECore
