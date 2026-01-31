#include "CRTDebugHook.h"

#include <BECore/Assert/StackTrace.h>

#include <cstdio>

#if defined(PLATFORM_WINDOWS) && defined(_DEBUG)
    #include <crtdbg.h>
#endif

namespace BECore {

#if defined(PLATFORM_WINDOWS) && defined(_DEBUG)

    // CRT debug report hook callback
    static int CRTDebugReportHook(int reportType, char* message, int* returnValue) {
        // Map CRT report types to readable strings
        const char* typeStr = "UNKNOWN";
        switch (reportType) {
            case _CRT_WARN:
                typeStr = "CRT_WARN";
                break;
            case _CRT_ERROR:
                typeStr = "CRT_ERROR";
                break;
            case _CRT_ASSERT:
                typeStr = "CRT_ASSERT";
                break;
        }

        // Print the CRT message
        fprintf(stderr, "\n==============================================\n");
        fprintf(stderr, "CRT Debug Report [%s]:\n", typeStr);
        if (message) {
            fprintf(stderr, "%s\n", message);
        }
        fprintf(stderr, "==============================================\n");

        // Capture stack trace
        // Skip 2 frames: this function and the CRT internal frame
        CaptureAndPrintStackTrace(2);

        // Return FALSE to pass the report to the default handler
        // (which may show a dialog or trigger a debugger break)
        // Set returnValue if you want to control CRT behavior
        if (returnValue) {
            *returnValue = 0;
        }

        return 0;
    }

    void InstallCRTDebugHooks() {
        // Install our hook (mode _CRTDBG_MODE_WNDW means it will be called
        // before showing the debug window)
        _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, CRTDebugReportHook);

        // Enable all CRT debug checks
        int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
        
        // Enable memory leak detection on exit
        flags |= _CRTDBG_LEAK_CHECK_DF;
        
        // Check for memory leaks on every allocation/free (performance impact)
        // Uncomment for aggressive debugging:
        // flags |= _CRTDBG_CHECK_ALWAYS_DF;
        
        // Check memory integrity on every allocation/free
        flags |= _CRTDBG_ALLOC_MEM_DF;
        
        _CrtSetDbgFlag(flags);

        fprintf(stderr, "[CRTDebugHook] CRT debug hooks installed (Debug build)\n");
        fflush(stderr);
    }

#else

    // No-op for Release builds or non-Windows platforms
    void InstallCRTDebugHooks() {
        // CRT debug hooks are only available in Windows Debug builds
        #if defined(PLATFORM_WINDOWS)
            fprintf(stderr, "[CRTDebugHook] Skipped (Release build)\n");
        #else
            fprintf(stderr, "[CRTDebugHook] Skipped (non-Windows platform)\n");
        #endif
        fflush(stderr);
    }

#endif

}  // namespace BECore
