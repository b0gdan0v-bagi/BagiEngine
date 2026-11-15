#include "CRTDebugHook.h"

#include <BECore/Assert/StackTrace.h>
#include <cstdio>

#if defined(PLATFORM_WINDOWS) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace BECore {

#if defined(PLATFORM_WINDOWS) && defined(_DEBUG)

    static int CRTDebugReportHook(int reportType, char* message, int* returnValue) {
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

        fprintf(stderr, "\n==============================================\n");
        fprintf(stderr, "CRT Debug Report [%s]:\n", typeStr);
        if (message) {
            fprintf(stderr, "%s\n", message);
        }
        fprintf(stderr, "==============================================\n");

        // Skips this function and the CRT's own frame.
        CaptureAndPrintStackTrace(2);

        // Returning 0 passes the report on to the default handler (a dialog or a debugger break).
        if (returnValue) {
            *returnValue = 0;
        }

        return 0;
    }

    void InstallCRTDebugHooks() {
        // The hook runs before the debug window is shown.
        _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, CRTDebugReportHook);

        int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);

        flags |= _CRTDBG_LEAK_CHECK_DF;

        flags |= _CRTDBG_ALLOC_MEM_DF;

        _CrtSetDbgFlag(flags);

        fprintf(stderr, "[CRTDebugHook] CRT debug hooks installed (Debug build)\n");
        fflush(stderr);
    }

#else

    void InstallCRTDebugHooks() {
// A Windows Release build says so on stderr; every other platform prints nothing, because the browser console shows stderr as an error
// and there is nothing to report there.
#if defined(PLATFORM_WINDOWS)
        fprintf(stderr, "[CRTDebugHook] Skipped (Release build)\n");
        fflush(stderr);
#endif
    }

#endif

}  // namespace BECore
