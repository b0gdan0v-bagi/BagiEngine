#include "StagedChanges.h"

namespace BECore {

    void StagedChanges::Set(const eastl::string& key, pugi::xml_attribute attr, eastl::string_view newValue) {
        auto& entry = _changes[key];
        entry.value.assign(newValue.data(), newValue.size());
        entry.attr = attr;
        entry.textNode = pugi::xml_node{};
    }

    void StagedChanges::SetText(const eastl::string& key, pugi::xml_node textNode, eastl::string_view newValue) {
        auto& entry = _changes[key];
        entry.value.assign(newValue.data(), newValue.size());
        entry.attr = pugi::xml_attribute{};
        entry.textNode = textNode;
    }

    const eastl::string* StagedChanges::Get(const eastl::string& key) const {
        const auto it = _changes.find(key);
        return it != _changes.end() ? &it->second.value : nullptr;
    }

    bool StagedChanges::Has(const eastl::string& key) const {
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

    void StagedChanges::ForEach(eastl::function<void(const eastl::string&, const eastl::string&)> fn) const {
        for (const auto& [key, entry] : _changes) {
            fn(key, entry.value);
        }
    }

}  // namespace BECore
