#pragma once

#include <BECore/PoolString/PoolStringSnapshot.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    CORE_ENUM(PoolStringSortKey, uint8_t,
              Text,    ///< By the bytes of the text
              Length,  ///< By its length in bytes
              Hash,
              Source)  ///< By origin, then by the source name of the block

    /// Which strings of a snapshot `PoolStringAnalysis::Search` gives, and in what order
    struct PoolStringQuery {
        StringView text;                    ///< A part of the string, ASCII case ignored; empty: every string
        Optional<PoolStringOrigin> origin;  ///< Nullopt: every origin
        StringView blockSource;             ///< The whole `PoolBlockRow::source` of the string's block; empty: every string
        PoolStringSortKey sortBy = PoolStringSortKey::Text;
        bool descending = false;  ///< Turns the order of the key round; equal keys stay in ascending order of the text
    };

}  // namespace BECore

/// Questions about a snapshot of a pool. They walk it whole and allocate: not for every frame.
namespace BECore::PoolStringAnalysis {

    /// Two rows of a snapshot, as indices into `PoolStringSnapshot::strings`
    struct RowPair {
        uint32_t first = 0;
        uint32_t second = 0;
    };

    /// Refills `out` with the different texts that share a hash: for each such hash, one text paired with each other one
    void FindHashCollisions(const PoolStringSnapshot& snapshot, Vector<RowPair>& out);

    /// Refills `out` with the texts held by more than one entry, which a pointer compare then takes for different strings:
    /// for each such text, one row paired with each other one. The texts are compared, not the hashes.
    void FindDuplicateTexts(const PoolStringSnapshot& snapshot, Vector<RowPair>& out);

    /// Refills `out` with the indices of the rows of `snapshot.strings` that `query` selects, in its order
    void Search(const PoolStringSnapshot& snapshot, const PoolStringQuery& query, Vector<uint32_t>& out);

}  // namespace BECore::PoolStringAnalysis
