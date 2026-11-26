#include "FieldHelpPanelModel.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Text/Utf8.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Generated/FieldHelpPanelModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorDocumentRows.h>
#include <Widgets/Feedback/ToastEvents.h>
#include <Widgets/SceneEditor/FieldHelpEvents.h>
#include <Widgets/SceneEditor/FieldTooltips.h>

namespace BECore {

    namespace {

        /// The longest description a cell of the table shows
        constexpr size_t kMaxSummaryBytes = 72;
        constexpr StringView kMore = "...";

        void Assign(String& out, StringView text) {
            out.assign(text.data(), text.size());
        }

        StringView SourceWord(FieldHelpSource source) {
            switch (source) {
                case FieldHelpSource::Engine:
                    return "engine";
                case FieldHelpSource::Game:
                    return "game";
                case FieldHelpSource::Attribute:
                    return "attribute";
                case FieldHelpSource::None:
                    break;
            }
            return "none";
        }

        /// The first line of @p text, cut between two characters to the longest summary; the mark says there is more
        void AssignSummary(String& out, StringView text) {
            const StringView line = text.substr(0, text.find('\n'));
            size_t size = 0;
            while (size < line.size()) {
                size_t next = size;
                Utf8::DecodeNext(line, next);
                if (next > kMaxSummaryBytes) {
                    break;
                }
                size = next;
            }
            Assign(out, line.substr(0, size));
            if (size < text.size()) {
                out.append(kMore.data(), kMore.size());
            }
        }

    }  // namespace

    void FieldHelpPanelModel::SetFileSystem(const FileSystem& fileSystem) {
        _help = FieldHelp(fileSystem);
    }

    void FieldHelpPanelModel::Initialize() {
        Subscribe<ConfigEvents::ConfigChangedEvent, &FieldHelpPanelModel::OnConfigChanged>(this);
        _view.newEntryFile = _help.Files().GetMountLayers(MountPoints::config).size() > 1 ? FieldHelpFile::Game : FieldHelpFile::Engine;
        BuildClasses();
        _help.Reload();
        _documents = CoreManager::GetEditorDocuments().Add([this](Vector<EditorDocumentState>& out) { AppendDocuments(out); });
    }

    TypedRef FieldHelpPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void FieldHelpPanelModel::Update() {
        ApplyEdits();
        RebuildRowsIfStale();
    }

    bool FieldHelpPanelModel::RunCommand(StringView id, StringView argument) {
        const bool select = id == PoolString(EditorCommandIds::kFieldHelpSelect).ToStringView();
        const bool save = id == PoolString(EditorCommandIds::kFieldHelpSave).ToStringView();
        const bool revert = id == PoolString(EditorCommandIds::kFieldHelpRevert).ToStringView();
        if (!select && !save && !revert) {
            return false;
        }
        // A click is emitted after the walk that wrote the controls, so their edits still wait for an `Update`
        ApplyEdits();
        if (select) {
            Select(argument);
        } else if (save) {
            Save();
        } else {
            Reread();
        }
        RebuildRowsIfStale();
        return true;
    }

    void FieldHelpPanelModel::OnOpenChanged(bool open) {
        // A closed panel is not updated, so an edit of the last frame it was drawn is taken here
        ApplyEdits();
        if (open && !IsDirty()) {
            Reread();
        }
    }

    void FieldHelpPanelModel::OnConfigChanged(const ConfigEvents::ConfigChangedEvent& event) {
        if (!FieldHelp::IsHelpConfig(event.name)) {
            return;
        }
        ApplyEdits();
        if (!IsDirty()) {
            Reread();
        }
    }

    void FieldHelpPanelModel::BuildClasses() {
        _classes = AbstractFactory<IComponent>::GetInstance().GetRegisteredTypes();
        const auto byName = [](const ClassMeta& left, const ClassMeta& right) { return left.typeName < right.typeName; };
        eastl::sort(_classes.begin(), _classes.end(), byName);
        // A second registration of a type lists it again
        _classes.erase(eastl::unique(_classes.begin(), _classes.end()), _classes.end());
        _view.classes.clear();
        _view.classes.reserve(_classes.size());
        for (const ClassMeta& meta : _classes) {
            _view.classes.emplace_back(meta.typeName.data(), meta.typeName.size());
        }
    }

    StringView FieldHelpPanelModel::ChosenClass() const {
        return _shownClass ? _classes[static_cast<size_t>(*_shownClass)].typeName : StringView{};
    }

    void FieldHelpPanelModel::ApplyEdits() {
        // The text belongs to the selection it was typed for, so it is taken before another class drops that
        if (!_selected.empty() && _view.description != _draft) {
            ApplyDescription();
        }
        if (!_shownClass || *_shownClass != _view.classIndex) {
            ChooseClass(_view.classIndex);
        }
    }

