#pragma once

namespace Core {

    template <class... Ts>
    struct overload : Ts... {
        using Ts::operator()...;
    };

}  // namespace Core

