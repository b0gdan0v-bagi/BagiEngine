// The item hooks are the ones declared under IMGUI_ENABLE_TEST_ENGINE; the define only adds enumerators to ImGuiItemStatusFlags, the
// layout of the context does not depend on it
#define IMGUI_ENABLE_TEST_ENGINE

#include "ImGuiItemIndex.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        constexpr StringView kHiddenLabelMark = "##";

        ImGuiItemIndex* IndexOf(ImGuiContext* context) {
            return context != nullptr ? static_cast<ImGuiItemIndex*>(context->TestEngine) : nullptr;
        }

        /// The first `##` of a label ends the part a person sees; `###id` begins with one too
        StringView VisiblePart(StringView label) {
            const size_t mark = label.find(kHiddenLabelMark);
            return mark == StringView::npos ? label : label.substr(0, mark);
        }

        void OnContextHook(ImGuiContext* context, ImGuiContextHook* hook) {
            auto* index = static_cast<ImGuiItemIndex*>(hook->UserData);
            if (hook->Type == ImGuiContextHookType_NewFramePre) {
                // FrameCount is advanced after this hook runs: the frame that starts is the next one
                index->BeginFrame(context->FrameCount + 1);
            } else {
                index->OnContextShutdown();
            }
        }

        /// The window a hook call belongs to: ImGui registers a window as an item whose id is the window's own
        bool IsWindowId(const ImGuiContext& context, uint32_t id) {
            return context.CurrentWindow != nullptr && context.CurrentWindow->ID == id;
        }

    }  // namespace

    bool ImGuiItemRecord::MatchesLabel(StringView text) const {
        return !text.empty() && (StringView{label} == text || VisiblePart(label) == text);
    }

    ImGuiItemIndex::~ImGuiItemIndex() {
        Disarm();
    }

    void ImGuiItemIndex::Arm(ImGuiContext& context) {
        if (_context == &context) {
            return;
        }
        Disarm();
        _context = &context;
        context.TestEngine = this;
        context.TestEngineHookItems = true;

        ImGuiContextHook hook;
        hook.Callback = &OnContextHook;
        hook.UserData = this;
        hook.Type = ImGuiContextHookType_NewFramePre;
        _newFrameHookId = ImGui::AddContextHook(&context, &hook);
        hook.Type = ImGuiContextHookType_Shutdown;
        _shutdownHookId = ImGui::AddContextHook(&context, &hook);
    }

    void ImGuiItemIndex::Disarm() {
        if (_context != nullptr) {
            ImGui::RemoveContextHook(_context, _newFrameHookId);
            ImGui::RemoveContextHook(_context, _shutdownHookId);
            _context->TestEngineHookItems = false;
            _context->TestEngine = nullptr;
            _context = nullptr;
        }
        _newFrameHookId = 0;
        _shutdownHookId = 0;
        _frame = kNoFrame;
        _truncated = false;
        _windowCount = 0;
        _itemCount = 0;
    }

    void ImGuiItemIndex::OnContextShutdown() {
        // The context frees its hooks itself
        _context = nullptr;
        Disarm();
    }

    void ImGuiItemIndex::BeginFrame(int frame) {
        _frame = frame;
        _truncated = false;
        _windowCount = 0;
        _itemCount = 0;
    }

    void ImGuiItemIndex::FindByLabel(StringView label, Vector<const ImGuiItemRecord*>& out) const {
        for (const ImGuiItemRecord& record : Windows()) {
            if (record.MatchesLabel(label)) {
                out.push_back(&record);
            }
        }
        for (const ImGuiItemRecord& record : Items()) {
            if (record.MatchesLabel(label)) {
                out.push_back(&record);
            }
        }
    }

    const ImGuiItemRecord* ImGuiItemIndex::FindById(uint32_t id) const {
        for (const ImGuiItemRecord& record : Windows()) {
            if (record.id == id) {
                return &record;
            }
        }
        for (const ImGuiItemRecord& record : Items()) {
            if (record.id == id) {
                return &record;
            }
        }
        return nullptr;
    }

    ImGuiItemRecord* ImGuiItemIndex::NextRecord(Vector<ImGuiItemRecord>& records, size_t& count) {
        if (count >= kMaxRecords) {
            _truncated = true;
            return nullptr;
        }
        if (count == records.size()) {
            records.emplace_back();
        }
        return &records[count++];
    }

    void ImGuiItemIndex::RecordAdd(ImGuiContext& context, uint32_t id, float minX, float minY, float maxX, float maxY) {
        const ImGuiWindow* window = context.CurrentWindow;
        const bool isWindow = IsWindowId(context, id);
        ImGuiItemRecord* record = isWindow ? NextRecord(_windows, _windowCount) : NextRecord(_items, _itemCount);
        if (record == nullptr) {
            return;
        }
        record->id = id;
        record->label.clear();
        if (window != nullptr) {
            record->window.assign(window->Name);
        } else {
            record->window.clear();
        }
        record->minX = minX;
        record->minY = minY;
        record->maxX = maxX;
        record->maxY = maxY;
        record->hoveredRect = false;
        if (isWindow) {
            // Begin computes `Hidden` after this hook ran, so the counters it is made of are read
            record->visible = window->HiddenFramesCanSkipItems == 0 && window->HiddenFramesCannotSkipItems == 0 && window->HiddenFramesForRenderOnly == 0;
        } else {
            // The test ImGui itself makes in ItemAdd; its Visible flag is set after the hook ran
            const ImRect rect(minX, minY, maxX, maxY);
            record->visible = window != nullptr && rect.Overlaps(window->ClipRect);
        }
    }

    void ImGuiItemIndex::RecordInfo(ImGuiContext& context, uint32_t id, const char* label, uint32_t statusFlags) {
        const bool isWindow = IsWindowId(context, id);
        Vector<ImGuiItemRecord>& records = isWindow ? _windows : _items;
        const size_t count = isWindow ? _windowCount : _itemCount;
        // The info of an item follows its add, so the match is the last record or just before it
        for (size_t i = count; i > 0; --i) {
            ImGuiItemRecord& record = records[i - 1];
            if (record.id == id) {
                if (label != nullptr) {
                    record.label.assign(label);
                }
                record.hoveredRect = (statusFlags & ImGuiItemStatusFlags_HoveredRect) != 0;
                return;
            }
        }
    }

}  // namespace BECore

// The functions ImGui calls under IMGUI_ENABLE_TEST_ENGINE. They are global and the linker takes them from whichever object defines them,
// so BECore is the one place; an executable that links BECore has them.

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* itemData) {
    BECore::ImGuiItemIndex* index = BECore::IndexOf(ctx);
    if (index == nullptr) {
        return;
    }
    // The item's own rect; `bb` is the navigation rect for most items
    const ImRect& rect = itemData != nullptr ? itemData->Rect : bb;
    index->RecordAdd(*ctx, id, rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
    BECore::ImGuiItemIndex* index = BECore::IndexOf(ctx);
    if (index != nullptr) {
        index->RecordInfo(*ctx, id, label, static_cast<uint32_t>(flags));
    }
}

void ImGuiTestEngineHook_Log(ImGuiContext* /*ctx*/, const char* /*fmt*/, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* /*ctx*/, ImGuiID /*id*/) {
    return nullptr;
}
