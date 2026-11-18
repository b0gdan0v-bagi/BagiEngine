#pragma once

#include <BECore/Control/Json.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    /// The reference modes the panel offers: the keys of `MODES` in tools/artgen/artgen_reference.py (the service owns what each one does)
    inline constexpr Array<StringView, 1> kArtGenReferenceModes = {"init"};

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

    /// The reference to the accepted sprite @p name (`assets/artgen/<name>.png`, init, the default weight); the name is not checked here
    [[nodiscard]] ArtGenReference ArtGenReferenceOfSprite(StringView name);

}  // namespace BECore
