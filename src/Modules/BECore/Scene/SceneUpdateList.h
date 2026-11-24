#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    class IComponent;

    /// The two update lists of a scene: SceneEvents::SceneUpdateEvent (once per frame) and SceneFixedUpdateEvent (once per fixed step)
    CORE_ENUM(SceneUpdateKind, uint8_t, Frame, Fixed)

    /// Calls the handler of one registration: casts @p component to its type and @p event to the event the handler takes
    using SceneUpdateInvoker = void (*)(IComponent* component, const void* event);

    /**
     * @brief The components of a scene that update: (component, invoker) pairs called in a fixed order, without a walk of the node tree.
     *
     * Never dereferences a component: it only compares and passes the pointers on, so the owner decides when a component leaves (Remove)
     * and a list that outlives its components is not read. The vector is kept between frames, so a pass allocates nothing.
     *
     * - Run() calls the entries from the last added to the first (what EventChannel::Publish does), over the count at its start, so an
     *   entry added by a handler is first called in the next run. The entry is copied before the call, as the handler may grow the vector.
     * - Remove() inside a run leaves a tombstone (a null component): the entry is skipped if it has not run yet, and the outermost
     *   Run() compacts when it ends. The order of the others never changes, unlike a channel, which moves its last listener into the
     *   freed slot. A nested Run() (a handler that updates the scene) only counts depth.
     * - Add() does not look for a duplicate: the same pair added twice is called twice.
     *
     * One thread only.
     */
    class SceneUpdateList {
    public:
        struct Entry {
            IComponent* component = nullptr;  ///< null: removed while a run was going
            SceneUpdateInvoker invoke = nullptr;
        };

        void Add(IComponent& component, SceneUpdateInvoker invoke) {
            _entries.push_back({&component, invoke});
        }

        /// Drops every entry of @p component. O(entries) over the whole list: for edits of the scene, never for a frame.
        void Remove(const IComponent& component) {
            for (Entry& entry : _entries) {
                if (entry.component == &component) {
                    entry.component = nullptr;
                    ++_tombstones;
                }
            }
            if (_runDepth == 0 && _tombstones != 0) {
                Compact();
            }
        }

        void Run(const void* event) {
            ++_runDepth;
            for (size_t pos = _entries.size(); pos != 0; --pos) {
                const Entry entry = _entries[pos - 1];
                if (entry.component != nullptr) {
                    entry.invoke(entry.component, event);
                }
            }
            if (--_runDepth == 0 && _tombstones != 0) {
                Compact();
            }
        }

        /// The entries that will be called, a tombstone not counted
        size_t GetCount() const {
            return _entries.size() - _tombstones;
        }

        bool IsEmpty() const {
            return _entries.empty();
        }

    private:
        void Compact() {
            size_t kept = 0;
            for (const Entry& entry : _entries) {
                if (entry.component != nullptr) {
                    _entries[kept++] = entry;
                }
            }
            _entries.resize(kept);
            _tombstones = 0;
        }

        Vector<Entry> _entries;
        size_t _tombstones = 0;
        uint32_t _runDepth = 0;
    };

}  // namespace BECore
