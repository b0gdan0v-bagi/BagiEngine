#include "ArtGenPresetForm.h"

#include <BECore/Control/ControlParams.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonViewOps.h>
#include <Widgets/ArtGen/ArtGenAccept.h>

namespace BECore {

    namespace {

        constexpr StringView kReferenceImageInput = "reference_image";
        constexpr StringView kServiceInputs[] = {"prompt", "negative", "seed", "width", "height", "output", kReferenceImageInput};
        // The keys of a written file in order, `reference` last of the form's own; any other key follows them
        constexpr StringView kFileOrder[] = {"preset", "title", "workflow", "prompt", "negative", "size", "params", "accept", "atlas", "reference"};
        constexpr StringView kCopySuffix = "_copy";

        void CopyText(char* out, size_t size, StringView text) {
            const size_t length = std::min(text.size(), size - 1);
            std::memcpy(out, text.data(), length);
            out[length] = '\0';
        }

        StringView ViewOf(const String& text) {
            return StringView(text.data(), text.size());
        }

        /// `[width, height]` of two positive integers
        bool ReadSizePair(JsonView value, int64_t& first, int64_t& second) {
            if (!value.IsArray() || value.Size() != 2) {
                return false;
            }
            Optional<int64_t> numbers[2];
            size_t index = 0;
            for (const JsonView item : value.Items()) {
                numbers[index++] = item.AsInt64();
            }
            if (!numbers[0] || !numbers[1] || *numbers[0] <= 0 || *numbers[1] <= 0) {
                return false;
            }
            first = *numbers[0];
            second = *numbers[1];
            return true;
        }

        /// Whether two texts are the same JSON value, as `Json::operator==` judged the documents: the key order and the form of a number do not matter
        bool SameContent(const String& a, const String& b) {
            JsonTape tapeA;
            JsonTape tapeB;
            static_cast<void>(tapeA.Parse(ViewOf(a)));
            static_cast<void>(tapeB.Parse(ViewOf(b)));
            return JsonEquals(JsonView(tapeA, 0), JsonView(tapeB, 0));
        }

    }  // namespace

    const ArtGenWorkflowInput* ArtGenWorkflow::FindInput(StringView input) const {
        for (const auto& candidate : inputs) {
            if (ViewOf(candidate.name) == input) {
                return &candidate;
            }
        }
        return nullptr;
    }

    bool ArtGenWorkflow::HasSize() const {
        return FindInput("width") != nullptr && FindInput("height") != nullptr;
    }

    bool ArtGenWorkflow::IsReferenceVariant() const {
        return FindInput(kReferenceImageInput) != nullptr;
    }

    bool IsArtGenServiceInput(StringView input) {
        return std::find(std::begin(kServiceInputs), std::end(kServiceInputs), input) != std::end(kServiceInputs);
    }

    String FormatArtGenPresetFile(JsonView preset) {
        String text = "{\n";
        bool first = true;
        const auto put = [&](StringView key, JsonView value) {
            text += first ? "  " : ",\n  ";
            first = false;
            {
                JsonWriter keyWriter(text);
                keyWriter.Value(key);
            }
            text += ": ";
            JsonWriter valueWriter(text);
            WriteJsonSorted(valueWriter, value);
        };
        for (const StringView key : kFileOrder) {
            if (const Optional<JsonView> value = FindParam(preset, key)) {
                put(key, *value);
            }
        }
        Vector<String> others;
        for (const JsonMember member : preset.Members()) {
            String key;
            member.key.ReadText(key);
            if (std::find(std::begin(kFileOrder), std::end(kFileOrder), StringView(key)) == std::end(kFileOrder)) {
                others.push_back(std::move(key));
            }
        }
        std::sort(others.begin(), others.end());
        others.erase(std::unique(others.begin(), others.end()), others.end());
        for (const String& key : others) {
            put(StringView(key), *FindParam(preset, StringView(key)));
        }
        text += "\n}\n";
        return text;
    }

    std::expected<void, String> CheckArtGenPresetShape(JsonView preset) {
        if (!preset.IsObject()) {
            return std::unexpected(String("a preset is a JSON object"));
        }
        const auto format = GetParamInt(preset, "preset");
        if (!format || *format != ArtGenPresetForm::kFormat) {
            return std::unexpected(Format("\"preset\" must be {} (the format version)", ArtGenPresetForm::kFormat));
        }
        for (const StringView key : {StringView("workflow"), StringView("prompt")}) {
            const auto value = GetParamString(preset, key);
            if (!value || value->empty()) {
                return std::unexpected(Format("\"{}\" is required", key));
            }
        }
        if (const Optional<JsonView> size = FindParam(preset, "size")) {
            if (int64_t sideX = 0, sideY = 0; !ReadSizePair(*size, sideX, sideY)) {
                return std::unexpected(String("\"size\" must be [width, height] of positive integers"));
            }
        }
        if (const Optional<JsonView> params = FindParam(preset, "params"); params && !params->IsObject()) {
            return std::unexpected(String("\"params\" must be an object"));
        }
        if (const Optional<JsonView> accept = FindParam(preset, "accept")) {
            if (const auto settings = ReadArtGenImageSettings(*accept); !settings) {
                return std::unexpected(Format("\"accept\": {}", settings.error()));
            }
        }
        if (FindParam(preset, "atlas")) {
            const auto group = GetParamString(preset, "atlas");
            if (!group || !IsValidSpriteName(StringView(*group))) {
                return std::unexpected(String("\"atlas\" must be an atlas group name: 1 to 64 of A-Z a-z 0-9 _ -"));
            }
        }
        if (const Optional<JsonView> reference = FindParam(preset, "reference")) {
            if (const auto read = ReadArtGenReference(*reference); !read) {
                return std::unexpected(read.error());
            }
        }
        return {};
    }

