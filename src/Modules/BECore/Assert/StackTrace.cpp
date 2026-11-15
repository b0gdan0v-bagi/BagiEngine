#include "StackTrace.h"

#include <cstdio>
#include <cstdlib>

#if defined(PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS)
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#endif

namespace BECore {

#if defined(PLATFORM_WINDOWS)

    void CaptureAndPrintStackTrace(int skipFrames) {
        constexpr int MaxFrames = 62;
        void* stack[MaxFrames];

        static bool symbolsInitialized = false;
        if (!symbolsInitialized) {
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
            if (SymInitialize(GetCurrentProcess(), nullptr, TRUE)) {
                symbolsInitialized = true;
            }
        }

        USHORT frameCount = CaptureStackBackTrace(skipFrames + 1, MaxFrames, stack, nullptr);

        fprintf(stderr, "\n=== Stack Trace ===\n");

        if (!symbolsInitialized) {
            fprintf(stderr, "Warning: Symbol handler not initialized. Stack trace will show addresses only.\n");
        }

        HANDLE process = GetCurrentProcess();

        constexpr int SymbolInfoSize = sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR);
        char symbolBuffer[SymbolInfoSize];
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->MaxNameLen = MAX_SYM_NAME;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

        IMAGEHLP_LINE64 line;
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

        for (USHORT i = 0; i < frameCount; ++i) {
            DWORD64 address = reinterpret_cast<DWORD64>(stack[i]);

            fprintf(stderr, "[%2d] 0x%016llX", i, address);

            if (symbolsInitialized) {
                DWORD64 displacement = 0;
                if (SymFromAddr(process, address, &displacement, symbol)) {
                    fprintf(stderr, " %s", symbol->Name);

                    DWORD lineDisplacement = 0;
                    if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line)) {
                        fprintf(stderr, " (%s:%lu)", line.FileName, line.LineNumber);
                    }
                } else {
                    fprintf(stderr, " <unknown>");
                }
            }

            fprintf(stderr, "\n");
        }

        fprintf(stderr, "===================\n\n");
        fflush(stderr);
    }

#elif defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS)

    void CaptureAndPrintStackTrace(int skipFrames) {
        constexpr int MaxFrames = 64;
        void* stack[MaxFrames];

        int frameCount = backtrace(stack, MaxFrames);

        fprintf(stderr, "\n=== Stack Trace ===\n");

        char** symbols = backtrace_symbols(stack, frameCount);

        if (symbols == nullptr) {
            fprintf(stderr, "Failed to get stack trace symbols\n");
            fprintf(stderr, "===================\n\n");
            return;
        }

        // The +1 skips this function itself.
        int startFrame = skipFrames + 1;
        if (startFrame >= frameCount) {
            startFrame = frameCount - 1;
        }

        for (int i = startFrame; i < frameCount; ++i) {
            fprintf(stderr, "[%2d] ", i - startFrame);

            char* mangled_name = nullptr;
            char* offset_begin = nullptr;
            char* offset_end = nullptr;

            // glibc frame text is "image(mangled+offset) [address]"; any other shape is printed as it is.
            for (char* p = symbols[i]; *p; ++p) {
                if (*p == '(') {
                    mangled_name = p;
                } else if (*p == '+') {
                    offset_begin = p;
                } else if (*p == ')') {
                    offset_end = p;
                    break;
                }
            }

            if (mangled_name && offset_begin && offset_end && mangled_name < offset_begin) {
                *mangled_name++ = '\0';
                *offset_begin++ = '\0';
                *offset_end = '\0';

                int status = 0;
                char* demangled = abi::__cxa_demangle(mangled_name, nullptr, nullptr, &status);

                if (status == 0 && demangled) {
                    fprintf(stderr, "%s: %s+%s\n", symbols[i], demangled, offset_begin);
                    free(demangled);
                } else {
                    fprintf(stderr, "%s: %s+%s\n", symbols[i], mangled_name, offset_begin);
                }
            } else {
                fprintf(stderr, "%s\n", symbols[i]);
            }
        }

        free(symbols);
        fprintf(stderr, "===================\n\n");
        fflush(stderr);
    }

#else

    void CaptureAndPrintStackTrace(int skipFrames) {
        fprintf(stderr, "\n=== Stack Trace ===\n");
        fprintf(stderr, "Stack trace not supported on this platform\n");
        fprintf(stderr, "===================\n\n");
        fflush(stderr);
    }

#endif

}  // namespace BECore
