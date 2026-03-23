#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Utils/Singleton.h>
#include <EASTL/unordered_set.h>
#include <EASTL/vector.h>

namespace BECore {

    class ActionsLibrary : public Singleton<ActionsLibrary> {
    public:
        const eastl::vector<PoolString>& GetAll() const { return _entries; }

        bool Contains(PoolString name) const { return _index.count(name) > 0; }

        void Add(PoolString name) {
            if (name.Empty() || Contains(name)) {
                return;
            }
            _entries.push_back(name);
            _index.insert(name);
        }

        void Remove(PoolString name) {
            if (!Contains(name)) {
                return;
            }
            _entries.erase(eastl::remove(_entries.begin(), _entries.end(), name), _entries.end());
            _index.erase(name);
        }

    private:
        eastl::vector<PoolString> _entries;
        eastl::unordered_set<PoolString> _index;
    };

}  // namespace BECore
