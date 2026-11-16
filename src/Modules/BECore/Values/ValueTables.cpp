#include "ValueTables.h"

namespace BECore {

    void ValueTableSet::Add(StringView name, Vector<double> entries) {
        _tables[String(name)] = std::move(entries);
    }

    Optional<Span<const double>> ValueTableSet::Find(StringView name) const {
        const auto found = _tables.find(String(name));
        if (found == _tables.end()) {
            return std::nullopt;
        }
        return Span<const double>(found->second.data(), found->second.size());
    }

    void ValueTableSet::Names(Vector<String>& out) const {
        for (const auto& table : _tables) {
            out.push_back(table.first);
        }
    }

}  // namespace BECore
