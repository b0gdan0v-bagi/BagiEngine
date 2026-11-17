#pragma once

#include <BECore/PoolString/StaticPoolString.h>

namespace BECore {

    /// The configs the engine reads by name from more than one place.
    struct ConfigNames {
        static constexpr auto logger = "LoggerConfig"_intern;
        static constexpr auto text = "TextConfig"_intern;
        static constexpr auto tests = "TestsConfig"_intern;
        static constexpr auto assertHandlers = "AssertHandlersConfig"_intern;
    };

}  // namespace BECore
