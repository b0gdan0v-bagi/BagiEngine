#include "SelectionHistory.h"

namespace BECore {

    void SelectionHistory::Record(uint64_t selected) {
        if (selected == 0 || (_passed > 0 && _entries[_passed - 1] == selected)) {
            return;
        }
        _count = _passed;
        if (_count == kCapacity) {
            Erase(0);
        }
        _entries[_count] = selected;
        ++_count;
        _passed = _count;
    }

    uint64_t SelectionHistory::Back(uint64_t selected, FunctionRef<bool(uint64_t)> isAlive) {
        Record(selected);
        // With a selection the history stands on it, so the search starts one entry earlier
        const size_t kept = selected != 0 ? 1 : 0;
        while (_passed > kept) {
            const size_t index = _passed - kept - 1;
            const uint64_t id = _entries[index];
            // A dropped entry can leave two equal ones side by side: the one that would select the selection again goes too
            if (id != selected && isAlive(id)) {
                _passed = index + 1;
                return id;
            }
            Erase(index);
        }
        return 0;
    }

    uint64_t SelectionHistory::Forward(uint64_t selected, FunctionRef<bool(uint64_t)> isAlive) {
        Record(selected);
        while (_passed < _count) {
            const uint64_t id = _entries[_passed];
            if (id != selected && isAlive(id)) {
                ++_passed;
                return id;
            }
            Erase(_passed);
        }
        return 0;
    }

    void SelectionHistory::Clear() {
        _count = 0;
        _passed = 0;
    }

    void SelectionHistory::Erase(size_t index) {
        for (size_t i = index + 1; i < _count; ++i) {
            _entries[i - 1] = _entries[i];
        }
        --_count;
        if (index < _passed) {
            --_passed;
        }
    }

}  // namespace BECore
