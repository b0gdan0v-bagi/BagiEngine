#pragma once

#include <BECore/Control/Json.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    /// What the panel knows of one reference mode: the key of `MODES` in tools/artgen/artgen_reference.py, its `default_weight`, the range of
    /// its slider (inside the open interval `weight_range` of the service, which owns what the mode does) and what it does, for a tooltip.
    /// A Python test holds the modes and defaults to `MODES`, so a mode is added there and here together
    struct ArtGenReferenceModeRow {
        StringView mode;
        double defaultWeight;
        float minWeight;
        float maxWeight;
        const char* tip;
    };

    /// The modes the panel offers, in the combo's order; the first is the fallback for a mode the table does not know
    inline constexpr Array<ArtGenReferenceModeRow, 2> kArtGenReferenceModes = {{
        {"init", 0.4, 0.05f, 0.95f, "Redraw this picture: the weight is how much of it is kept"},
        {"style", 0.8, 0.1f, 1.5f, "Draw something new in the style of this picture: the weight is how strongly the style is applied"},
    }};

    /// The reference picture of a run: `{"mode", "image", "weight"}` of tools/artgen/CLAUDE.md, "Presets". Only the shape is held here;
    /// whether the mode exists, the weight is in its range and the file is a picture is the service's to check, which fails the run
    struct ArtGenReference {
        String mode;
        String image;             // a `/`-separated path: relative to the game root, or absolute
        Optional<double> weight;  // none: the mode's default

        bool operator==(const ArtGenReference& other) const = default;
    };

    /// `{"mode", "image"}` and `weight` when there is one
    [[nodiscard]] Json ArtGenReferenceToJson(const ArtGenReference& reference);

    /// The reference of a JSON value, or the error that names the key (`reference.mode`, ...). A null `weight` counts as absent
    [[nodiscard]] std::expected<ArtGenReference, String> ReadArtGenReference(const Json& value);

    /// The reference to the file @p realPath (init, the default weight): its path relative to @p gameRoot with `/` when it lies inside it
    /// (a game folder moves, a relative path stays right), else the absolute path with `/`. Backslashes of a Windows path are turned
    [[nodiscard]] ArtGenReference ArtGenReferenceOfFile(StringView gameRoot, StringView realPath);

    /// The reference to a variant's file @p realPath (as `ArtGenReferenceOfFile`), or none for a sound or a variant with no file: only a picture can be one
    [[nodiscard]] Optional<ArtGenReference> ArtGenReferenceOfVariant(StringView gameRoot, StringView kind, StringView realPath);

    /// The reference to the accepted sprite @p name (`assets/artgen/<name>.png`, init, the default weight); the name is not checked here
    [[nodiscard]] ArtGenReference ArtGenReferenceOfSprite(StringView name);

    /// The row of @p mode in kArtGenReferenceModes; the first row for a mode the table does not know
    [[nodiscard]] const ArtGenReferenceModeRow& ArtGenReferenceModeOf(StringView mode);

    /// The weight of a mode when the reference names none (its row's `defaultWeight`)
    [[nodiscard]] double ArtGenReferenceDefaultWeight(StringView mode);

    /// The real path of a reference's picture: @p image as it is when absolute (a `/` or a drive letter first), else under @p gameRoot
    [[nodiscard]] String ArtGenReferenceFilePath(StringView gameRoot, StringView image);

    /// The file name at the end of a reference's path, what the slot shows
    [[nodiscard]] StringView ArtGenReferenceFileName(StringView image);

}  // namespace BECore
