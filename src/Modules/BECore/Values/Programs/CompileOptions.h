#pragma once

#include <BECore/Values/ValueTables.h>

namespace BECore {

    /// What a caller of `CompileExpression` / `CompileValue` may change about one compile
    struct CompileOptions {
        /// The provider of `ByLevel('Name', i)`: read while the formula compiles, never kept. Null: a formula that names a table is a load error
        const IValueTables* tables = nullptr;
        /// Whether the peephole pass fuses instructions of the built code; the fused program computes what the unfused one does, bit for bit. Off for tests and benchmarks
        bool fuse = true;
    };

}  // namespace BECore
