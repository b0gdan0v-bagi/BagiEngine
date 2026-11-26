#pragma once

#include <BECore/Control/ControlClient.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <BECore/Utils/Singleton.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    class ControlTouches;

    /**
     * @brief What the editor shows of the nodes the clients of the control API touched: a flash that fades within kTouchFlashSeconds, and the mark of who touched
     * a field last, which stays until it is dismissed. No ImGui.
     *
     * Update() reads the record (ControlTouches) from the first report it has not seen. A node is named by its runtime id; the component and field names may be
     * client text and are only compared. Both tables are bounded: a full one gives away the entry reported longest ago. A Update() with nothing new and every
     * question allocate nothing. Main thread only.
     */
    class TouchHighlights : public Singleton<TouchHighlights> {
    public:
        static constexpr size_t kMaxFlashes = 64;
        static constexpr size_t kMaxMarks = 256;

        TouchHighlights() : _flashes(kMaxFlashes), _marks(kMaxMarks) {}

        /// Starts reading @p touches, which must live until Reset(). What it holds already is old news
        void Connect(const ControlTouches& touches);

        /// Ages the flashes by @p seconds, then takes in the reports recorded since the last call
        void Update(float seconds);

        /// Forgets the record, every flash and every mark
        void Reset();

        [[nodiscard]] bool HasFlashes() const {
            return _flashCount > 0;
        }

        /// 0 to 1, how strong the freshest flash of anything of the node is
        [[nodiscard]] float NodeAlpha(uint64_t nodeId) const;
        /// The same of the reports that named @p component, whatever field they named
        [[nodiscard]] float ComponentAlpha(uint64_t nodeId, StringView component) const;
        /// The same of the reports that named exactly this field
        [[nodiscard]] float FieldAlpha(uint64_t nodeId, StringView component, StringView field) const;

        /// The client that touched the field last; null for a field with no mark. A report that names no field leaves no mark
        [[nodiscard]] const ControlClient* FindMark(uint64_t nodeId, StringView component, StringView field) const;

        /// Calls @p visit for every marked field of the component, in no order; @p visit changes no mark
        void ForEachMark(uint64_t nodeId, StringView component, FunctionRef<void(StringView field, const ControlClient& client)> visit) const;

        /// Removes the mark of the field, if it has one
        void Dismiss(uint64_t nodeId, StringView component, StringView field);

        /// Removes the marks of every field of the component
        void DismissComponent(uint64_t nodeId, StringView component);

    private:
        /// What both tables are keyed by, and the report that put an entry there last
        struct Entry {
            uint64_t sequence = 0;
            uint64_t nodeId = 0;
            String component;
            String field;

            [[nodiscard]] bool Is(uint64_t node, StringView componentName, StringView fieldName) const {
                return nodeId == node && StringView(component) == componentName && StringView(field) == fieldName;
            }
        };

        struct Flash : Entry {
            float age = 0.0f;  ///< seconds since its report was taken in
        };

        struct Mark : Entry {
            IntrusivePtr<ControlClient> client;
        };

        /// The strength of the freshest flash that @p matches
        template <typename TMatch>
        [[nodiscard]] float StrongestAlpha(const TMatch& matches) const;

        /// Removes the live mark @p index
        void RemoveMark(size_t index);

        void ReadTouches();

        const ControlTouches* _touches = nullptr;
        uint64_t _nextSequence = 0;  ///< of the first report not read yet
        Vector<Flash> _flashes;      ///< made once; the first _flashCount are live, in no order
        Vector<Mark> _marks;         ///< as _flashes
        size_t _flashCount = 0;
        size_t _markCount = 0;
    };

    /// One component of one node as TouchHighlights knows it: what an inspector asks about and dismisses. Good while the three it names are
    struct ComponentTouches {
        TouchHighlights& highlights;
        uint64_t nodeId = 0;
        StringView component;  ///< the component's type name
    };

}  // namespace BECore
