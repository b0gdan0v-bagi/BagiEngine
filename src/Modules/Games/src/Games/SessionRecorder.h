#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonFwd.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/Singleton.h>
#include <Games/ReplayFile.h>
#include <expected>
#include <fstream>  //-NoStd: the file layer has no write call (FileSystem/CLAUDE.md), so the recording is a std::ofstream

namespace BECore {

    class IGame;

    /**
     * @brief Writes the accepted calls of the control API on the active game to a replay file, one flushed line each.
     *
     * Main thread only, no lock: every report comes from a control handler or from the game's own input, which run there.
     * The calls are reported by ControlledReset / ControlledAction / ControlledSetState (GameControl.h) and, for a person's input
     * that does not pass them, by a game directly. A call the game refused is never reported.
     *
     * The file starts at a `reset`: what comes before the first one acts on a state the file does not hold and is dropped.
     * A write that fails stops the recording with one log error and leaves the game running.
     */
    class SessionRecorder : public Singleton<SessionRecorder> {
        friend class Singleton<SessionRecorder>;

    public:
        /**
         * @brief Starts a recording of @p game into the file at @p path (a path of the host), replacing it.
         *
         * Errors: not_controllable, unsupported (the game is not replayable), invalid_params (a recording runs, or the file cannot be written).
         */
        std::expected<void, ControlError> Start(StringView path, IGame& game, StringView title = {});

        /// Ends the recording and answers how many entries it wrote; 0 when none ran. A recording that stopped by itself has been closed already
        size_t Stop();

        [[nodiscard]] bool IsRecording() const;

        /// Entries written by the running recording, or by the last one after it has ended
        [[nodiscard]] size_t GetEntryCount() const;

        /// @p startParams are IControllableGame::GetReplayStart() after the reset, not what the caller asked for
        void ReportReset(const Json& startParams);

        /// @p action as the game was given it, @p outcome what PerformAction answered
        void ReportAction(const Json& action, const Json& outcome);

        void ReportSetState(const Json& state);

    private:
        SessionRecorder() = default;

        void Write(ReplayOp op, const Json& params, const Json* outcome);
        void Fail(StringView what);

        std::ofstream _file;  //-NoStd: see the include
        bool _recording = false;
        bool _started = false;  ///< A reset has been written: the file has a state to act on
        bool _warnedDropped = false;
        size_t _entryCount = 0;
        uint64_t _lastFrame = 0;
        double _lastSeconds = 0.0;
    };

}  // namespace BECore
