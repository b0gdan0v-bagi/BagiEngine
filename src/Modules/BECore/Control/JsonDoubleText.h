#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    /// The size of the buffer WriteDoubleText needs; the longest text is 25 characters
    constexpr size_t kMaxDoubleTextBytes = 64;

    /// Writes the text of the finite @p value into @p buffer (not terminated) and returns its length.
    /// The text is that of Grisu2 (the routine and its notice are in JsonDoubleText.cpp): it is not always the shortest
    /// text of `std::to_chars`, and every control answer, replay file, room message and ArtGen file holds these bytes. `-0` is `-0.0`, a whole
    /// number has `.0`, the exponent form is `1e+21` / `5e-324`. The caller keeps the check for a value that is not finite.
    [[nodiscard]] size_t WriteDoubleText(char* buffer, double value);

}  // namespace BECore
