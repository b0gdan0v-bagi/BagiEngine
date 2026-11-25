#include "ImGuiInputControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Input/ImGuiInputAnswers.h>
#include <BECore/Input/ImGuiInputEvents.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        constexpr StringView kSpaceOutput = "output";
        constexpr StringView kSpaceImGui = "imgui";
        constexpr StringView kHexPrefix = "0x";
        constexpr int kHexBase = 16;
        constexpr size_t kMaxIdDigits = 8;

        /// The words of `kind` in an answer as interned constants
        struct KindWords {
            static constexpr auto window = "window"_intern;
            static constexpr auto item = "item"_intern;
        };

        struct KindName {
            StringView name;
            ImGuiInputKind kind;
        };
        constexpr KindName kKindNames[] = {{"move", ImGuiInputKind::Move},   {"click", ImGuiInputKind::Click}, {"down", ImGuiInputKind::Down}, {"up", ImGuiInputKind::Up},
                                           {"wheel", ImGuiInputKind::Wheel}, {"key", ImGuiInputKind::Key},     {"text", ImGuiInputKind::Text}};

        using ParseError = std::unexpected<String>;

        ParseError Refuse(StringView reason) {
            return ParseError(String(reason));
        }

        /// A number member: absent is nullopt, a wrong type, a value that is not finite or one beyond what ImGui takes is an error
        std::expected<Optional<float>, String> ReadFloat(JsonView object, StringView key) {
            if (!FindParam(object, key)) {
                return Optional<float>{};
            }
            const auto number = GetParamNumber(object, key);
            if (!number || std::fabs(*number) > static_cast<double>(kMaxImGuiInputMagnitude)) {
                return Refuse(Format("'{}' has to be a finite number in range", key));
            }
            return Optional<float>(static_cast<float>(*number));
        }

        /// A string member that is one of @p words: absent is nullopt, anything else an error
        std::expected<Optional<size_t>, String> ReadWord(JsonView object, StringView key, Span<const StringView> words) {
            if (!FindParam(object, key)) {
                return Optional<size_t>{};
            }
            const auto text = GetParamString(object, key);
            if (text) {
                for (size_t i = 0; i < words.size(); ++i) {
                    if (*text == words[i]) {
                        return Optional<size_t>(i);
                    }
                }
            }
            String list;
            for (const StringView word : words) {
                list += list.empty() ? Format("\"{}\"", word) : Format(", \"{}\"", word);
            }
            return Refuse(Format("'{}' has to be one of {}", key, list));
        }

        constexpr StringView kButtonWords[] = {"left", "right", "middle"};
        constexpr StringView kActionWords[] = {"press", "down", "up"};
        constexpr StringView kModWords[] = {"ctrl", "shift", "alt", "super"};

        std::expected<void, String> ReadMods(JsonView object, ImGuiInputMods& mods) {
            const Optional<JsonView> list = FindParam(object, "mods");
            if (!list) {
                return {};
            }
            if (!list->IsArray()) {
                return Refuse("'mods' has to be an array of \"ctrl\", \"shift\", \"alt\" or \"super\"");
            }
            for (const JsonView item : list->Items()) {
                String name;
                if (!item.IsString() || !item.ReadText(name)) {
                    return Refuse("'mods' has to be an array of \"ctrl\", \"shift\", \"alt\" or \"super\"");
                }
                const StringView word(name);
                if (word == kModWords[0]) {
                    mods.ctrl = true;
                } else if (word == kModWords[1]) {
                    mods.shift = true;
                } else if (word == kModWords[2]) {
                    mods.alt = true;
                } else if (word == kModWords[3]) {
                    mods.super = true;
                } else {
                    return Refuse(Format("unknown modifier \"{}\"", name));
                }
            }
            return {};
        }

        std::expected<void, String> ReadPosition(JsonView object, ImGuiInputEvent& event) {
            const auto x = ReadFloat(object, "x");
            const auto y = ReadFloat(object, "y");
            if (!x) {
                return std::unexpected(x.error());
            }
            if (!y) {
                return std::unexpected(y.error());
            }
            if (x->has_value() != y->has_value()) {
                return Refuse("'x' and 'y' go together");
            }
            if (x->has_value()) {
                event.hasPosition = true;
                event.x = **x;
                event.y = **y;
            }
            return {};
        }

        /// Reads the fields the kind of @p event uses; the limits the unit checks itself (a click count, a key name, a text) are checked by `ValidateImGuiEvents`
        std::expected<void, String> ParseEvent(JsonView object, ImGuiInputEvent& event) {
            const auto type = GetParamString(object, "type");
            if (!type) {
                return Refuse("'type' has to be a string");
            }
            bool known = false;
            for (const KindName& entry : kKindNames) {
                if (*type == entry.name) {
                    event.kind = entry.kind;
                    known = true;
                }
            }
            if (!known) {
                return Refuse(Format("unknown type \"{}\"", *type));
            }

            if (const auto position = ReadPosition(object, event); !position) {
                return position;
            }
            const auto button = ReadWord(object, "button", kButtonWords);
            if (!button) {
                return std::unexpected(button.error());
            }
            if (button->has_value()) {
                event.button = static_cast<ImGuiInputButton>(**button);
            }
            const auto action = ReadWord(object, "action", kActionWords);
            if (!action) {
                return std::unexpected(action.error());
            }
            if (action->has_value()) {
                event.action = static_cast<ImGuiKeyAction>(**action);
            }
            if (FindParam(object, "count")) {
                const auto count = GetParamInt(object, "count");
                if (!count || *count < 1 || *count > kMaxImGuiClickCount) {
                    return Refuse(Format("'count' has to be an integer from 1 to {}", kMaxImGuiClickCount));
                }
                event.count = static_cast<int>(*count);
            }
            const auto dx = ReadFloat(object, "dx");
            const auto dy = ReadFloat(object, "dy");
            if (!dx) {
                return std::unexpected(dx.error());
            }
            if (!dy) {
                return std::unexpected(dy.error());
            }
            event.dx = dx->value_or(0.0f);
            event.dy = dy->value_or(0.0f);
            if (const auto mods = ReadMods(object, event.mods); !mods) {
                return mods;
            }
            if (FindParam(object, "key")) {
                const auto key = GetParamString(object, "key");
                if (!key) {
                    return Refuse("'key' has to be a string");
                }
                event.key = *key;
            }
            if (FindParam(object, "text")) {
                const auto text = GetParamString(object, "text");
                if (!text) {
                    return Refuse("'text' has to be a string");
                }
                event.text = *text;
            }
            return {};
        }

        /// The `events` of the request; the error names the index of the event that cannot be read
        std::expected<Vector<ImGuiInputEvent>, String> ParseEvents(JsonView params) {
            const Optional<JsonView> list = FindParam(params, "events");
            if (!list || !list->IsArray()) {
                return Refuse("'events' has to be an array of events");
            }
            if (list->Size() > kMaxImGuiInputEvents) {
                return Refuse(Format("too many events ({}, at most {})", list->Size(), kMaxImGuiInputEvents));
            }
            Vector<ImGuiInputEvent> events;
            events.reserve(list->Size());
            size_t index = 0;
            for (const JsonView item : list->Items()) {
                ImGuiInputEvent event;
                if (!item.IsObject()) {
                    return Refuse(Format("events[{}]: an event has to be an object", index));
                }
                if (const auto parsed = ParseEvent(item, event); !parsed) {
                    return Refuse(Format("events[{}]: {}", index, parsed.error()));
                }
                events.push_back(std::move(event));
                ++index;
            }
            return events;
        }

        std::expected<ImGuiInputSpace, String> ReadSpace(JsonView params) {
            constexpr StringView words[] = {kSpaceOutput, kSpaceImGui};
            const auto word = ReadWord(params, "space", words);
            if (!word) {
                return std::unexpected(word.error());
            }
            return word->value_or(0) == 0 ? ImGuiInputSpace::Output : ImGuiInputSpace::Window;
        }

        /// ImGui's frame is not ended when this runs between frames, so the flags are those of the last frame that ran
        template <typename TAnswer>
        void FillWants(const ImGuiIO& io, TAnswer& answer) {
            answer.wantCaptureKeyboard = io.WantCaptureKeyboard;
            answer.wantCaptureMouse = io.WantCaptureMouse;
            answer.wantTextInput = io.WantTextInput;
        }

        struct InputWait {
            int64_t events = 0;
            int64_t frames = 0;
            bool drained = false;
        };

        /// Runs frames of zero seconds until ImGui has taken the whole queue and one more frame has reacted to it: ImGui applies one change of a button,
        /// a key or the wheel per frame, so the state the client wants to see is not there before
        void AwaitInput(ControlCall& call, InputWait wait) {
            call.DeferFrames(1, 0.0, [wait](ControlCall& done) mutable {
                ++wait.frames;
                if (ImGui::GetCurrentContext() == nullptr) {
                    done.Fail(ImGuiControlErrors::kNoImGui, "the ImGui context is gone");
                    return;
                }
                const ImGuiIO& io = ImGui::GetIO();
                if (ImGuiInputQueueEmpty(io)) {
                    if (wait.drained) {
                        ImGuiInputAnswer answer;
                        answer.events = wait.events;
                        answer.frames = wait.frames;
                        FillWants(io, answer);
                        done.ReplyObject(answer);
                        return;
                    }
                    wait.drained = true;
                } else if (wait.frames >= kMaxImGuiWaitFrames) {
                    done.Fail(ImGuiControlErrors::kTimeout, Format("ImGui did not take the input in {} frames", kMaxImGuiWaitFrames));
                    return;
                }
                AwaitInput(done, wait);
            });
        }

        void HandleInput(ControlCall& call) {
            if (ImGui::GetCurrentContext() == nullptr) {
                call.Fail(ImGuiControlErrors::kNoImGui, "there is no ImGui context: the game runs in Game mode");
                return;
            }
            const JsonView params = call.Params();
            const auto space = ReadSpace(params);
            if (!space) {
                call.Fail(ControlErrors::kInvalidParams, space.error());
                return;
            }
            const auto events = ParseEvents(params);
            if (!events) {
                call.Fail(ControlErrors::kInvalidParams, events.error());
                return;
            }
            if (const auto queued = QueueImGuiEvents(ImGui::GetIO(), *events, *space); !queued) {
                call.Fail(ControlErrors::kInvalidParams, queued.error());
                return;
            }
            InputWait wait;
            wait.events = static_cast<int64_t>(events->size());
            AwaitInput(call, wait);
        }

        /// What `imgui_find` was asked for
        CORE_ENUM(FindKind, uint8_t, Any, Window, Item)

        struct FindFilter {
            String label;
            String labelContains;
            String window;
            Optional<uint32_t> id;
            FindKind kind = FindKind::Any;
            bool visibleOnly = false;
            size_t limit = static_cast<size_t>(kDefaultImGuiFindLimit);

            [[nodiscard]] bool HasSelector() const {
                return !label.empty() || !labelContains.empty() || !window.empty() || id || kind != FindKind::Any;
            }
        };

        /// A text member that, when present, is a string that is not empty
        std::expected<String, String> ReadName(JsonView params, StringView key) {
            if (!FindParam(params, key)) {
                return String();
            }
            auto text = GetParamString(params, key);
            if (!text || text->empty()) {
                return Refuse(Format("'{}' has to be a string that is not empty", key));
            }
            return std::move(*text);
        }

        /// An id as ImGui prints it: hex digits with an optional `0x`
        Optional<uint32_t> ParseId(StringView text) {
            if (text.starts_with(kHexPrefix) || text.starts_with("0X")) {
                text.remove_prefix(kHexPrefix.size());
            }
            uint32_t value = 0;
            if (text.empty() || text.size() > kMaxIdDigits) {
                return {};
            }
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, kHexBase);
            if (error != std::errc{} || end != text.data() + text.size()) {
                return {};
            }
            return value;
        }

        std::expected<FindFilter, String> ParseFilter(JsonView params) {
            FindFilter filter;
            auto label = ReadName(params, "label");
            auto contains = ReadName(params, "labelContains");
            auto window = ReadName(params, "window");
            for (const auto* name : {&label, &contains, &window}) {
                if (!*name) {
                    return std::unexpected(name->error());
                }
            }
            filter.label = std::move(*label);
            filter.labelContains = std::move(*contains);
            filter.window = std::move(*window);
            const auto idText = ReadName(params, "id");
            if (!idText) {
                return std::unexpected(idText.error());
            }
            if (!idText->empty()) {
                filter.id = ParseId(*idText);
                if (!filter.id) {
                    return Refuse("'id' has to be hex digits as ImGui prints an id, like 0x1A2B3C4D");
                }
            }
            constexpr StringView kindWords[] = {"window", "item"};
            const auto kind = ReadWord(params, "kind", kindWords);
            if (!kind) {
                return std::unexpected(kind.error());
            }
            if (kind->has_value()) {
                filter.kind = **kind == 0 ? FindKind::Window : FindKind::Item;
            }
            if (FindParam(params, "visibleOnly")) {
                const auto visible = GetParamBool(params, "visibleOnly");
                if (!visible) {
                    return Refuse("'visibleOnly' has to be true or false");
                }
                filter.visibleOnly = *visible;
            }
            if (FindParam(params, "limit")) {
                const auto limit = GetParamInt(params, "limit");
                if (!limit || *limit < 1 || *limit > kMaxImGuiFindLimit) {
                    return Refuse(Format("'limit' has to be an integer from 1 to {}", kMaxImGuiFindLimit));
                }
                filter.limit = static_cast<size_t>(*limit);
            }
            if (!filter.HasSelector()) {
                // The cheap overview: what is on the screen, not every item of it
                filter.kind = FindKind::Window;
            }
            return filter;
        }

        bool Matches(const ImGuiItemRecord& record, const FindFilter& filter) {
            if (filter.visibleOnly && !record.visible) {
                return false;
            }
            if (!filter.label.empty() && !record.MatchesLabel(filter.label)) {
                return false;
            }
            if (!filter.labelContains.empty() && record.label.find(filter.labelContains) == String::npos) {
                return false;
            }
            if (!filter.window.empty() && !record.MatchesWindow(filter.window)) {
                return false;
            }
            return !filter.id || record.id == *filter.id;
        }

        ArenaString ArenaText(StringView text) {
            return ArenaString(text.data(), text.size());
        }

        /// Output pixels per ImGui unit; a backend that never set the scale leaves 0
        double OutputScale(float framebufferScale) {
            return std::isfinite(framebufferScale) && framebufferScale > 0.0f ? static_cast<double>(framebufferScale) : 1.0;
        }

        Optional<ArenaString> WindowName(const ImGuiWindow* window) {
            return window != nullptr ? Optional<ArenaString>(ArenaText(window->Name)) : Optional<ArenaString>();
        }

        /// Appends the records that match, up to the limit; @return false when one more matched than fits
        bool AppendMatches(Span<const ImGuiItemRecord> records, bool windows, const FindFilter& filter, const ImVec2& scale, ArenaVector<ImGuiMatchAnswer>& out) {
            for (const ImGuiItemRecord& record : records) {
                if (!Matches(record, filter)) {
                    continue;
                }
                if (out.size() >= filter.limit) {
                    return false;
                }
                ImGuiMatchAnswer match;
                match.x = static_cast<double>(record.minX) * scale.x;
                match.y = static_cast<double>(record.minY) * scale.y;
                match.width = static_cast<double>(record.maxX - record.minX) * scale.x;
                match.height = static_cast<double>(record.maxY - record.minY) * scale.y;
                match.id = ArenaText(Format("0x{:08X}", record.id));
                if (windows) {
                    match.kind = KindWords::window;
                } else {
                    match.kind = KindWords::item;
                }
                match.label = TextView(record.label);
                match.window = TextView(record.window);
                match.visible = record.visible;
                match.hovered = record.hoveredRect;
                out.push_back(std::move(match));
            }
            return true;
        }

        void ReplyFind(ControlCall& done, const ImGuiItemIndex& index, const FindFilter& filter) {
            const ImGuiContext& context = *ImGui::GetCurrentContext();
            const ImGuiIO& io = ImGui::GetIO();
            const ImVec2 scale(static_cast<float>(OutputScale(io.DisplayFramebufferScale.x)), static_cast<float>(OutputScale(io.DisplayFramebufferScale.y)));

            ImGuiFindAnswer answer;
            answer.frame = index.Frame();
            answer.display.scaleX = static_cast<double>(scale.x);
            answer.display.scaleY = static_cast<double>(scale.y);
            answer.display.width = static_cast<double>(io.DisplaySize.x) * answer.display.scaleX;
            answer.display.height = static_cast<double>(io.DisplaySize.y) * answer.display.scaleY;
            FillWants(io, answer);
            answer.hoveredWindow = WindowName(context.HoveredWindow);
            answer.focusedWindow = WindowName(context.NavWindow);

            bool complete = true;
            if (filter.kind != FindKind::Item) {
                complete = AppendMatches(index.Windows(), true, filter, scale, answer.matches);
            }
            if (complete && filter.kind != FindKind::Window) {
                complete = AppendMatches(index.Items(), false, filter, scale, answer.matches);
            }
            if (!complete) {
                answer.truncated = true;
            }
            done.ReplyObject(answer);
        }

        /// The index lives as long as a handler or a pending call holds it, so a replaced handler never leaves ImGui armed
        struct FindState : RefCountedNonAtomic {
            ImGuiItemIndex index;
        };

        /// Waits for a whole frame recorded by the armed index: a call that runs inside a frame arms an index that has seen only the rest of that frame, which
        /// `ImGuiItemIndex::Frame()` calls no frame at all
        void AwaitFind(ControlCall& call, IntrusivePtr<FindState> state, FindFilter filter, uint32_t waited) {
            call.DeferFrames(1, 0.0, [state = std::move(state), filter = std::move(filter), waited](ControlCall& done) mutable {
                ++waited;
                ImGuiContext* context = ImGui::GetCurrentContext();
                if (context == nullptr) {
                    state->index.Disarm();
                    done.Fail(ImGuiControlErrors::kNoImGui, "the ImGui context is gone");
                    return;
                }
                state->index.Arm(*context);
                if (state->index.Frame() != ImGuiItemIndex::kNoFrame) {
                    ReplyFind(done, state->index, filter);
                    state->index.Disarm();
                    return;
                }
                if (waited >= kMaxImGuiWaitFrames) {
                    state->index.Disarm();
                    done.Fail(ImGuiControlErrors::kTimeout, Format("ImGui did not run a frame in {} frames", kMaxImGuiWaitFrames));
                    return;
                }
                AwaitFind(done, std::move(state), std::move(filter), waited);
            });
        }

        void HandleFind(ControlCall& call, const IntrusivePtr<FindState>& state) {
            ImGuiContext* context = ImGui::GetCurrentContext();
            if (context == nullptr) {
                call.Fail(ImGuiControlErrors::kNoImGui, "there is no ImGui context: the game runs in Game mode");
                return;
            }
            auto filter = ParseFilter(call.Params());
            if (!filter) {
                call.Fail(ControlErrors::kInvalidParams, filter.error());
                return;
            }
            state->index.Arm(*context);
            AwaitFind(call, state, std::move(*filter), 0);
        }

    }  // namespace

    void RegisterImGuiInputControl(ControlService& service) {
        auto state = New<FindState>();
        service.RegisterHandler("imgui_input", HandleInput);
        service.RegisterHandler("imgui_find", [state](ControlCall& call) { HandleFind(call, state); });

        service.DescribeMethod("imgui_input",
                               {"Sends mouse, key and text events to the editor's ImGui and answers when ImGui has taken them and a frame has reacted, with what ImGui wants to capture",
                                {{"events", "array", true,
                                  "Events in order: {type: \"move\", x, y}, {type: \"click\", x?, y?, button?, count?, mods?}, {type: \"down\" or \"up\", button?}, {type: \"wheel\", dx?, dy?}, "
                                  "{type: \"key\", key, action?, mods?} (ImGui's key name: Enter, Escape, A, F6, LeftArrow), {type: \"text\", text}; button is \"left\" (default), \"right\" or "
                                  "\"middle\"; mods are \"ctrl\", \"shift\", \"alt\", \"super\"; action is \"press\" (default), \"down\" or \"up\""},
                                 {"space", "string", false, "\"output\" (default): positions in output pixels, as screenshots and imgui_find give them; \"imgui\": ImGui's own window units"}}});
        service.DescribeMethod("imgui_find", {"Lists the windows and items the editor's ImGui drew in a frame, with their rects in output pixels; with no filter the windows only",
                                              {{"label", "string", false, "The whole label, or its part before ##"},
                                               {"labelContains", "string", false, "A part of the label"},
                                               {"id", "string", false, "ImGui's id as hex text, like 0x1A2B3C4D"},
                                               {"window", "string", false, "Only what belongs to this window (its name, or its part before ##)"},
                                               {"kind", "string", false, "\"window\" or \"item\""},
                                               {"visibleOnly", "boolean", false, "true: leave out what is clipped or scrolled out of its window"},
                                               {"limit", "integer", false, "At most this many matches, default 50, at most 500"}}});
    }

}  // namespace BECore
