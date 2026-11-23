#pragma once

#include <BECore/Control/JsonFwd.h>
#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    /// What the checks of the art service said of one picture: none (the file has no verdict: an opaque picture, a sound, an old file, a shape this build does not read)
    CORE_ENUM(ArtGenVerdictKind, uint8_t, None, Passed, Failed, Unreadable)

    /// At most this many failed checks are named in a tooltip; the rest is "and N more"
    inline constexpr size_t kMaxVerdictNames = 8;
    /// The service's error text is cut to this many bytes (at a character boundary) in a tooltip
    inline constexpr size_t kMaxVerdictError = 200;

    /// The verdict of a picture as the panel shows it: the kind and the tooltip text, built once when the thumbnail loads
    struct ArtGenVerdict {
        ArtGenVerdictKind kind = ArtGenVerdictKind::None;
        String tip;  // empty for None
    };

    /**
     * @brief The verdict of the `checks` member of a provenance record (`tools/artgen/CLAUDE.md`, "The verdict").
     *
     * `passed` true is Passed, false is Failed (the tip names the failed checks in words), null is Unreadable (the tip has the
     * service's error). Anything the editor cannot read, a record without `checks` included, is None: the editor claims nothing it
     * cannot read. Pure; reads the JSON with the checked helpers only.
     */
    [[nodiscard]] ArtGenVerdict ArtGenVerdictFromProvenance(const Json& provenance);

    /// The verdict of the provenance file @p provenanceFile; an empty path, a missing file or text that is not JSON is None. Logs nothing
    /// (a gone file is the Set tab's gap, not a warning)
    [[nodiscard]] ArtGenVerdict ReadArtGenVerdict(const Path& provenanceFile);

}  // namespace BECore
