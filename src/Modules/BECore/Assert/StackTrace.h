#pragma once

/**
 * @file StackTrace.h
 * @brief Stack trace capture and formatting utilities
 * 
 * Provides cross-platform stack trace capture for debugging assertion
 * failures and memory errors.
 */

namespace BECore {

    /**
     * @brief Capture current stack trace and print to stderr
     * 
     * Captures the call stack at the point of invocation and outputs
     * formatted stack frames with symbol names, file names, and line numbers
     * (when debug symbols are available).
     * 
     * @param skipFrames Number of stack frames to skip (to hide implementation details)
     * 
     * @note On Windows, requires DbgHelp.dll and PDB files for symbol resolution
     * @note On Unix, requires -rdynamic linker flag for symbol names
     */
    void CaptureAndPrintStackTrace(int skipFrames = 0);

}  // namespace BECore
