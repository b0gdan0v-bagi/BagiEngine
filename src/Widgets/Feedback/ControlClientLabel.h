#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>

namespace BECore {

    class ControlClient;

    /// Room for the longest label ControlClientLabel() writes itself
    inline constexpr size_t kControlClientLabelSize = 32;

    /**
     * @brief What the editor calls @p client: the name it has now, `client N` for one with no name, `engine` for no client
     *
     * The view is of the client's own name, of a literal or of @p buffer, and is good as long as the one it came from. A name is client text: draw it
     * unformatted. Allocates nothing.
     */
    [[nodiscard]] StringView ControlClientLabel(const ControlClient* client, Array<char, kControlClientLabelSize>& buffer);

}  // namespace BECore
