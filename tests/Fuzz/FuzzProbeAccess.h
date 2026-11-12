#pragma once

#include <BECore/FuzzProbe.h>

namespace BECore::Fuzz {

    using FuzzProbeData::ProbeState;

    /**
     * @brief A state with every field of the probe set to a value that is not its default
     *
     * The generated reflection code (tests/BECore/FuzzProbe.h) is included in FuzzProbeAccess.cpp only, because the generated
     * file holds non-inline definitions: every other file of the library uses the probe through this header.
     */
    ProbeState MakeFilledProbe();

    /// One number for every field of the state, the elements of the lists included: two equal states give the same digest
    uint64_t DigestProbe(const ProbeState& state);

}  // namespace BECore::Fuzz
