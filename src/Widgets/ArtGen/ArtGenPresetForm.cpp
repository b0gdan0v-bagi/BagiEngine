#include "ArtGenPresetForm.h"

namespace BECore {

    namespace {

        constexpr StringView kReferenceImageInput = "reference_image";
        constexpr StringView kServiceInputs[] = {"prompt", "negative", "seed", "width", "height", "output", kReferenceImageInput};
        // The keys of a written file in order, `reference` last of the form's own; any other key follows them
        constexpr StringView kFileOrder[] = {"preset", "title", "workflow", "prompt", "negative", "size", "params", "accept", "reference"};
        constexpr StringView kCopySuffix = "_copy";

        void CopyText(char* out, size_t size, StringView text) {
            const size_t length = std::min(text.size(), size - 1);
            std::memcpy(out, text.data(), length);
            out[length] = '\0';
        }

        StringView ViewOf(const String& text) {
            return StringView(text.data(), text.size());
        }

        String StringOf(const Json& value) {
            return value.is_string() ? String(FromStd(value.get_ref<const std::string&>())) : String();  //-NoStd: nlohmann holds a std::string
        }

        bool IsPositiveInt(const Json& value) {
            return value.is_number_integer() && value.get<int64_t>() > 0;
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

    String FormatArtGenPresetFile(const Json& preset) {
        String text = "{\n";
        bool first = true;
        auto put = [&](const std::string& key, const Json& value) {  //-NoStd: the keys of a nlohmann object
            text += first ? "  " : ",\n  ";
            first = false;
            text += DumpJson(Json(key));
            text += ": ";
            text += DumpJson(value);
        };
        for (const StringView key : kFileOrder) {
            const Json* value = FindJsonMember(preset, key);
            if (value != nullptr) {
                put(std::string(key.data(), key.size()), *value);  //-NoStd: a nlohmann key
            }
        }
        for (const auto& [key, value] : preset.items()) {
            const StringView name(key.data(), key.size());
            if (std::find(std::begin(kFileOrder), std::end(kFileOrder), name) == std::end(kFileOrder)) {
                put(key, value);
            }
        }
        text += "\n}\n";
        return text;
    }

    std::expected<void, String> CheckArtGenPresetShape(const Json& preset) {
        if (!preset.is_object()) {
            return std::unexpected(String("a preset is a JSON object"));
        }
        const auto format = GetJsonInt(preset, "preset");
        if (!format || *format != ArtGenPresetForm::kFormat) {
            return std::unexpected(Format("\"preset\" must be {} (the format version)", ArtGenPresetForm::kFormat));
        }
        for (const StringView key : {StringView("workflow"), StringView("prompt")}) {
            const auto value = GetJsonString(preset, key);
            if (!value || value->empty()) {
                return std::unexpected(Format("\"{}\" is required", key));
            }
        }
        if (const Json* size = FindJsonMember(preset, "size"); size != nullptr) {
            if (!size->is_array() || size->size() != 2 || !IsPositiveInt((*size)[0]) || !IsPositiveInt((*size)[1])) {
                return std::unexpected(String("\"size\" must be [width, height] of positive integers"));
            }
        }
        if (const Json* params = FindJsonMember(preset, "params"); params != nullptr && !params->is_object()) {
            return std::unexpected(String("\"params\" must be an object"));
        }
        if (const Json* accept = FindJsonMember(preset, "accept"); accept != nullptr) {
            if (const auto settings = ArtGenImageSettingsFromJson(*accept); !settings) {
                return std::unexpected(Format("\"accept\": {}", settings.error()));
            }
        }
        if (const Json* reference = FindJsonMember(preset, "reference"); reference != nullptr) {
            if (const auto read = ReadArtGenReference(*reference); !read) {
                return std::unexpected(read.error());
            }
        }
        return {};
    }

    bool ArtGenPresetForm::ReadAccept(const Json& value) {
        const auto settings = ArtGenImageSettingsFromJson(value);
        if (!settings) {
            return false;
        }
        _accept = _acceptLoaded = *settings;
        _acceptInFile = true;
        _acceptAsLoaded = value;
        return true;
    }

    bool ArtGenPresetForm::ReadReference(const Json& value) {
        auto reference = ReadArtGenReference(value);
        if (!reference) {
            return false;
        }
        _reference = _referenceLoaded = std::move(*reference);
        _referenceAsLoaded = value;
        return true;
    }

    void ArtGenPresetForm::FromJson(const Json& data) {
        name[0] = title[0] = prompt[0] = negative[0] = '\0';
        width = height = 0;
        _workflow.clear();
        _params.clear();
        _extra = Json::object();
        _accept = {};
        _acceptInFile = false;
        _acceptAsLoaded = Json();
        _acceptLoaded = {};
        _reference.reset();
        _referenceAsLoaded = Json();
        _referenceLoaded.reset();
        if (!data.is_object()) {
            return;
        }
        for (const auto& [key, value] : data.items()) {
            const StringView field(key.data(), key.size());
            if (field == "title") {
                CopyText(title, sizeof(title), ViewOf(StringOf(value)));
            } else if (field == "workflow") {
                _workflow = StringOf(value);
            } else if (field == "prompt") {
                CopyText(prompt, sizeof(prompt), ViewOf(StringOf(value)));
            } else if (field == "negative") {
                CopyText(negative, sizeof(negative), ViewOf(StringOf(value)));
            } else if (field == "size" && value.is_array() && value.size() == 2 && IsPositiveInt(value[0]) && IsPositiveInt(value[1])) {
                width = static_cast<int32_t>(std::min<int64_t>(value[0].get<int64_t>(), INT32_MAX));
                height = static_cast<int32_t>(std::min<int64_t>(value[1].get<int64_t>(), INT32_MAX));
            } else if (field == "params" && value.is_object()) {
                for (const auto& [param, paramValue] : value.items()) {
                    SetParam(StringView(param.data(), param.size()), paramValue);
                }
            } else if (field == "accept" && value.is_object() && ReadAccept(value)) {
                continue;
            } else if (field == "reference" && ReadReference(value)) {
                continue;
            } else if (field != "preset") {
                _extra[key] = value;
            }
        }
    }

    Json ArtGenPresetForm::ToJson() const {
        Json data = _extra;
        data["preset"] = kFormat;
        if (title[0] != '\0') {
            data["title"] = MakeJsonString(StringView(title));
        }
        data["workflow"] = MakeJsonString(ViewOf(_workflow));
        data["prompt"] = MakeJsonString(StringView(prompt));
        if (negative[0] != '\0') {
            data["negative"] = MakeJsonString(StringView(negative));
        }
        if (width > 0 && height > 0) {
            data["size"] = Json::array({width, height});
        }
        if (!_params.empty()) {
            Json params = Json::object();
            for (const auto& param : _params) {
                params[ToStdString(param.name)] = param.value;
            }
            data["params"] = std::move(params);
        }
        if (HasImageAccept()) {
            data["accept"] = (_acceptInFile && _accept == _acceptLoaded) ? _acceptAsLoaded : ArtGenImageSettingsToJson(_accept);
        }
        if (_reference) {
            data["reference"] = (_referenceLoaded && *_reference == *_referenceLoaded) ? _referenceAsLoaded : ArtGenReferenceToJson(*_reference);
        }
        return data;
    }

    void ArtGenPresetForm::Load(const ArtGenPreset& preset) {
        FromJson(preset.data);
        CopyText(name, sizeof(name), ViewOf(preset.name));
        _base = preset.data;
        _baseName = preset.name;
        _baseSource = preset.source;
        Touch();
    }

    void ArtGenPresetForm::New(StringView workflow) {
        FromJson(Json::object());
        CopyText(name, sizeof(name), "new_preset");
        _workflow = String(workflow);
        CopyText(prompt, sizeof(prompt), "{subject}");
        _base = Json();
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
        _base = Json();
        _baseName.clear();
        _baseSource.clear();
        Touch();
    }

    void ArtGenPresetForm::Revert() {
        if (!_base.is_null()) {
            const String baseName = _baseName;
            FromJson(_base);
            CopyText(name, sizeof(name), ViewOf(baseName));
            Touch();
        }
    }

    void ArtGenPresetForm::MarkSaved() {
        _base = ToJson();
        if (const Json* accept = FindJsonMember(_base, "accept"); accept != nullptr) {
            ReadAccept(*accept);
        }
        _referenceLoaded.reset();
        if (const Json* reference = FindJsonMember(_base, "reference"); reference != nullptr) {
            ReadReference(*reference);
        }
        _baseName = String(name);
        _baseSource = "game";
        Touch();
    }

    bool ArtGenPresetForm::IsDirty() const {
        if (_dirtyRevision != _revision) {
            _dirtyRevision = _revision;
            _dirty = _base.is_null() || ToJson() != _base || ViewOf(_baseName) != StringView(name);
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
            _acceptAsLoaded = Json();
            Touch();
        }
    }

    void ArtGenPresetForm::SetReference(const ArtGenReference& reference) {
        if (_reference != reference || HasUnreadableReference()) {
            _reference = reference;
            _extra.erase("reference");
            Touch();
        }
    }

    void ArtGenPresetForm::ClearReference() {
        if (_reference || HasUnreadableReference()) {
            _reference.reset();
            _extra.erase("reference");
            Touch();
        }
    }

    const Json* ArtGenPresetForm::FindParam(StringView input) const {
        for (const auto& param : _params) {
            if (ViewOf(param.name) == input) {
                return &param.value;
            }
        }
        return nullptr;
    }

    void ArtGenPresetForm::SetParam(StringView input, Json value) {
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
