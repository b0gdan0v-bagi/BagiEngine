#pragma once

/**
 * @file CRTDebugHook.h
 * @brief CRT debug report hook for capturing memory errors
 * 
 * Provides hooks into the Windows CRT debug heap to intercept
 * memory corruption, invalid parameters, and other CRT assertions.
 * Only active in Debug builds.
 */

namespace BECore {

    /**
     * @brief Install CRT debug report hooks
     * 
     * Sets up hooks to intercept CRT debug reports (heap corruption,
     * invalid parameters, assertions, etc.) and capture stack traces
     * when they occur.
     * 
     * Should be called early during initialization, before significant
     * memory allocations occur.
     * 
     * @note Only functional in Debug builds (when _DEBUG is defined)
     * @note Windows-only (no-op on other platforms)
     */
    void InstallCRTDebugHooks();

}  // namespace BECore
