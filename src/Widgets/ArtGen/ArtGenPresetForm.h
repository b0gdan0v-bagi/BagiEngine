#pragma once

#include <BECore/Control/Json.h>
#include <BECore/Types/Types.h>
#include <Widgets/ArtGen/ArtGenImage.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// One named input of a workflow as the service described it: the graph's own value and, for a combo, what the generator offers
    struct ArtGenWorkflowInput {
        String name;
        Json defaultValue;
        Json choices;  // an array, or null for a free value
    };

    /// A workflow the game can use (tools/artgen/artgen_preset.py `Workflow.describe`); a broken one carries its error
    struct ArtGenWorkflow {
        String name;
        String source;  // "game" or "engine"
        String kind;    // what it makes: "image" or "audio"
        String error;
        Vector<ArtGenWorkflowInput> inputs;

        [[nodiscard]] const ArtGenWorkflowInput* FindInput(StringView input) const;
        /// The workflow takes `size` (it names both `width` and `height`)
        [[nodiscard]] bool HasSize() const;
    };

    /// A style preset the service reported: the game's file, an engine starter (`source` "engine"), or a broken file with its error. `data`
    /// is the preset's JSON as written (null when the file is not JSON)
    struct ArtGenPreset {
        String name;
        String title;
        String source;
        String kind;  // "image" or "audio": its workflow's
        String error;
        Json data;
    };

    /// The named inputs the service fills itself, never a param of a preset (tools/artgen/CLAUDE.md, "Presets")
    [[nodiscard]] bool IsArtGenServiceInput(StringView input);

    /// A preset as the text of its file: the keys in the order of the docs (preset, title, workflow, prompt, negative, size, params, accept,
    /// then any other), two-space indented, a newline at the end
    [[nodiscard]] String FormatArtGenPresetFile(const Json& preset);

    /// Checks the shape the C++ side owns before a save: an object with `preset` 1, `workflow` and `prompt` text, `size` [w, h] of positive
    /// integers, `params` an object, `accept` image settings in range. Everything else (does the workflow have the input) is the service's to say
    [[nodiscard]] std::expected<void, String> CheckArtGenPresetShape(const Json& preset);

    /**
     * @brief The preset form of the Generate panel without ImGui: the fields of one preset, its params as overrides of the workflow's
     * defaults, and the file it came from, so the panel knows what is unsaved and can revert.
     *
     * The text fields are fixed buffers the panel edits in place; after an edit the panel calls Touch(). Keys of the file the form does not
     * show are kept and written back, so loading and saving an unchanged file changes nothing.
     */
    class ArtGenPresetForm {
    public:
        static constexpr size_t kNameSize = 65;
        static constexpr size_t kTitleSize = 128;
        static constexpr size_t kPromptSize = 2048;
        static constexpr size_t kNegativeSize = 1024;
        static constexpr int64_t kFormat = 1;

        /// One override of a workflow input
        struct Param {
            String name;
            Json value;
        };

        /// Takes @p preset as the saved state: its name, source and content
        void Load(const ArtGenPreset& preset);
        /// An empty preset on @p workflow, not saved anywhere yet
        void New(StringView workflow);
        /// The same content under the name `<name>_copy`, not saved anywhere yet
        void Duplicate();
        /// Back to the content it was loaded with (nothing for a new one)
        void Revert();
        /// The content now written to `<game>/artgen/presets/<name>.json`: it is the saved state, of the game
        void MarkSaved();

        [[nodiscard]] Json ToJson() const;
        /// The content differs from what it was loaded with, or it was never saved
        [[nodiscard]] bool IsDirty() const;
        [[nodiscard]] bool HasBase() const {
            return !_baseName.empty();
        }
        /// The name it was loaded with (empty for New / Duplicate) and its source ("game", "engine")
        [[nodiscard]] const String& GetBaseName() const {
            return _baseName;
        }
        [[nodiscard]] const String& GetBaseSource() const {
            return _baseSource;
        }
        [[nodiscard]] bool IsEmpty() const {
            return _workflow.empty();
        }

        [[nodiscard]] const String& GetWorkflow() const {
            return _workflow;
        }
        /// Another workflow drops the params it does not have (and the size when it has none)
        void SetWorkflow(StringView workflow, const ArtGenWorkflow* described);

        /// The override of @p input, or null (the workflow's default applies)
        [[nodiscard]] const Json* FindParam(StringView input) const;
        void SetParam(StringView input, Json value);
        void ClearParam(StringView input);
        [[nodiscard]] const Vector<Param>& GetParams() const {
            return _params;
        }

        /// What Accept does to an image of this preset (trim, padding, max size); the defaults when the preset has no `accept`
        [[nodiscard]] const ArtGenImageSettings& GetImageAccept() const {
            return _accept;
        }
        void SetImageAccept(const ArtGenImageSettings& settings);
        /// Drops the key: the defaults apply again
        void ClearImageAccept();
        /// The file gets an `accept` key: it had one, or the settings differ from the defaults
        [[nodiscard]] bool HasImageAccept() const {
            return _acceptInFile || _accept != ArtGenImageSettings{};
        }

        /// Call after an edit of a field: the dirty check is made again only then
        void Touch() {
            ++_revision;
        }

        char name[kNameSize] = {};
        char title[kTitleSize] = {};
        char prompt[kPromptSize] = {};
        char negative[kNegativeSize] = {};
        int32_t width = 0;  // 0: no size, the workflow's own
        int32_t height = 0;

    private:
        void FromJson(const Json& data);
        /// Takes a valid `accept` object; false (nothing changed) for one that is not, which the caller keeps among the extra keys
        bool ReadAccept(const Json& value);

        String _workflow;
        Vector<Param> _params;
        Json _extra = Json::object();  // keys of the file the form does not show, a malformed `accept` among them
        ArtGenImageSettings _accept;
        bool _acceptInFile = false;         // the loaded file had a valid `accept`, or the person set one
        Json _acceptAsLoaded;               // its text, written back while the settings are as loaded, so an untouched file stays as it was
        ArtGenImageSettings _acceptLoaded;  // the settings _acceptAsLoaded reads as
        Json _base;                         // the saved content; null for a new one
        String _baseName;
        String _baseSource;
        uint64_t _revision = 0;
        mutable uint64_t _dirtyRevision = UINT64_MAX;
        mutable bool _dirty = false;
    };

}  // namespace BECore
