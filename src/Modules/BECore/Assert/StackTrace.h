#pragma once

namespace BECore {

    /**
     * @brief Prints the current call stack to stderr, with symbol, file and line where the symbols allow
     *
     * @param skipFrames frames to leave out, the innermost first
     *
     * @note Windows needs DbgHelp.dll and the PDB files, Unix needs the -rdynamic linker flag for names
     */
    void CaptureAndPrintStackTrace(int skipFrames = 0);

}  // namespace BECore
