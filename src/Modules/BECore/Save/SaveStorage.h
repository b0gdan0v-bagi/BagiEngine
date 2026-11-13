#pragma once

namespace BECore {

    /// The save folder of a browser build: a folder of the page's virtual file system that the page keeps in the site's IndexedDB (web/saves.js)
    inline constexpr eastl::string_view kWebSaveDirectory = "/bagi/saves";

    /**
     * @brief Whether the files of the save folder outlive the program.
     *
     * NotApplicable: the platform keeps the folder itself (a desktop build); nothing to ask. Persistent: a browser build whose
     * page mounted the folder on IndexedDB. NotRemembered: a browser build whose saves live in memory only and are gone at the
     * next reload (IndexedDB refused or did not answer in time, or the page has no web/saves.js).
     */
    CORE_ENUM(SaveStorageState, uint8_t, NotApplicable, Persistent, NotRemembered)

    /**
     * @brief The folder a game passes to SaveGameService::Initialize() to get saves that survive a reload
     *
     * kWebSaveDirectory in a browser build, an empty path elsewhere (the game keeps its own real folder there).
     */
    [[nodiscard]] std::filesystem::path PersistentSaveDirectory();

    /**
     * @brief Whether the saves are remembered. Read after the game started: the page settles it before the start.
     *
     * A browser build asks the page (Module.bagiSaves.state); a page without it reads as NotRemembered. Main thread only.
     */
    [[nodiscard]] SaveStorageState GetSaveStorageState();

    /**
     * @brief Tell the storage that a save file was written or removed, so it can flush it
     *
     * SaveGameService calls it after a successful WriteSlot() and Delete(). In a browser build the default handler asks the
     * page to flush the folder to IndexedDB soon; elsewhere it does nothing. Main thread only.
     */
    void NotifySaveChanged();

    /// For tests: replace what NotifySaveChanged() calls. An empty function restores the default.
    void SetSaveChangedHandler(eastl::function<void()> handler);

}  // namespace BECore
