#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <BECore/Widgets/PanelAssets/PanelAsset.h>

namespace BECore {

    class IDeserializer;

    /// One problem of a panel asset: where it is (an element id, or a path such as `elements[2].columns[0]`) and a short line that says what is wrong
    struct PanelIssue {
        String where;
        String message;
    };

    /**
     * @brief What the editor knows that the asset names: the checks of the names a file cannot verify itself
     *
     * A predicate is referenced, not copied: the callable must outlive the load. An empty one skips that check, so the folder includes no
     * editor header (the editor passes its command table and its model factory).
     */
    struct PanelValidationContext {
        Optional<FunctionRef<bool(StringView)>> isKnownCommand;
        Optional<FunctionRef<bool(StringView)>> isKnownModel;
    };

    /**
     * @brief The asset and everything wrong with it
     *
     * `issues.empty()` is the only meaning of "valid". An asset with issues is still safe to hold and to walk (the tree is bounded by
     * the reader's limits, and a breach of `PanelAssetLimits` is reported as an issue, never trimmed; no pointer in it is null that the
     * format allows to be set); `asset` is null only for a text that does not
     * parse. Issues are short lines with no control character, so they can be shown in the panel and written to the log.
     */
    struct PanelLoadResult {
        IntrusivePtr<PanelAsset> asset;
        Vector<PanelIssue> issues;
    };

    /// Reads a panel file's text. No input asserts or crashes; the same text gives the same result (no state is kept)
    [[nodiscard]] PanelLoadResult LoadPanelAsset(StringView xmlText, const PanelValidationContext& context = {});

    /**
     * @brief Reads the asset from @p deserializer, positioned at the root of the document (`ConfigManager::CreateDeserializer`)
     *
     * Switches the deserializer to `DiagnosticsMode::Error` and strict values before the read and ends it with `FinishLoad()`, so an
     * unknown attribute, a bad number and an unknown `type` are issues (with `did you mean`), as are the deserializer's own errors.
     */
    [[nodiscard]] PanelLoadResult LoadPanelAsset(IDeserializer& deserializer, const PanelValidationContext& context = {});

    /// The checks that need the whole tree, for an asset that was not read from a file; appends to @p issues
    void ValidatePanelAsset(const PanelAsset& asset, const PanelValidationContext& context, Vector<PanelIssue>& issues);

}  // namespace BECore
