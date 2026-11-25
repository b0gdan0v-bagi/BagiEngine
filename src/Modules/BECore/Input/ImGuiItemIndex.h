#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

struct ImGuiContext;

namespace BECore {

    /// One item or window of an ImGui frame as the item hooks of ImGui report it
    struct ImGuiItemRecord {
        uint32_t id = 0;
        /// The text the widget was given, `##` and `###` suffixes included; empty for an item ImGui reports without one
        String label;
        /// The window that was current when the item was added; a window record names itself
        String window;
        /// The rect in ImGui screen units (not output pixels)
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        /// The rect overlaps the clip rect of its window (a window: it is not hidden)
        bool visible = false;
        bool hoveredRect = false;

        [[nodiscard]] float CenterX() const {
            return (minX + maxX) * 0.5f;
        }

        [[nodiscard]] float CenterY() const {
            return (minY + maxY) * 0.5f;
        }

        /// Whether `text` (not empty) is the whole label or its part before the first `##`
        [[nodiscard]] bool MatchesLabel(StringView text) const;

        /// Whether `text` (not empty) is the whole name of the window of this record or its part before the first `##`
        [[nodiscard]] bool MatchesWindow(StringView text) const;
    };

    /**
     * @brief The items and windows one ImGui frame submitted, with their rects and labels, for the control API.
     *
     * ImGui keeps no list of its items. This one is filled by the item hooks of ImGui's test engine (`IMGUI_ENABLE_TEST_ENGINE` is
     * defined for the `imgui` target only; the hook functions are in ImGuiItemIndex.cpp, so every executable that links BECore has
     * them). While no index is armed `ImGuiContext::TestEngineHookItems` is false and ImGui pays one branch per item. An item exists
     * only in a frame that drew it: a closed menu or a collapsed tree has none.
     *
     * The list is cleared when the next frame starts (`NewFramePre` hook), not at `Arm`, so the list read after a full frame is
     * that frame's whole list, and one read between frames is the frame that ended. A frame that was already running when
     * the index was armed holds only the items submitted after it (compare `Frame()` with the frame you armed in).
     *
     * One index serves one context at a time. Records keep their strings between frames, so a frame that submits no more items
     * than an earlier one allocates nothing.
     */
    class ImGuiItemIndex {
    public:
        /// A frame with more items than this records the first of them only (`Truncated()`)
        static constexpr size_t kMaxRecords = 1U << 16;
        /// `Frame()` before any frame was recorded
        static constexpr int kNoFrame = -1;

        ImGuiItemIndex() = default;
        ImGuiItemIndex(const ImGuiItemIndex&) = delete;
        ImGuiItemIndex& operator=(const ImGuiItemIndex&) = delete;
        ~ImGuiItemIndex();

        /// Starts recording the items of `context`; an index armed for another context leaves it first
        void Arm(ImGuiContext& context);

        /// Stops recording and drops the data. Safe after the context was destroyed
        void Disarm();

        [[nodiscard]] bool IsArmed() const {
            return _context != nullptr;
        }

        /// The ImGui frame number of the list, `kNoFrame` until the first frame started after `Arm`
        [[nodiscard]] int Frame() const {
            return _frame;
        }

        /// True when a frame had more items or windows than `kMaxRecords`
        [[nodiscard]] bool Truncated() const {
            return _truncated;
        }

        /// The windows in the order of their `Begin`
        [[nodiscard]] Span<const ImGuiItemRecord> Windows() const {
            return {_windows.data(), _windowCount};
        }

        /// The items (windows excluded) in the order they were submitted
        [[nodiscard]] Span<const ImGuiItemRecord> Items() const {
            return {_items.data(), _itemCount};
        }

        /// Appends the windows and items whose label matches (`ImGuiItemRecord::MatchesLabel`) to `out`, windows first
        void FindByLabel(StringView label, Vector<const ImGuiItemRecord*>& out) const;

        /// @return the first window or item with the id, or null
        [[nodiscard]] const ImGuiItemRecord* FindById(uint32_t id) const;

        // Called by the hook functions and the context hooks of ImGuiItemIndex.cpp
        void RecordAdd(ImGuiContext& context, uint32_t id, float minX, float minY, float maxX, float maxY);
        void RecordInfo(ImGuiContext& context, uint32_t id, const char* label, uint32_t statusFlags);
        void BeginFrame(int frame);
        void OnContextShutdown();

    private:
        ImGuiItemRecord* NextRecord(Vector<ImGuiItemRecord>& records, size_t& count);

        ImGuiContext* _context = nullptr;
        uint32_t _newFrameHookId = 0;
        uint32_t _shutdownHookId = 0;
        int _frame = kNoFrame;
        bool _truncated = false;
        /// The records [0, count) are live; the rest keep their strings for the next frame
        Vector<ImGuiItemRecord> _windows;
        Vector<ImGuiItemRecord> _items;
        size_t _windowCount = 0;
        size_t _itemCount = 0;
    };

}  // namespace BECore
