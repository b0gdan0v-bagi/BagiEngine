#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Utils/Singleton.h>
#include <cstdint>

namespace BECore {

    /// An operation of the editor that spans frames, as its source answers it
    struct EditorProgress {
        StringView label;          ///< what runs; good until the source is asked again
        Optional<float> fraction;  ///< 0 to 1; none when how far it got is unknown
    };

    /// What runs now: the operation of the first source that has one, and how many more have one
    struct EditorProgressSummary {
        Optional<EditorProgress> first;
        uint32_t others = 0;
    };

    /**
     * @brief The operation of its owner that runs now, or nothing
     *
     * Asked once per drawn frame, whether or not a panel of the owner is open, so it takes what became of the operation itself. It allocates nothing
     * while nothing changed, and registers and unregisters no source from within the call.
     */
    using EditorProgressSource = MoveOnlyFunction<Optional<EditorProgress>()>;

    class EditorProgressRegistry;

    /// The registration of a source; the source is unregistered when the handle ends. Move-only
    class EditorProgressHandle {
    public:
        EditorProgressHandle() = default;
        ~EditorProgressHandle();

        EditorProgressHandle(const EditorProgressHandle&) = delete;
        EditorProgressHandle& operator=(const EditorProgressHandle&) = delete;
        EditorProgressHandle(EditorProgressHandle&& other) noexcept;
        EditorProgressHandle& operator=(EditorProgressHandle&& other) noexcept;

        /// Unregisters now; nothing for an empty handle
        void Reset();

    private:
        friend class EditorProgressRegistry;
        explicit EditorProgressHandle(uint64_t token) : _token(token) {}

        uint64_t _token = 0;
    };

    /// The sources of the progress strip of the status bar. Main thread only, so it takes no lock
    class EditorProgressRegistry : public Singleton<EditorProgressRegistry> {
    public:
        EditorProgressRegistry() = default;

        /// What @p source captures must outlive the handle
        [[nodiscard]] EditorProgressHandle Register(EditorProgressSource source);

        /// Asks every source, in the order they were registered
        [[nodiscard]] EditorProgressSummary Collect();

    private:
        friend class EditorProgressHandle;

        struct Entry {
            uint64_t token = 0;
            EditorProgressSource source;
        };

        void Unregister(uint64_t token);

        Vector<Entry> _entries;
        uint64_t _nextToken = 1;
    };

}  // namespace BECore
