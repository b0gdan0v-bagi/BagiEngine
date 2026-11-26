#include "Workspaces.h"

#include <BECore/Control/ControlParams.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <Widgets/EditorLayout/EditorPanelIds.h>
#include <Widgets/EditorLayout/EditorUserFile.h>
#include <Widgets/EditorLayout/LayoutStore.h>
#include <Widgets/ImGuiLayoutText.h>

namespace BECore {

    namespace {

        constexpr int kFileVersion = 1;

        constexpr StringView kVersionKey = "version";
        constexpr StringView kActiveKey = "active";
        constexpr StringView kWorkspacesKey = "workspaces";
        constexpr StringView kLayoutKey = "layout";
        constexpr StringView kOpenKey = "open";

        constexpr float kSideWidth = 0.25f;
        constexpr float kWideSideWidth = 0.4f;
        constexpr float kLeftWidth = 0.22f;
        // Of the narrower rest that a wide right column leaves, so the left column is about as wide as in the other layouts
        constexpr float kLeftWidthBesideWide = 0.275f;
        constexpr float kBottomHeight = 0.3f;
        constexpr float kRightBottomHeight = 0.4f;

        template <typename T>
        void SetValue(JsonMemberMap& map, StringView key, T value) {
            map.SetWith(key, [&](JsonWriter& writer) { writer.Value(value); });
        }

        void WarnWrongType(StringView key, StringView expected) {
            LOG_WARNING("Editor settings: '{}' in {} is not {}; using the default", key, kWorkspacesFileName, expected);
        }

        void ReadOpenStates(JsonView object, WorkspacePanelStates& open) {
            String id;
            for (const JsonMember member : object.Members()) {
                member.key.ReadText(id);
                if (const Optional<bool> value = member.value.AsBool()) {
                    // Of two equal keys the first counts, as for every read of the tape
                    open.insert(MakePair(PoolString::Intern(StringView(id)), *value));
                } else {
                    WarnWrongType(StringView(id), "true or false");
                }
            }
        }

    }  // namespace

    Span<const WorkspacePanel> GetWorkspacePanels() {
        using namespace EditorPanelIds;
        static const Array<WorkspacePanel, 13> panels = {{
            {kViewport, "Viewport"},
            {kSceneEditor, "Scene Editor"},
            {kConfigEditor, "Config Editor###ConfigEditor"},
            {kSpriteLibrary, "Sprite Library###SpriteLibrary"},
            {kAssetBrowser, "Asset Browser###AssetBrowser"},
            {kConsole, "Console###Console"},
            {kProfiler, "Profiler###Profiler"},
            {kPoolStrings, "Pool Strings###PoolStrings"},
            {kGenerate, "Generate###Generate"},
            {kGamePanel, "Game###GamePanel"},
            {kFileChanges, "File changes###FileChanges"},
            {kFieldHelp, "Field Help###FieldHelp"},
            {kActivity, "Activity###Activity"},
        }};
        return {panels.data(), panels.size()};
    }

    const ReadyWorkspace& GetReadyWorkspace(EditorWorkspace workspace) {
        using namespace EditorPanelIds;
        using Slot = WorkspaceSlot;
        static const Array<WorkspaceDock, 4> scene = {{
            {kSceneEditor, Slot::Left},
            {kConfigEditor, Slot::LeftBottom},
            {kViewport, Slot::Center},
            {kSpriteLibrary, Slot::Right},
        }};
        static const Array<WorkspaceDock, 5> ui = {{
            {kSceneEditor, Slot::Left},
            {kViewport, Slot::Center},
            {kAssetBrowser, Slot::CenterBottom},
            {kSpriteLibrary, Slot::Right},
            {kConfigEditor, Slot::RightBottom},
        }};
        static const Array<WorkspaceDock, 4> animation = {{
            {kSceneEditor, Slot::Left},
            {kViewport, Slot::Center},
            {kAssetBrowser, Slot::CenterBottom},
            {kSpriteLibrary, Slot::Right},
        }};
        static const Array<WorkspaceDock, 4> profiling = {{
            {kSceneEditor, Slot::Left},
            {kViewport, Slot::Center},
            {kConsole, Slot::CenterBottom},
            {kProfiler, Slot::Right},
        }};
        static const Array<ReadyWorkspace, EnumUtils<EditorWorkspace>::Count()> ready = {{
            {kSideWidth, kLeftWidth, kBottomHeight, 0.0f, 0.0f, {scene.data(), scene.size()}},
            {kSideWidth, kLeftWidth, 0.0f, kBottomHeight, kRightBottomHeight, {ui.data(), ui.size()}},
            {kSideWidth, kLeftWidth, 0.0f, kBottomHeight, 0.0f, {animation.data(), animation.size()}},
            {kWideSideWidth, kLeftWidthBesideWide, 0.0f, kBottomHeight, 0.0f, {profiling.data(), profiling.size()}},
        }};
        return ready[static_cast<size_t>(workspace)];
    }

