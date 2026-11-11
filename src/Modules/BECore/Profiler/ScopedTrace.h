#pragma once

namespace BECore {

    class IProfilerBackend;

    /**
     * @brief A Chrome trace of one scope, for tools without Application (the bot arena, a benchmark).
     *
     * The constructor installs the Chrome trace recorder and marks the start of the trace; the destructor writes what was recorded
     * since to @p path and puts the previous backend back. Threads that record into it must have ended (or stopped recording) by
     * then for their zones to be complete. In a build without the profiler the macros record nothing and the file holds no event.
     * Errors are logged (LOG_ERROR) and kept in GetError().
     */
    class ScopedTrace {
    public:
        explicit ScopedTrace(eastl::string_view path);
        ~ScopedTrace();

        ScopedTrace(const ScopedTrace&) = delete;
        ScopedTrace& operator=(const ScopedTrace&) = delete;

        /// Writes the trace now (the destructor writes it again); false with the reason in GetError() when it cannot
        bool Write();

        [[nodiscard]] const eastl::string& GetError() const {
            return _error;
        }

    private:
        eastl::string _path;
        eastl::string _error;
        IProfilerBackend* _previous = nullptr;
    };

}  // namespace BECore
