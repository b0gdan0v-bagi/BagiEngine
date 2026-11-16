#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    /// The tokens of a `|`-joined value: trimmed, non-empty, in value order.
    Vector<String> SplitFlagTokens(StringView value);

    /// The tokens of `value` that are not in `options`, in value order and without repeats.
    Vector<String> UnknownFlagTokens(StringView value, const Vector<String>& options);

    /// The tokens joined with `|`.
    String JoinFlagTokens(const Vector<String>& tokens);

}  // namespace BECore