    WorkspaceState MakeReadyState(EditorWorkspace workspace) {
        WorkspaceState state;
        for (const WorkspacePanel& panel : GetWorkspacePanels()) {
            state.open[panel.id] = false;
        }
        for (const WorkspaceDock& dock : GetReadyWorkspace(workspace).docks) {
            state.open[dock.panel] = true;
        }
        return state;
    }

    Workspaces::Workspaces(Path userDirectory) : _directory(std::move(userDirectory)) {}

    void Workspaces::Load() {
        _records = {};
        _extra.Clear();
        _extraWorkspaces.Clear();
        _active = EditorWorkspace::Scene;
        _pending = false;
        _unreadable = false;
        if (_directory.Empty()) {
            return;
        }

        String fileText;
        JsonTape tape;
        if (!ReadEditorJsonFile(_directory / kWorkspacesFileName, fileText, tape, _unreadable)) {
            return;
        }
        const JsonView content(tape, 0);
        _extra.Assign(content, {kVersionKey, kActiveKey, kWorkspacesKey});

        String text;
        if (const Optional<JsonView> active = FindParam(content, kActiveKey)) {
            active->ReadText(text);
            if (const Optional<EditorWorkspace> workspace = EnumUtils<EditorWorkspace>::Cast(StringView(text)); active->IsString() && workspace) {
                _active = *workspace;
            } else {
                WarnWrongType(kActiveKey, "the name of a workspace");
            }
        }

        const Optional<JsonView> workspaces = FindParam(content, kWorkspacesKey);
        if (!workspaces) {
            return;
        }
        if (!workspaces->IsObject()) {
            WarnWrongType(kWorkspacesKey, "an object");
            return;
        }
        _extraWorkspaces.Assign(*workspaces, {});
        for (const EditorWorkspace workspace : EnumUtils<EditorWorkspace>::Values()) {
            const StringView name = EnumUtils<EditorWorkspace>::ToString(workspace);
            _extraWorkspaces.Erase(name);
            const Optional<JsonView> entry = FindParam(*workspaces, name);
            if (!entry) {
                continue;
            }
            if (!entry->IsObject()) {
                WarnWrongType(name, "an object");
                continue;
            }
            Record& record = RecordOf(workspace);
            record.extra.Assign(*entry, {kLayoutKey, kOpenKey});
            WorkspaceState state;
            bool stored = false;
            if (const Optional<JsonView> layout = FindParam(*entry, kLayoutKey)) {
                if (layout->IsString()) {
                    layout->ReadText(state.layout);
                    stored = true;
                } else {
                    WarnWrongType(kLayoutKey, "a text");
                }
            }
            if (const Optional<JsonView> open = FindParam(*entry, kOpenKey)) {
                if (open->IsObject()) {
                    ReadOpenStates(*open, state.open);
                    stored = true;
                } else {
                    WarnWrongType(kOpenKey, "an object");
                }
            }
            if (stored) {
                record.state = std::move(state);
            }
        }
    }

