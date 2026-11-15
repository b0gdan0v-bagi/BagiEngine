#pragma once

#include <BECore/FuzzProbe.h>
#include <BECore/Reflection/TypedRef.h>
#include <cstdint>

namespace BECore::Fuzz {

    using FuzzProbeData::ProbeCalls;
    using FuzzProbeData::ProbeShapes;
    using FuzzProbeData::ProbeState;

    /**
     * @brief A state with every field of the probe set to a value that is not its default
     *
     * The generated reflection code (tests/BECore/FuzzProbe.h) is included in FuzzProbeAccess.cpp only, because the generated
     * file holds non-inline definitions: every other file of the library uses the probe through this header.
     */
    ProbeState MakeFilledProbe();

    /// References for a control root over the probe: the generated reflection is in this library's one file only
    TypedRef MakeProbeRef(ProbeState& state);
    TypedRef MakeProbeRef(ProbeCalls& calls);

    /// A state of the shapes (`ProbeShapes`) with every field engaged or filled
    ProbeShapes MakeFilledShapes();

    /// One number for every field of the state, the elements of the lists included: two equal states give the same digest
    uint64_t DigestProbe(const ProbeState& state);
    uint64_t DigestProbe(const FuzzProbeData::ProbeNode& node);
    uint64_t DigestProbe(const ProbeShapes& shapes);
    uint64_t DigestProbe(const ProbeCalls& calls);

}  // namespace BECore::Fuzz
