#pragma once

#include <Core/Logger/ILogSink.h>

#include <fstream>
#include <mutex>
#include <string>

namespace Core {

    /**
     * @brief Log sink that outputs to a file
     * 
     * Thread-safe file logging with optional append mode.
     */
    class FileSink : public ILogSink {
    public:
        /**
         * @brief Construct a FileSink
         * 
         * @param filename Path to the log file
         * @param append If true, append to existing file; otherwise truncate
         */
        explicit FileSink(const std::string& filename, bool append = false);
        
        ~FileSink() override;

        /**
         * @brief Check if the file was opened successfully
         * 
         * @return true if file is open and ready for writing
         */
        bool IsOpen() const { return _file.is_open(); }

        /**
         * @brief Write a log message to file
         * 
         * @param level Severity level of the message
         * @param message The formatted log message
         * @param file Source file (can be nullptr)
         * @param line Source line (0 if not available)
         */
        void Write(LogLevel level, const char* message, const char* file, int line) override;

        /**
         * @brief Flush file output
         */
        void Flush() override;

    private:
        std::ofstream _file;
        std::mutex _mutex;
    };

}  // namespace Core