    bool Workspaces::Save() {
        if (_directory.Empty()) {
            return false;
        }
        JsonMemberMap workspaces = _extraWorkspaces;
        for (const EditorWorkspace workspace : EnumUtils<EditorWorkspace>::Values()) {
            const Record& record = RecordOf(workspace);
            if (!record.state && record.extra.Empty()) {
                continue;
            }
            JsonMemberMap entry = record.extra;
            if (record.state) {
                if (!record.state->layout.empty()) {
                    SetValue(entry, kLayoutKey, StringView(record.state->layout));
                }
                JsonMemberMap open;
                for (const auto& [panel, isOpen] : record.state->open) {
                    SetValue(open, panel.ToStringView(), isOpen);
                }
                entry.SetWith(kOpenKey, [&](JsonWriter& writer) { open.Write(writer); });
            }
            workspaces.SetWith(EnumUtils<EditorWorkspace>::ToString(workspace), [&](JsonWriter& writer) { entry.Write(writer); });
        }
        JsonMemberMap content = _extra;
        SetValue(content, kVersionKey, kFileVersion);
        SetValue(content, kActiveKey, EnumUtils<EditorWorkspace>::ToString(_active));
        content.SetWith(kWorkspacesKey, [&](JsonWriter& writer) { workspaces.Write(writer); });

        const std::expected<void, String> written = WriteEditorJsonFile(_directory / kWorkspacesFileName, content, _unreadable);
        if (!written) {
            LOG_ERROR("Editor settings: {}", written.error());
            return false;
        }
        return true;
    }

    const WorkspaceState* Workspaces::FindStored(EditorWorkspace workspace) const {
        const Optional<WorkspaceState>& state = RecordOf(workspace).state;
        return state ? &*state : nullptr;
    }

    Optional<WorkspaceState> Workspaces::SwitchTo(EditorWorkspace target, const WorkspaceState* shown) {
        if (target == _active) {
            return std::nullopt;
        }
        if (!_pending && shown != nullptr) {
            // Merged: a panel that is not alive now keeps what was stored of it
            WorkspaceState& left = ActiveState();
            left.layout = shown->layout;
            for (const auto& [panel, open] : shown->open) {
                left.open[panel] = open;
            }
        }
        _active = target;
        _pending = true;
        return Answer(target);
    }

    WorkspaceState Workspaces::Reset() {
        RecordOf(_active).state.reset();
        _pending = true;
        return MakeReadyState(_active);
    }

    void Workspaces::LoadLayout(StringView text) {
        ActiveState().layout = String(text);
        _pending = true;
    }

    WorkspaceState Workspaces::Restore() {
        _pending = true;
        return Answer(_active);
    }

    void Workspaces::StoreLayout(StringView text) {
        if (!_pending) {
            ActiveState().layout = String(text);
        }
    }

    bool Workspaces::NoteOpen(PoolString panel, bool open) {
        if (_pending) {
            return false;
        }
        WorkspacePanelStates& states = ActiveState().open;
        const auto found = states.find(panel);
        if (found == states.end()) {
            states.insert(MakePair(panel, open));
            return false;
        }
        if (found->second == open) {
            return false;
        }
        found->second = open;
        return true;
    }

    WorkspaceState& Workspaces::ActiveState() {
        Optional<WorkspaceState>& state = RecordOf(_active).state;
        if (!state) {
            state.emplace();
        }
        return *state;
    }

    WorkspaceState Workspaces::Answer(EditorWorkspace workspace) const {
        const WorkspaceState* const stored = FindStored(workspace);
        if (stored != nullptr && !stored->layout.empty()) {
            // ImGui drops the dock tree before it reads one, so such a text must never reach it
            if (!LooksLikeLayoutText(StringView(stored->layout))) {
                LOG_WARNING("Editor: the stored layout of the workspace {} has no dock tree; the ready one is used", EnumUtils<EditorWorkspace>::ToString(workspace));
                return MakeReadyState(workspace);
            }
            return WorkspaceState{MigrateLegacyWindowNames(StringView(stored->layout)), stored->open};
        }
        WorkspaceState state = MakeReadyState(workspace);
        if (stored != nullptr) {
            for (const auto& [panel, open] : stored->open) {
                state.open[panel] = open;
            }
        }
        return state;
    }

}  // namespace BECore