    bool ArtGenPresetForm::ReadAccept(JsonView value) {
        const auto settings = ReadArtGenImageSettings(value);
        if (!settings) {
            return false;
        }
        _accept = _acceptLoaded = *settings;
        _acceptInFile = true;
        _acceptAsLoaded = String(value.Raw());
        return true;
    }

    bool ArtGenPresetForm::ReadReference(JsonView value) {
        auto reference = ReadArtGenReference(value);
        if (!reference) {
            return false;
        }
        _reference = _referenceLoaded = std::move(*reference);
        _referenceAsLoaded = String(value.Raw());
        return true;
    }

    void ArtGenPresetForm::FromText(StringView data) {
        JsonTape tape;
        if (!tape.Parse(data)) {
            FromView(JsonView());
            return;
        }
        FromView(JsonView(tape, 0));
    }

    void ArtGenPresetForm::FromView(JsonView data) {
        name[0] = title[0] = prompt[0] = negative[0] = '\0';
        width = height = 0;
        _workflow.clear();
        _params.clear();
        _extra.Clear();
        _accept = {};
        _acceptInFile = false;
        _acceptAsLoaded.clear();
        _acceptLoaded = {};
        _reference.reset();
        _referenceAsLoaded.clear();
        _referenceLoaded.reset();
        if (!data.IsObject()) {
            return;
        }
        // Every key but the ones the form owns outright is kept; a key the form shows is taken out again below once it reads
        _extra.Assign(data, {"preset", "title", "workflow", "prompt", "negative"});
        const auto copyText = [&](StringView key, char* out, size_t size) {
            String text;
            if (const Optional<JsonView> value = data.Find(key)) {
                value->ReadText(text);
            }
            CopyText(out, size, ViewOf(text));
        };
        copyText("title", title, sizeof(title));
        copyText("prompt", prompt, sizeof(prompt));
        copyText("negative", negative, sizeof(negative));
        if (const Optional<JsonView> workflow = data.Find("workflow")) {
            workflow->ReadText(_workflow);
        }
        const Optional<JsonView> size = data.Find("size");
        if (int64_t sideX = 0, sideY = 0; size && ReadSizePair(*size, sideX, sideY)) {
            width = static_cast<int32_t>(std::min<int64_t>(sideX, INT32_MAX));
            height = static_cast<int32_t>(std::min<int64_t>(sideY, INT32_MAX));
            _extra.Erase("size");
        }
        if (const Optional<JsonView> params = data.Find("params"); params && params->IsObject()) {
            for (const JsonMember member : params->Members()) {
                String param;
                member.key.ReadText(param);
                if (FindParam(StringView(param)) == nullptr) {
                    SetParam(StringView(param), ArtGenInputValue::Read(member.value));
                }
            }
            _extra.Erase("params");
        }
        if (const Optional<JsonView> accept = data.Find("accept"); accept && accept->IsObject() && ReadAccept(*accept)) {
            _extra.Erase("accept");
        }
        if (const Optional<JsonView> reference = data.Find("reference"); reference && ReadReference(*reference)) {
            _extra.Erase("reference");
        }
    }

    String ArtGenPresetForm::ToJsonText() const {
        JsonMemberMap data = _extra;
        data.SetWith("preset", [](JsonWriter& writer) { writer.Value(ArtGenPresetForm::kFormat); });
        if (title[0] != '\0') {
            data.SetWith("title", [&](JsonWriter& writer) { writer.Value(StringView(title)); });
        }
        data.SetWith("workflow", [&](JsonWriter& writer) { writer.Value(ViewOf(_workflow)); });
        data.SetWith("prompt", [&](JsonWriter& writer) { writer.Value(StringView(prompt)); });
        if (negative[0] != '\0') {
            data.SetWith("negative", [&](JsonWriter& writer) { writer.Value(StringView(negative)); });
        }
        if (width > 0 && height > 0) {
            data.SetWith("size", [&](JsonWriter& writer) {
                writer.BeginArray();
                writer.Value(width);
                writer.Value(height);
                writer.EndArray();
            });
        }
        if (!_params.empty()) {
            JsonMemberMap params;
            for (const auto& param : _params) {
                params.SetWith(ViewOf(param.name), [&](JsonWriter& writer) { param.value.Write(writer); });
            }
            data.SetWith("params", [&](JsonWriter& writer) { params.Write(writer); });
        }
        if (HasImageAccept()) {
            if (_acceptInFile && _accept == _acceptLoaded) {
                data.Set("accept", ViewOf(_acceptAsLoaded));
            } else {
                data.SetWith("accept", [&](JsonWriter& writer) { WriteArtGenImageSettings(writer, _accept); });
            }
        }
        if (_reference) {
            if (_referenceLoaded && *_reference == *_referenceLoaded) {
                data.Set("reference", ViewOf(_referenceAsLoaded));
            } else {
                data.SetWith("reference", [&](JsonWriter& writer) { WriteArtGenReference(writer, *_reference); });
            }
        }
        String text;
        JsonWriter writer(text);
        data.Write(writer);
        return text;
    }

