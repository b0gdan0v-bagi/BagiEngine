#pragma once

#include <BECore/Logger/LogLevel.h>
#include <BECore/RefCounted/RefCounted.h>

#include <EASTL/string_view.h>

namespace BECore {

    class XmlNode;

    /**
     * @brief Interface for log output destinations
     * 
     * Implement this interface to create custom log sinks
     * (e.g., console, file, network, etc.)
     * 
     * Sinks are managed by LoggerManager and sorted by priority.
     * Lower priority values are processed first.
     * 
     * @example
     * class MyLogSink : public ILogSink {
     * public:
     *     void Initialize() override {
     *         // Setup sink
     *     }
     * 
     *     void Write(LogLevel level, eastl::string_view message) override {
     *         // Write log message
     *     }
     * 
     *     void Flush() override {
     *         // Flush output
     *     }
     * };
     */
    class ILogSink : public RefCounted {
    public:
        ~ILogSink() override = default;

        /**
         * @brief Initialize the sink
         * 
         * Called during engine initialization.
         * Should set up any necessary state.
         */
        virtual void Initialize() = 0;

        /**
         * @brief Configure the sink from XML node
         * 
         * Called after sink creation to configure type-specific options.
         * Base implementation does nothing - override in derived classes.
         * 
         * @param node XML node containing sink configuration
         */
        virtual void Configure(const XmlNode& node) {}

        /**
         * @brief Write a log message to the sink
         * 
         * @param level Severity level of the message
         * @param message The formatted log message
         */
        virtual void Write(LogLevel level, eastl::string_view message) = 0;

        /**
         * @brief Flush any buffered output
         * 
         * Called to ensure all pending messages are written.
         */
        virtual void Flush() = 0;

        /**
         * @brief Set minimum log level for this sink
         * 
         * Messages below this level will be ignored.
         * 
         * @param level Minimum level to log
         */
        void SetMinLevel(LogLevel level) { _minLevel = level; }

        /**
         * @brief Get minimum log level for this sink
         * 
         * @return Current minimum log level
         */
        LogLevel GetMinLevel() const { return _minLevel; }

        /**
         * @brief Check if a message at given level should be logged
         * 
         * @param level Level to check
         * @return true if the message should be logged
         */
        bool ShouldLog(LogLevel level) const {
            return static_cast<int>(level) >= static_cast<int>(_minLevel);
        }

        /**
         * @brief Get sink priority for sorting
         * 
         * Lower values are processed first. Use this to control
         * the order in which sinks receive log messages.
         * 
         * @return Priority value (lower = higher priority)
         */
        virtual int GetPriority() const { return _priority; }

        /**
         * @brief Set sink priority
         * @param priority Priority value (lower = higher priority)
         */
        void SetPriority(int priority) { _priority = priority; }

    protected:
        ILogSink() = default;

        LogLevel _minLevel = LogLevel::Debug;
        int _priority = 0;
    };

}  // namespace BECore
