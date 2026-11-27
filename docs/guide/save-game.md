# A save

You will give a game a save file: a class for what the player did, a call that writes it to a slot on disk, a call that reads it back, and the rules that keep a damaged, foreign or newer file from breaking the game. Then you will run the test that does all of it and read what each case shows.

You need the build from [Run the demo](run-the-demo.md), and it helps to have read [Config and reflection](config-and-reflection.md): the state of a save is a class marked the same way. Reading the C++ needs no tools.

## What a save is, and what it is not

The **Save button of the editor** writes the scene's XML, a file for you to edit and keep in the repository ([Run the demo](run-the-demo.md) shows it). That is content. A **save of a game** is another thing: a file the running game writes about the player (the level, the name, the coins) and reads back at the next start. The player never edits it, and nobody keeps it in the repository.

The file is **binary**: a reflected state in the engine's binary format, wrapped in an envelope that says which game wrote it and in which version of its state. The envelope is what lets the game refuse a file instead of guessing. One file is one **slot**, named `<slot>.save`, in a folder the game gives to the service. The service is `SaveGameService` (`src/Modules/BECore/Save/`). A save is never cooked: it is read as it was written.

## The state

The state is a class marked like a config. Only the `BE_REFLECT_FIELD` members go into the file; [Config and reflection](config-and-reflection.md#the-marks) says what the marks do.

<!-- source: tests/BECore/GuideSaveExample.h -->
```cpp
struct GuideProfileState {
    BE_CLASS(GuideProfileState)

    BE_REFLECT_FIELD int32_t _level = 1;
    BE_REFLECT_FIELD String _name;
    BE_REFLECT_FIELD Vector<int32_t> _unlocked;
    BE_REFLECT_FIELD int32_t _coins = 10;
};
```

From `tests/BECore/GuideSaveExample.h`. The code of this page's blocks is compiled into the engine's tests and run there (`GuideSaveExampleTest`), so it does what the page says. The engine ships no game with a save, so the example lives with the tests; a game puts its own state class wherever its code lives. A field's value in the file is stored under its name without the underscore, and a field that is not in the file keeps the default you gave it in the class (`_coins = 10`). Remember this, it is what makes a new field cheap.

## The schema

A game says two things about itself: **who it is** and **which version of its state it writes**. That is the `SaveSchema`:

<!-- source: tests/BECore/GuideSaveExample.h -->
```cpp
constexpr uint32_t kLevelsFromOneVersion = 2;
...
constexpr SaveSchema kProfileSchema{"GuideGame", kLevelsFromOneVersion, 1};
```

From `tests/BECore/GuideSaveExample.h`. The three values are `game`, `version` and `minVersion`. Every file is written with the game and the version, and every read compares them with the schema it was given:

- a file of **another game** (`game` differs) is refused: a save of a chess game must not be read as the state of a platformer;
- a file **newer** than the game (`version` is above the schema's) is refused: the game does not know what the later one wrote;
- a file **older than `minVersion`** is refused: the game can no longer read its shape;
- anything between `minVersion` and `version` is read, and the call tells the game which version it was, so the game can fix it up.

## Save and load

The service needs a folder. On the desktop the game chooses it; in the browser it is `PersistentSaveDirectory()`, which is empty everywhere else, so a game passes it as it is and keeps its own folder on the desktop. `Initialize` makes the folder if it is missing. The two calls then take a slot name and the schema, and a reflected state:

<!-- source: tests/BECore/GuideSaveExample.h -->
```cpp
[[nodiscard]] bool Save(StringView slot) {
    const auto saved = _saves.Save(slot, kProfileSchema, _state);
    if (!saved) {
        LOG_ERROR("Cannot save '{}': {}", slot, saved.error().detail);
    }
    return saved.has_value();
}
```

From `tests/BECore/GuideSaveExample.h`. Both calls return a `std::expected`: a value, or a `SaveError` with a `kind` to branch on and a `detail`, a sentence for the log. A slot name is 1 to 64 letters, digits, `_` and `-`: no dot, slash or space, so a name cannot leave the folder; `"../x"` is refused as `InvalidSlotName`.

The write is **atomic**: the service writes `<slot>.save.tmp`, flushes it and renames it over `<slot>.save`. If the power goes in the middle, the old save is still there whole.

## Load into a temporary, apply after a success

The file is input nobody trusts: a player edits it, a disk damages it, an update changes the game. So the game does not read into its live state. It reads into a **temporary**, and replaces the live state only when the read has passed:

<!-- source: tests/BECore/GuideSaveExample.h -->
```cpp
LoadOutcome Load(StringView slot) {
    GuideProfileState loaded;
    const auto info = _saves.Load(slot, kProfileSchema, loaded);
    if (!info) {
        if (info.error().kind == SaveErrorKind::NotFound) {
            return LoadOutcome::NewGame;
        }
        LOG_ERROR("Cannot load '{}': {}", slot, info.error().detail);
        return LoadOutcome::Refused;
    }
    if (info->version < kLevelsFromOneVersion) {
        ++loaded._level;
    }
    _state = std::move(loaded);
    return LoadOutcome::Loaded;
}
```

From `tests/BECore/GuideSaveExample.h`. The reason: a read that fails halfway can leave the state partly filled. With a temporary, a refused file changes **nothing**, and the game goes on as it was. The rules of a game's loader:

- **One log line, never a crash.** The `detail` is one short sentence that names the slot and the reason, never the contents. A refused version looks like this:

<!-- illustration: the log line of a refused file (a save of version 3 read by a game that reads up to 2); the time of day is whenever it happens -->
```text
[Error] [12:00:00.000] Cannot load 'newer': The save in slot 'newer' has version 3, this game reads up to 2
```

- **No file is not an error.** `NotFound` is a new game: nothing to load, nothing to say.
- **The damaged file is left alone.** The game does not delete or rewrite a file it refused, until the player chooses to save again. A refusal that was a mistake (a save of a newer build, a rule too strict) then loses nothing.

What the game does for each kind of refusal:

| Kind | What it means | What the game does |
|------|---------------|--------------------|
| `NotFound` | the slot has no file | starts a new game, no message |
| `Corrupt` | the bytes are not a save, or a field has another type | logs the `detail`, keeps the live state, leaves the file |
| `WrongGame` | the file is another game's | logs, keeps the state, leaves the file |
| `VersionTooNew` | the file is from a newer build | logs, keeps the state, leaves the file |
| `VersionTooOld` | the file is below `minVersion` | logs, keeps the state, leaves the file |
| `ReadFailed` | the file exists and cannot be read | logs, keeps the state |
| `InvalidSlotName` | the name has a dot, a slash or a space | a bug of the caller: fix the name |
| `NotInitialized` | `Initialize` was not called, or failed | a bug of the caller |
| `WriteFailed` | a save could not be written (`Save` only) | logs; the old save stays |

## A new version

**A field you add needs no version.** The file stores fields by name, so an old file that lacks `coins` loads with the default of the new class, 10. Add the field and go on.

**Raise `version` when the meaning of data changes**, or a field is removed. Raise `minVersion` when the old shape can no longer be read at all. In the example, version 1 counted the levels from 0 and version 2 counts them from 1. The file does not know that; the game does, and `Load` gives it the version of the file it read, so the fix-up is one line (the `info->version < kLevelsFromOneVersion` of the block above). A file of version 1 is read, and its level is one more.

## Try it

The test runs everything above against a temporary folder of its own, so it touches no save of yours:

<!-- command -->
```bash
cd build/macos-debug
bin/BagiEngineTests --gtest_filter='GuideSaveExample*'
```

It needs the build of [Run the demo](run-the-demo.md). The cases are:

- a profile saved to `slot1` comes back field by field; `slot1.save` is in the folder and no `.save.tmp` is left;
- a slot with no file is a new game and logs nothing;
- a file written by version 1 of the game is read, its level is one more, and the field it never had keeps its default;
- a file of another game and a file of a newer version are refused, the live state is untouched, and the log has one line;
- a file cut in half is `Corrupt` with a `detail`, the live state is untouched, and the file on disk is left as it was;
- a name with a slash is refused, and `List()` shows the slots by name.

The writer saw one thing worth saying about the file: the profile of the test (a name, three unlocked levels, a level and the coins) is **253 bytes**, in a single file `slot1.save`. A real save is small; the engine refuses anything over 16 MiB.

## In the browser

A game in the browser calls `SaveGameService::Initialize(PersistentSaveDirectory())` and nothing else changes. The page keeps the folder in the browser's **IndexedDB**, loads it before the game starts and writes it back soon after every save, so the files survive a reload. They are the site's data: clearing the site data clears the saves, and two tabs of one site share them. `GetSaveStorageState()` says whether a save is remembered (`Persistent`) or lives in memory only (`NotRemembered`, gone at reload); a game that wants to warn the player reads it after the start. The engine shows nothing itself.

## What you did

You gave a game a state class, a schema and two calls, saw that a file is a slot written whole or not at all, read it into a temporary so that a refused file changes nothing, and met the kinds of refusal and what the game does about each. You know when a version needs a bump and when a field does not, and you ran the test that pins all of it.

The engine keeps one slot of its own in a game's folder, `audio`, with the player's sound levels; a game does not name a slot so. The exact rules of the service are in `src/Modules/BECore/Save/CLAUDE.md`, written for coding agents.

Next: [The editor](editor.md), where each panel lives and what it is for.
