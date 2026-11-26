#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// Where the entry of a pooled string came from
    CORE_ENUM(PoolStringOrigin, uint8_t,
              Static,   ///< A registered ready entry, in no block of the pool (a code constant lies in the program image)
              Runtime,  ///< Copied by `Intern` or `InternBatch`
              Adopted)  ///< An entry of an adopted block

    CORE_ENUM(PoolBlockKind, uint8_t,
              Runtime,    ///< Written by the pool itself: `Intern`, `InternBatch`, `Reserve`
              Allocated,  ///< Given out by `AllocateBlock` and not adopted, yet or ever
              Adopted)    ///< Given out by `AllocateBlock` and adopted

    /// `PoolStringRow::block` of a string that lies in no block of the pool
    inline constexpr uint32_t kNoPoolBlock = UINT32_MAX;

    struct PoolStringRow {
        PoolString text;
        PoolStringOrigin origin = PoolStringOrigin::Static;
        uint32_t block = kNoPoolBlock;  ///< Index into `PoolStringSnapshot::blocks`
    };

    struct PoolBlockRow {
        const char* data = nullptr;  ///< Its first byte
        PoolBlockKind kind = PoolBlockKind::Runtime;
        size_t capacity = 0;
        size_t used = 0;         ///< Bytes taken by entries from the start, dead ones included; 0 for an allocated block
        size_t deadStrings = 0;  ///< Entries of an adopted block that were not linked
        size_t deadBytes = 0;    ///< Their aligned sizes
        String source;           ///< `PoolAdoptOptions::sourceName` of an adopted block
    };

    /// The strings and blocks of a pool at one moment (`Details::PoolStringStorage::TakeSnapshot`). The strings are the
    /// entries of the table, so a dead entry of an adopted block is no row. The strings stay valid for the life of the
    /// pool, which never frees; refilling the same object reuses its memory.
    struct PoolStringSnapshot {
        PoolStringStats stats;
        Vector<PoolStringRow> strings;  ///< In no particular order
        Vector<PoolBlockRow> blocks;    ///< In the order the pool took them
    };

}  // namespace BECore
