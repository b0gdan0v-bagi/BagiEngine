#pragma once

#include <Games/ISeatedGame.h>

namespace BECore {

    /// What a room server needs to know about a seated game without creating it
    struct SeatedGameInfo {
        String name;
        int seatCount = 0;
        /// The keys of the `options` of `create_room` that every member of a room may see before the start (the `lobby` message
        /// carries them, `Room::SetLobbyOptions`): only these, and only with a boolean or integer value. The others stay the game's.
        eastl::vector<String> publicOptions;
        /// Whether a public room of this game that has started and still has a free seat is in the answer to `list_rooms` (`started: true`).
        /// Nobody can join it (`game_started`); it is for a game that wants to show what is going on. A finished room is never listed.
        bool listsStartedRooms = false;
    };

    using SeatedGameFactory = std::function<eastl::unique_ptr<ISeatedGame>()>;

    /**
     * @brief The seated games a server can create by name.
     *
     * A plain class that the caller owns and fills (a server registers its games at start-up): no singleton and no static
     * initializer. Names come from a client (`create_room {game}`), so they are looked up in a hash map of strings and
     * never interned into PoolString. Not thread-safe: fill it before the server starts, then only read it.
     */
    class SeatedGameRegistry {
    public:
        /**
         * @brief Adds a game, or replaces the one of the same name.
         *
         * @return false (and nothing changes) for an empty name, fewer than one seat or an empty factory.
         */
        bool Register(SeatedGameInfo info, SeatedGameFactory factory);

        /// The info of the game @p name, or nullopt when there is none
        [[nodiscard]] Optional<SeatedGameInfo> Find(StringView name) const;

        /// A new independent instance of the game @p name (not started), or null when there is none
        [[nodiscard]] eastl::unique_ptr<ISeatedGame> Create(StringView name) const;

        [[nodiscard]] size_t GetCount() const;

    private:
        struct Entry {
            SeatedGameInfo info;
            SeatedGameFactory factory;
        };

        eastl::hash_map<String, Entry> _games;
    };

}  // namespace BECore