    void FieldHelpPanelModel::ApplyDescription() {
        _draft = _view.description;
        const Optional<FieldHelpFile> own = _heldBy ? _heldBy : _createdIn;
        const FieldHelpFile file = own.value_or(_view.newEntryFile);
        if (!_draft.empty()) {
            _help.Set(file, ChosenClass(), _selected, StringView(_draft.data(), _draft.size()));
            if (!_heldBy) {
                _createdIn = file;
            }
        } else if (own) {
            _help.Remove(file, ChosenClass(), _selected);
            // A field that had no entry is new again: the next text goes where the choice says then
            _createdIn.reset();
        }
        _view.dirty = IsDirty();
    }

    void FieldHelpPanelModel::ChooseClass(int32_t index) {
        if (_classes.empty()) {
            return;
        }
        _view.classIndex = eastl::clamp(index, 0, static_cast<int32_t>(_classes.size()) - 1);
        if (_shownClass && *_shownClass == _view.classIndex) {
            return;
        }
        _shownClass = _view.classIndex;
        ClearSelection();
        _rowsStale = true;
    }

    void FieldHelpPanelModel::Select(StringView field) {
        const TypeInfo* const type = TypeRegistry::GetInstance().Find(ChosenClass());
        const FieldDesc* const desc = type != nullptr ? FindReflectedField(*type, field) : nullptr;
        if (desc == nullptr || desc->attributes.hidden) {
            return;
        }
        _selected = desc->name;
        _view.hasSelection = true;
        Assign(_view.selectedField, _selected);
        for (FieldHelpRow& row : _view.rows) {
            row.selected = StringView(row.name.data(), row.name.size()) == _selected;
        }
        LoadDraft();
    }

    void FieldHelpPanelModel::ClearSelection() {
        _selected = {};
        _draft.clear();
        _heldBy.reset();
        _createdIn.reset();
        _view.hasSelection = false;
        _view.selectedField.clear();
        _view.description.clear();
    }

    void FieldHelpPanelModel::LoadDraft() {
        if (_selected.empty()) {
            return;
        }
        Assign(_draft, _help.Find(ChosenClass(), _selected).text);
        _view.description = _draft;
        _heldBy = _help.HolderOf(ChosenClass(), _selected);
        _createdIn.reset();
    }

    void FieldHelpPanelModel::Save() {
        bool written = false;
        for (const FieldHelpFile file : EnumUtils<FieldHelpFile>::Values()) {
            if (!_help.IsModified(file)) {
                continue;
            }
            if (const std::expected<void, String> saved = _help.Save(file); !saved) {
                LOG_WARNING("FieldHelp: not saved: {}", saved.error());
                EditorToastEvent::Emit(ToastLevel::Error, "Save failed: {}"_format(saved.error()));
            } else {
                written = true;
            }
        }
        _view.dirty = IsDirty();
        if (written) {
            FieldHelpSavedEvent::Emit();
        }
    }

    void FieldHelpPanelModel::Reread() {
        _help.Reload();
        LoadDraft();
        _view.dirty = IsDirty();
    }

    bool FieldHelpPanelModel::IsDirty() const {
        return _help.IsModified(FieldHelpFile::Engine) || _help.IsModified(FieldHelpFile::Game);
    }

    void FieldHelpPanelModel::RebuildRowsIfStale() {
        if (_rowsStale || _help.GetVersion() != _builtVersion) {
            RebuildRows();
        }
    }

    void FieldHelpPanelModel::RebuildRows() {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* const type = registry.Find(ChosenClass());
        size_t count = 0;
        if (type != nullptr) {
            const ClassMeta& meta = _classes[static_cast<size_t>(*_shownClass)];
            for (const FieldRef& ref : registry.AllFields(*type)) {
                const FieldDesc& field = *ref.field;
                if (field.attributes.hidden) {
                    continue;
                }
                if (count == _view.rows.size()) {
                    _view.rows.emplace_back();
                }
                FieldHelpRow& row = _view.rows[count++];
                Assign(row.name, field.name);
                row.type.clear();
                AppendTypeWords(row.type, *field.type);
                row.range = RangeWords(field.attributes);
                Assign(row.defaultText, _defaults.DefaultText(meta, field.name).value_or(StringView{}));
                const FieldHelpAnswer answer = _help.Find(meta.typeName, field.name);
                AssignSummary(row.description, answer.text);
                Assign(row.fullDescription, answer.text);
                Assign(row.source, SourceWord(answer.source));
                row.selected = field.name == _selected;
            }
        }
        _view.rows.resize(count);
        _view.hasRows = count > 0;
        _builtVersion = _help.GetVersion();
        _rowsStale = false;
    }

    void FieldHelpPanelModel::AppendDocuments(Vector<EditorDocumentState>& out) const {
        for (const FieldHelpFile file : EnumUtils<FieldHelpFile>::Values()) {
            if (_help.IsModified(file)) {
                out.push_back(MakeConfigFileDocument(_help.Files(), FieldHelp::ConfigName(file).ToStringView(), true, _help.SourcePath(file)));
            }
        }
    }

}  // namespace BECore
