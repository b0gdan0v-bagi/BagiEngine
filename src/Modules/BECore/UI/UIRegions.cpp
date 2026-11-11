#include "UIRegions.h"

namespace BECore {

    namespace {

        bool SameRect(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

    }  // namespace

    void UIRegionSet::Set(PoolString name, const Rect& rect) {
        if (name.Empty()) {
            return;
        }
        for (Entry& entry : _entries) {
            if (entry.name == name) {
                entry.rect = rect;
                return;
            }
        }
        _entries.push_back({name, rect});
    }

    bool UIRegionSet::Remove(PoolString name) {
        for (auto it = _entries.begin(); it != _entries.end(); ++it) {
            if (it->name == name) {
                _entries.erase(it);
                return true;
            }
        }
        return false;
    }

    void UIRegionSet::Clear() {
        _entries.clear();
    }

    const Rect* UIRegionSet::Find(PoolString name) const {
        if (name.Empty()) {
            return nullptr;
        }
        for (const Entry& entry : _entries) {
            if (entry.name == name) {
                return &entry.rect;
            }
        }
        return nullptr;
    }

    bool UIRegionSet::operator==(const UIRegionSet& other) const {
        if (_entries.size() != other._entries.size()) {
            return false;
        }
        // Names are unique within a set, so equal sizes and every entry found with the same rect make the sets equal
        for (const Entry& entry : _entries) {
            const Rect* found = other.Find(entry.name);
            if (found == nullptr || !SameRect(*found, entry.rect)) {
                return false;
            }
        }
        return true;
    }

}  // namespace BECore
