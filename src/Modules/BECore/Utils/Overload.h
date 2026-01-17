#pragma once

namespace BECore {

    template <class... Ts>
    struct overload : Ts... {
        using Ts::operator()...;
    };

}  // namespace BECore

