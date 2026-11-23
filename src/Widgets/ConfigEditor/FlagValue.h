#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>

namespace BECore {

    /// The tokens of a `|`-joined value: trimmed, non-empty, in value order.
    Vector<String> SplitFlagTokens(StringView value);

    /// The tokens of `value` that are not in `options`, in value order and without repeats.
    Vector<String> UnknownFlagTokens(StringView value, const Vector<String>& options);

    /// Whether `value` has `token` as one of its trimmed tokens (an exact match, not a prefix). Allocates nothing.
    [[nodiscard]] bool HasFlagToken(StringView value, StringView token);

    /// Calls `fn` for each token of `value` that is not in `options`, in value order and once per distinct token, as
    /// UnknownFlagTokens lists them but without building the list. The views point into `value`.
    void ForEachUnknownFlagToken(StringView value, const Vector<String>& options, FunctionRef<void(StringView)> fn);

    /// The tokens joined with `|`.
    String JoinFlagTokens(const Vector<String>& tokens);

}  // namespace BECore
