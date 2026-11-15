#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    class ControlService;

    CORE_ENUM(ConsoleOutputKind, uint8_t, Input, Result, Error, Note)

    struct ConsoleOutputLine {
        ConsoleOutputKind kind = ConsoleOutputKind::Note;
        String text;
    };

    /**
     * @brief What the command line printed: the typed lines and their answers, the newest last.
     *
     * Bounded: past kMaxLines the oldest line goes, and a line over kMaxLineBytes is cut and says how much was left out, so a `screenshot`
     * answer does not fill the panel. Not part of the log history.
     *
     * Written and read on the main thread only. It is reference counted atomically because the reply of a request holds it, and a reply
     * can outlive the session (a deferred one, or one that ControlService::Shutdown answers) and be dropped on the thread that destroys the service.
     */
    class ConsoleOutput : public RefCountedAtomic {
    public:
        static constexpr size_t kMaxLines = 200;
        static constexpr size_t kMaxLineBytes = 4096;

        void Append(ConsoleOutputKind kind, StringView text);
        void Clear();

        [[nodiscard]] size_t Size() const {
            return _lines.size();
        }

        /// @p index counts from the oldest line
        [[nodiscard]] const ConsoleOutputLine& At(size_t index) const {
            return _lines[(_first + index) % _lines.size()];
        }

        /// Grows on every Append and Clear
        [[nodiscard]] uint64_t GetVersion() const {
            return _version;
        }

    private:
        Vector<ConsoleOutputLine> _lines;
        size_t _first = 0;  // the oldest line once the ring is full; 0 before
        uint64_t _version = 0;
    };

    /**
     * @brief The commands typed before, walked with Up and Down.
     *
     * Bounded by kMaxConsoleHistory; an equal command moves to the end. The first Previous() keeps the text typed so far as a draft that
     * Next() gives back after the newest entry. Adding a command ends a walk.
     */
    class ConsoleCommandHistory {
    public:
        /// Replaces the entries (the stored start); empty and over-long ones are dropped, the newest kMaxConsoleHistory kept
        void Set(const Vector<String>& entries);

        void Add(StringView command);

        /// The older command to show, or nullopt when there is none; @p typed is the unfinished line, kept on the first call of a walk. Valid until the next call
        [[nodiscard]] Optional<StringView> Previous(StringView typed);

        /// The newer command, or the draft after the newest, or nullopt when no walk is going on. Valid until the next call
        [[nodiscard]] Optional<StringView> Next();

        [[nodiscard]] const Vector<String>& GetEntries() const {
            return _entries;
        }

    private:
        Vector<String> _entries;  // oldest first
        String _draft;
        size_t _position = 0;  // the entry shown while walking
        bool _walking = false;
    };

    /**
     * @brief The command line of the Console panel over a ControlService: Submit posts a typed command and prints the answer.
     *
     * Everything runs on the main thread. A reply holds only the output, never the session, so a session that is gone before a deferred answer
     * arrives (a `step`) costs nothing.
     */
    class ConsoleSession {
    public:
        ConsoleSession();

        /**
         * @brief Echoes @p text, parses it and posts it to @p service.
         *
         * An empty line does nothing; a line that does not parse is an Error line and is not posted. The answer is printed when it arrives (after
         * the service's Drain, or later for a deferred call): the result as compact JSON, or `code: message`.
         *
         * @return The command to keep in the saved history (trimmed): set when the line parsed, also when the call then fails.
         */
        Optional<String> Submit(ControlService& service, StringView text);

        [[nodiscard]] ConsoleOutput& GetOutput() const {
            return *_output;
        }

        [[nodiscard]] ConsoleCommandHistory& GetHistory() {
            return _history;
        }

    private:
        IntrusivePtrAtomic<ConsoleOutput> _output;
        ConsoleCommandHistory _history;
        int64_t _nextId = 1;
    };

}  // namespace BECore
