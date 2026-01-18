#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/Logger/LogEvent.h>

#include <fstream>
#include <mutex>
#include <string>

namespace BECore {

    class XmlNode;

    /**
     * @brief Log sink that outputs to a file
     * 
     * Thread-safe file logging with optional append mode.
     */
    class FileSink : public ILogSink {
        BE_CLASS(FileSink)

    public:
        FileSink() = default;
        ~FileSink() override;

        /**
         * @brief Initialize the file sink
         */
        void Initialize() override;

        /**
         * @brief Configure the file sink from XML node
         * 
         * @param node XML node containing sink configuration
         */
        void Configure(const XmlNode& node) override;

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
         */
        void Write(LogLevel level, eastl::string_view message) override;

    private:
        /**
         * @brief Handle LogEvent
         */
        void OnLogEvent(const LogEvent& event);

        /**
         * @brief Flush file output
         */
        void Flush() override;

        /**
         * @brief Set the filename for logging
         * @param filename Path to the log file
         */
        void SetFilename(const std::string& filename) { _filename = filename; }

        /**
         * @brief Get the filename
         * @return Path to the log file
         */
        const std::string& GetFilename() const { return _filename; }

        /**
         * @brief Set append mode
         * @param append If true, append to existing file; otherwise truncate
         */
        void SetAppend(bool append) { _append = append; }

        /**
         * @brief Check if append mode is enabled
         * @return true if append mode is enabled
         */
        bool IsAppend() const { return _append; }

    private:
        std::string _filename = "engine.log";
        bool _append = false;
        std::ofstream _file;
        std::mutex _mutex;
        bool _initialized = false;
    };

}  // namespace BECore
