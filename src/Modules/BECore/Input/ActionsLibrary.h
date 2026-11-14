#pragma once

#include <EASTL/unordered_set.h>

namespace BECore {

    class ActionsLibrary : public Singleton<ActionsLibrary> {
    public:
        const Vector<PoolString>& GetAll() const {
            return _entries;
        }

        bool Contains(PoolString name) const {
            return _index.count(name) > 0;
        }

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
        Vector<PoolString> _entries;
        HashSet<PoolString> _index;
    };

}  // namespace BECore
