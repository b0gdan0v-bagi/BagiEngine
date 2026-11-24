#pragma once

#include <BECore/Control/JsonView.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// The version of the replay file format, written as `replay` in the header. A reader refuses any other number.
    inline constexpr int64_t kReplayFormatVersion = 1;

    /// The call of the control API that an entry records
    CORE_ENUM(ReplayOp, uint8_t, Reset, Action, State)

    /// The name of @p op in the file: `reset`, `action` or `state`
    [[nodiscard]] StringView ReplayOpName(ReplayOp op);

    /// The op a file name stands for; nullopt for any other text
    [[nodiscard]] Optional<ReplayOp> FindReplayOp(StringView name);

    /// Line 1 of a replay file
    struct ReplayHeader {
        String game;    ///< IGame::GetName() of the recorded game
        String engine;  ///< The engine version that wrote the file (informational)
        String title;   ///< Free text; empty is left out of the file
    };

    /// The game time between an entry and the one before it
    struct ReplayGap {
        uint64_t frames = 0;
        double seconds = 0.0;  ///< Total over the `frames` frames; zero when `frames` is zero

        [[nodiscard]] bool operator==(const ReplayGap& other) const = default;
    };

    /// One accepted call of the control API
    struct ReplayEntry {
        ReplayOp op = ReplayOp::Reset;
        /// Compact JSON text of one object: `reset`: IControllableGame::WriteReplayStart(); `action`, `state`: what the caller sent
        String params = "{}";
        /// Compact JSON text of one value: what PerformAction answered; only `action` has one
        Optional<String> outcome;
        ReplayGap gap;
        size_t line = 0;  ///< 1-based line of the file the entry was read from; 0 for an entry that was not read from a file
    };

    /// A file that was read: the header and the entries in order
    struct ParsedReplay {
        ReplayHeader header;
        Vector<ReplayEntry> entries;
    };

    /// Why a file was refused. `line` is 1-based, the line of the file the reason is about
    struct ReplayError {
        size_t line = 0;
        String message;
    };

    /// "line N: <message>"
    [[nodiscard]] String DescribeReplayError(const ReplayError& error);

    /// The header as one line of JSON without a newline; members in the order `replay`, `game`, `engine`, `title`
    [[nodiscard]] String FormatReplayHeader(const ReplayHeader& header);

    /// Appends an entry to @p out as one line of JSON without a newline; members in the order `op`, `params`, `outcome`, `gap`, so two recordings of one
    /// session diff cleanly. The two texts are written as they are, so they have to be one JSON value each. A caller that keeps @p out (cleared
    /// between entries) allocates nothing once it has grown
    void AppendReplayEntry(String& out, const ReplayEntry& entry);

    /// The same line from the parts a recorder holds: @p params is written from the view in the order of its document (a value that is not an
    /// object is written as `{}`), @p outcomeText is ready JSON of one value, or nullopt for an entry that has none
    void AppendReplayEntry(String& out, ReplayOp op, JsonView params, Optional<StringView> outcomeText, const ReplayGap& gap);

    /// AppendReplayEntry() into a new string
    [[nodiscard]] String FormatReplayEntry(const ReplayEntry& entry);

    /**
     * @brief Reads a replay file.
     *
     * The text is untrusted (a person or a model edits these files): the first reason it cannot be used is returned with its
     * line, and nothing aborts. Blank lines are skipped. The first entry has to be a `reset`: the file is of one game from a start. Each line is
     * read over a JsonTape, never a DOM; `params` and `outcome` are kept as the compact text of their value, so a hand-edited line with blanks reads
     * as its compact form and of two equal keys the first counts.
     */
    [[nodiscard]] std::expected<ParsedReplay, ReplayError> ParseReplay(StringView text);

}  // namespace BECore
