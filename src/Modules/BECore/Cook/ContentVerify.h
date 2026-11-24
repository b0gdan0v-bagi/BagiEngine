#pragma once

#include <BECore/Cook/ContentCook.h>
#include <BECore/Cook/ContentCooker.h>
#include <BECore/File/Path.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief Checks that the content a cook wrote reads as the content it came from: the equivalence of the release path with the one
     *        developers run
     *
     * Every file of the layers of @p sourceFs is matched with the file the cook made of it in `<cookedDir>/layer<k>/config/` (the same
     * relative path; a cooked `.xml` is `<stem>.bin`), and the result is one row per file in the order the cook walks them (the report
     * of `CookContent` has the same shape, so a caller prints both alike). Per file:
     * - **Cooked** (`Cooked` here means verified): the `.bin` is read the way the game reads it (a `ConfigManager` over the cooked layer,
     *   so the scan, the stamp and the layout hash are the loader's) and the object read from it prints as the object read from the
     *   source `.xml`. A plain config is compared by the `describe` of its cooker (the type, the `ValueLinkScope` and the strict values
     *   of the cook); a `SceneManager` by `LoadSceneManager` of the source (node references expanded through every layer of
     *   @p sourceFs, as the game loads it) against `LoadSceneManagerFromBinary`; a node file by the node loader of one layer on each side
     *   (the document it gives, read as a `SceneNode`).
     * - **Copied**: a file the cook copied has the bytes of its source.
     * - **Failed**, with the reason in the row: a difference (the stem, the first differing line of the two texts, trimmed, and how many
     *   lines differ), a `.bin` that `ConfigManager` refuses (the message of the refusal), a `.bin` of a file no cooker claims, a `.bin`
     *   that has no source `.xml` (the cook did not write it), a stem that two files of one layer share (the loader reads one of them), a
     *   source file with no counterpart, a copy that differs, a layer folder that is missing.
     *
     * The unit of comparison is the text of the object (`Serialize` into an `XmlSerializer`), so state an object keeps outside its reflected
     * fields (a key a factory element reads in its own `Initialize`) is in neither text: such a type is not cookable, and the equality
     * of its texts proves nothing about that state. Every layer is checked on its own, so a file that an upper layer shadows is checked
     * too. It needs the core services (`ConfigManager` loads on `TaskManager`) and, as the cook, the types of the program that ships.
     *
     * @param registry The cookers the cook used (the engine's own is `ContentCookRegistry::GetInstance()`)
     */
    CookReport VerifyCooked(const FileSystem& sourceFs, const Path& cookedDir, const ContentCookRegistry& registry);

    /// The longest line `DescribeDifference` quotes; a longer one is cut and ends in `...`
    inline constexpr size_t kMaxQuotedLineLength = 160;

    /**
     * @brief What differs between two texts, or an empty string when they are equal
     *
     * The text is `line <n>: source '<line>', cooked '<line>' (<count> lines differ)`: the first line (counting from 1) at which the
     * two differ, a line missing from the shorter text shown as `<end of text>`, each line cut to `kMaxQuotedLineLength`, and the
     * number of lines at which they differ in total.
     */
    String DescribeDifference(StringView source, StringView cooked);

}  // namespace BECore
