#pragma once

#include <nlohmann/json_fwd.hpp>

namespace BECore {

    /// JSON value of the control protocol. Json.h gives the complete type; read Control/CLAUDE.md, "JSON rules", before using it.
    using Json = nlohmann::json;

}  // namespace BECore