    void ArtGenPresetForm::Load(const ArtGenPreset& preset) {
        FromText(ViewOf(preset.data));
        CopyText(name, sizeof(name), ViewOf(preset.name));
        _base = preset.data;
        _baseName = preset.name;
        _baseSource = preset.source;
        Touch();
    }

    void ArtGenPresetForm::New(StringView workflow) {
        FromView(JsonView());
        CopyText(name, sizeof(name), "new_preset");
        _workflow = String(workflow);
        CopyText(prompt, sizeof(prompt), "{subject}");
        _base.clear();
        _baseName.clear();
        _baseSource.clear();
        Touch();
    }

    void ArtGenPresetForm::Duplicate() {
        const size_t length = std::strlen(name);
        if (length + kCopySuffix.size() < sizeof(name)) {
            std::memcpy(name + length, kCopySuffix.data(), kCopySuffix.size());
            name[length + kCopySuffix.size()] = '\0';
        }
        _base.clear();
        _baseName.clear();
        _baseSource.clear();
        Touch();
    }

    void ArtGenPresetForm::Revert() {
        if (!_base.empty()) {
            const String baseName = _baseName;
            FromText(ViewOf(_base));
            CopyText(name, sizeof(name), ViewOf(baseName));
            Touch();
        }
    }

    void ArtGenPresetForm::MarkSaved() {
        _base = ToJsonText();
        JsonTape tape;
        if (tape.Parse(ViewOf(_base))) {
            const JsonView saved(tape, 0);
            if (const Optional<JsonView> accept = saved.Find("accept")) {
                ReadAccept(*accept);
            }
            _referenceLoaded.reset();
            if (const Optional<JsonView> reference = saved.Find("reference")) {
                ReadReference(*reference);
            }
        }
        _baseName = String(name);
        _baseSource = "game";
        Touch();
    }

    bool ArtGenPresetForm::IsDirty() const {
        if (_dirtyRevision != _revision) {
            _dirtyRevision = _revision;
            _dirty = _base.empty() || !SameContent(ToJsonText(), _base) || ViewOf(_baseName) != StringView(name);
        }
        return _dirty;
    }

    void ArtGenPresetForm::SetWorkflow(StringView workflow, const ArtGenWorkflow* described) {
        _workflow = String(workflow);
        if (described != nullptr) {
            const auto end = std::remove_if(_params.begin(), _params.end(), [&](const Param& param) { return described->FindInput(ViewOf(param.name)) == nullptr; });
            _params.erase(end, _params.end());
            if (!described->HasSize()) {
                width = height = 0;
            }
        }
        Touch();
    }

    void ArtGenPresetForm::SetImageAccept(const ArtGenImageSettings& settings) {
        if (settings != _accept) {
            _accept = settings;
            Touch();
        }
    }

    void ArtGenPresetForm::ClearImageAccept() {
        if (HasImageAccept()) {
            _accept = {};
            _acceptInFile = false;
            _acceptAsLoaded.clear();
            Touch();
        }
    }

    void ArtGenPresetForm::SetReference(const ArtGenReference& reference) {
        if (_reference != reference || HasUnreadableReference()) {
            _reference = reference;
            _extra.Erase("reference");
            Touch();
        }
    }

    void ArtGenPresetForm::ClearReference() {
        if (_reference || HasUnreadableReference()) {
            _reference.reset();
            _extra.Erase("reference");
            Touch();
        }
    }

    const ArtGenInputValue* ArtGenPresetForm::FindParam(StringView input) const {
        for (const auto& param : _params) {
            if (ViewOf(param.name) == input) {
                return &param.value;
            }
        }
        return nullptr;
    }

    void ArtGenPresetForm::SetParam(StringView input, ArtGenInputValue value) {
        Param* slot = nullptr;
        for (auto& param : _params) {
            if (ViewOf(param.name) == input) {
                slot = &param;
            }
        }
        if (slot == nullptr) {
            slot = &_params.emplace_back();
            slot->name = String(input);
        }
        slot->value = std::move(value);
        Touch();
    }

    void ArtGenPresetForm::ClearParam(StringView input) {
        const auto end = std::remove_if(_params.begin(), _params.end(), [&](const Param& param) { return ViewOf(param.name) == input; });
        if (end != _params.end()) {
            _params.erase(end, _params.end());
            Touch();
        }
    }

}  // namespace BECore
