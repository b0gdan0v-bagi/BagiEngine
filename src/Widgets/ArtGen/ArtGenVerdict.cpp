#include "ArtGenVerdict.h"

#include <BECore/Control/ControlParams.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/File/File.h>
#include <Widgets/ArtGen/ArtGenFileText.h>

namespace BECore {

    namespace {

        struct CheckMeaning {
            StringView name;
            StringView meaning;
        };

        // Sorted by name; the names are `artgen_checks.CHECK_NAMES`. A name that is not here is shown as it is
        constexpr Array<CheckMeaning, 8> kCheckMeanings{{
            {"background", "the background was not removed"},
            {"bodies", "several objects side by side"},
            {"edge", "the object touches the edge of the frame"},
            {"empty", "nothing is drawn"},
            {"halo", "a halo of half-transparent pixels"},
            {"offCentre", "the object is far off centre"},
            {"pieces", "several separate pieces"},
            {"small", "the object is too small"},
        }};

        constexpr StringView kPassedTip = "Checks passed";
        constexpr StringView kFailedTip = "Checks failed";
        constexpr StringView kUnreadableTip = "Checks could not read the picture";
        constexpr uint8_t kUtf8ContinuationMask = 0xC0;
        constexpr uint8_t kUtf8ContinuationBits = 0x80;

        static_assert(std::is_sorted(kCheckMeanings.begin(), kCheckMeanings.end(), [](const CheckMeaning& a, const CheckMeaning& b) { return a.name < b.name; }));

        StringView MeaningOf(StringView name) {
            const auto it = std::lower_bound(kCheckMeanings.begin(), kCheckMeanings.end(), name, [](const CheckMeaning& row, StringView key) { return row.name < key; });
            return (it != kCheckMeanings.end() && it->name == name) ? it->meaning : StringView();
        }

        /// The first @p limit bytes of @p text, moved back to the start of a character when the cut would split one
        StringView CutAtCharacter(StringView text, size_t limit) {
            if (text.size() <= limit) {
                return text;
            }
            size_t end = limit;
            while (end > 0 && (static_cast<uint8_t>(text[end]) & kUtf8ContinuationMask) == kUtf8ContinuationBits) {
                --end;
            }
            return text.substr(0, end);
        }

        void Append(String& out, StringView text) {
            out.append(text.data(), text.size());
        }

        Vector<String> FailedNamesOf(const Optional<JsonView>& failed) {
            Vector<String> names;
            if (!failed || !failed->IsArray()) {
                return names;
            }
            for (const JsonView item : failed->Items()) {
                String name;
                if (item.ReadText(name)) {
                    names.push_back(eastl::move(name));
                }
            }
            return names;
        }

        ArtGenVerdict FailedVerdict(const Optional<JsonView>& failed) {
            const Vector<String> names = FailedNamesOf(failed);
            ArtGenVerdict verdict{ArtGenVerdictKind::Failed, String(kFailedTip)};
            if (names.empty()) {
                return verdict;
            }
            Append(verdict.tip, ": ");
            const size_t shown = std::min(names.size(), kMaxVerdictNames);
            for (size_t i = 0; i < shown; ++i) {
                if (i > 0) {
                    Append(verdict.tip, "; ");
                }
                verdict.tip += names[i];
                if (const StringView meaning = MeaningOf(names[i]); !meaning.empty()) {
                    Append(verdict.tip, " (");
                    Append(verdict.tip, meaning);
                    Append(verdict.tip, ")");
                }
            }
            if (names.size() > shown) {
                verdict.tip += Format("; and {} more", names.size() - shown);
            }
            return verdict;
        }

        ArtGenVerdict UnreadableVerdict(JsonView checks) {
            ArtGenVerdict verdict{ArtGenVerdictKind::Unreadable, String(kUnreadableTip)};
            if (const Optional<String> error = GetParamString(checks, "error"); error && !error->empty()) {
                Append(verdict.tip, ": ");
                Append(verdict.tip, CutAtCharacter(StringView(error->data(), error->size()), kMaxVerdictError));
            }
            return verdict;
        }

    }  // namespace

    ArtGenVerdict ArtGenVerdictFromProvenance(JsonView provenance) {
        const Optional<JsonView> checks = FindParam(provenance, "checks");
        if (!checks || !checks->IsObject()) {
            return {};
        }
        const Optional<JsonView> passed = FindParam(*checks, "passed");
        if (!passed) {
            return {};
        }
        if (passed->IsNull()) {
            return UnreadableVerdict(*checks);
        }
        const Optional<bool> value = GetParamBool(*checks, "passed");
        if (!value) {
            return {};
        }
        if (*value) {
            return {ArtGenVerdictKind::Passed, String(kPassedTip)};
        }
        return FailedVerdict(FindParam(*checks, "failed"));
    }

    ArtGenVerdict ReadArtGenVerdict(const Path& provenanceFile) {
        if (provenanceFile.Empty()) {
            return {};
        }
        const auto text = ReadTextFile(provenanceFile);
        if (!text) {
            return {};
        }
        JsonTape tape;
        if (!tape.Parse(WithoutUtf8Bom(StringView(text->data(), text->size())))) {
            return {};
        }
        return ArtGenVerdictFromProvenance(JsonView(tape, 0));
    }

}  // namespace BECore
