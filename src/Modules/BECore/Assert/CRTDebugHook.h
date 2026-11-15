#pragma once

namespace BECore {

    /// Prints a stack trace for every CRT debug report (heap corruption, invalid parameter, CRT assert). Call it early, before significant
    /// allocations. Does something only in a Windows Debug build; a no-op elsewhere.
    void InstallCRTDebugHooks();

}  // namespace BECore
