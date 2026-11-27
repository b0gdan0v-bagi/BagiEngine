# Where the data lives

You will find where each kind of thing is kept, see which file wins when two have the same name, change a value without touching code, and see how a mistake in a file shows. It takes about fifteen minutes.

You need the build from [Run the demo](run-the-demo.md). No C++ is written here.

## Data and code

The engine keeps data apart from code. In the words of the repository's `README.md`: scenes, UI, text and settings are XML under `config/` and `assets/`, a game's file of the same name wins, and a release build reads the cooked `.bin` files.

So you can change, with no programmer: the scenes and their nodes, the screens, the words and colours, the settings, the pictures, fonts and sounds. What a component *does* is code: a new kind of thing, a component that does not exist yet, needs a programmer, or an agent ([Ask for a change](ask-for-a-change.md)). You change the data of the kinds that exist.

## Where a file is

The engine has two folders of content, `config/` (XML) and `assets/` (fonts, pictures, sounds). `BagiDemo` is a small game, and its own files are in `tools/demo/`. Look at all four:

<!-- command -->
```bash
ls config assets tools/demo/config tools/demo/assets
```

| File or folder | What it holds | Where the page is |
|---|---|---|
| `SceneConfig.xml` | the scenes: nodes and their components. The engine has one, and a game replaces it | [Your first scene](first-scene.md) |
| the `nodes` folder in a game's `config` | node files, a node kept on its own and used by name | [A prefab and its overrides](prefab.md) |
| `TextureLibrary.xml` | the sprite library: which rectangle of which picture a sprite id is | [Your first scene](first-scene.md) |
| `ApplicationConfig.xml` | the application: the run mode, the renderer, the window file | [Config and reflection](config-and-reflection.md) |
| `SDLWindowConfig.xml`, `AudioConfig.xml`, `UIConfig.xml`, `TextConfig.xml`, `LoggerConfig.xml` | the window, the volumes, the UI scale and touch size, the fonts, the log sinks | [Config and reflection](config-and-reflection.md) |
| `docs/guide/examples/ui-screen/assets/ui/settings.ui.html` and `settings.css` | a screen: its tree in HTML and its style sheet | [A UI screen](ui-screen.md) |
| `docs/guide/examples/ui-screen/config/UIStrings.xml` and `UIThemes.xml` | the words per language and the colours per theme; the engine ships neither, a game has its own | [A UI screen](ui-screen.md) |
| `config/panels/` | the editor's own panels | [The editor](editor.md) |
| `assets/fonts/`, `tools/demo/assets/textures/demo_atlas.png` | fonts of the engine, the demo's picture | [Your first scene](first-scene.md) |

The three example paths are folders laid out like a game's content, so the screen's files are where a game would keep them: `config` for the XML, a `ui` folder in `assets` for the HTML and CSS. A file of a game is named as the engine's is, which is what the next section is about.

## The layers

A **content root** is a folder with `config/` and `assets/` in it. A game has the engine's root, and its own root on top of it (`src/Modules/BECore/FileSystem/CLAUDE.md`, "Content roots and layers"). In a browser there is a third root on top, the engine's `web/` folder with a few files for the page (`web/config/`).

A file is looked up from the top root down, and **a file of the same name in a higher root replaces the whole file**. It is not merged. A file that exists in one root only is used as it is. The consequence: to change one value of the engine's `SceneConfig.xml`, a game copies the whole file and changes the value. For a config file (a file of `config/` that a config type reads) the log names the winner (the run below shows it); for other files it says nothing.

Then there is the **overlay**. `--content-overlay=<folder>` makes a folder the top root, and every file the editor saves goes into it. So a try never touches a file that git tracks. Here is a made-up picture of the three folders, and which file wins:

<!-- illustration: made up for this page; the folders and the file names are the real ones of BagiDemo, but nothing lists them like this -->
```text
$HOME/bagi-data/config/SceneConfig.xml     <- the overlay (the top): this one is used
tools/demo/config/SceneConfig.xml          <- the game's: hidden by the overlay
config/SceneConfig.xml                     <- the engine's: hidden by the game's
```

### See the layers in a run

Make an overlay with a copy of the demo's scene, and change the title:

<!-- command -->
```bash
mkdir -p $HOME/bagi-data/config
cp tools/demo/config/SceneConfig.xml $HOME/bagi-data/config/
```

Open `$HOME/bagi-data/config/SceneConfig.xml` in your editor of text. Find the title (the line below is in the file), and change `text="BagiEngine demo scene"` to `text="Title from the overlay"`:

<!-- source: tools/demo/config/SceneConfig.xml -->
```xml
<component type="TextComponent" text="BagiEngine demo scene" fontFamily="Russo One" fontSize="46" align="Center" wrap="false" overflow="AutoFit" outlineWidth="2" shadowOffsetX="3" shadowOffsetY="4">
```

From `tools/demo/config/SceneConfig.xml`. Run the demo with the folder as the overlay:

<!-- command -->
```bash
build/macos-debug/bin/BagiDemo --content-overlay=$HOME/bagi-data
```

The title in the window reads `Title from the overlay`; every other node is as it was, because your file is a copy of the whole scene. Take the overlay away (run without the flag) and the title is the demo's again: the file in `tools/demo/` was never changed.

The log of this run names the winner of every config file that exists in two roots (`ConfigManager` writes the lines; an asset that two roots have is not logged). `engine.log` lies in the folder you started the demo from (its name is in `LoggerConfig.xml`, and git ignores it). The two lines of the scene, from the writer's run (the paths of the machine are shortened to `<repo>` and `<overlay>`):

<!-- illustration: the lines the config manager logged when the demo started with an overlay; the time of day is whenever you run it and the folders are yours -->
```text
[Info] [12:00:00.000] [ConfigManager] Config 'SceneConfig' from <repo>/tools/demo/config/SceneConfig.xml overrides <repo>/config/SceneConfig.xml
[Info] [12:00:00.000] [ConfigManager] Config 'SceneConfig' from <overlay>/config/SceneConfig.xml overrides <repo>/tools/demo/config/SceneConfig.xml
```

Read it as the layers: your file over the demo's, the demo's over the engine's. The scene written from nothing, in the same way, is [Your first scene](first-scene.md).

## Who writes a file

You do, in an editor of text. Or the editor of the engine does, with Save: the Scene Editor's Save writes the scene back, and the Config Editor's Save writes the config ([The editor](editor.md), section 5). Save writes the file the config was read from, the one that won; with an overlay it writes a copy in the overlay and leaves the original (`src/Modules/BECore/Config/CLAUDE.md`, "ConfigManager"; `FileSystem/CLAUDE.md`, "Content overlay"). Two things the overlay does not cover are named there: the editor's window layout (`config/imgui.ini`) and the presets of the Generate panel.

When a file changes while the game runs, the engine can read it again: [Config and reflection](config-and-reflection.md#change-a-config-while-the-game-runs) shows it, and that a scene that is already running keeps what it built.

## A mistake in a file shows

A file that is slightly wrong does not stop the game. It is read, and what is wrong is logged. Try it on the overlay you have: in the same title line, write `fontsize="46"` in place of `fontSize="46"` (a lower case `s`), and run the demo again with the overlay. The terminal and `engine.log` have:

<!-- illustration: the warning of the writer's run; the time of day is whenever you run it, the numbers are the place: the second node of the scene, its first child, the second component -->
```text
[Warning] [12:00:00.000] Config 'SceneConfig': scenes[0].nodes[1].children[0].components[1].fontsize: Unknown attribute 'fontsize', did you mean 'fontSize'?
```

The wording is the engine's. The title is drawn as before, because the key it did not know was skipped. An unknown key, a missing required field and a value outside its range are warnings, never silence (`src/Modules/BECore/Config/CLAUDE.md`, "Reading values"). Run with `--strict-config` and the same line is an `[Error]`; the demo still starts, and the title is drawn the same.

<!-- command -->
```bash
build/macos-debug/bin/BagiDemo --content-overlay=$HOME/bagi-data --strict-config
```

Put `fontSize` back, or delete the folder, when you are done.

## In a release: the cook

You edit XML, and XML stays the source. A release build reads `.bin` files that **the cook** makes from it, and nobody edits a `.bin` (`src/Modules/BECore/Cook/CLAUDE.md`). The cook is a mode of the game's own program, and `BagiDemo` has a target for it, which writes into the build folder (git ignores it):

<!-- command -->
```bash
cmake --build --preset macos-debug --target BagiDemoCook
```

The writer's run printed a line for each file, `Cooked:` or `Copied:` for a file the cook leaves as it is, and ended with the count:

<!-- illustration: the last line of the cook of the demo on the writer's machine; the count is the one of this commit and changes as files are added -->
```text
10 cooked, 26 copied, 0 failed
```

Under the rule of the cook, **any finding fails the file**, and no `.bin` is left for it: the unknown key above is a finding, so a typo is found by the cook and not by a player (`Cook/CLAUDE.md`, "The failure rule"; that is the rule of the file: the cook above ran on the demo's clean files and so met no finding). The `.bin` files are the release's. A game follows with its own cook after `Update engine`, and its XML does not change (`Cook/CLAUDE.md`, "How a game follows").

## The content has a version

A scene file and a node file carry `contentVersion` on their root, and the engine reads exactly one number. A scene file of an older, newer or malformed version is one error in the log, and the scene is empty; a node file of one is dropped (`Config/CLAUDE.md`, "Content version"). The message names the file and both numbers. When the engine changes what such a file may say, a tool rewrites a game's files to the new form (`python3 engine/tools/migrate/migrate.py <your game folder>`: `tools/migrate/CLAUDE.md`), so the files are not edited by hand, one by one. That tool is the engine side's, and the changelog of an engine update lists its steps for a game ([NEW_GAME.md](../../NEW_GAME.md), where a game's repository is made).

## I want to ...

| I want to ... | Change this | Page |
|---|---|---|
| move or add a node | the node's `TransformComponent`, or a new `<node>` in `SceneConfig.xml` | [Your first scene](first-scene.md) |
| change how a screen looks | the screen's CSS and its theme colours | [A UI screen](ui-screen.md) |
| change a text, or add a language | the screen's strings file, `UIStrings.xml` | [A UI screen](ui-screen.md) |
| change how a thing moves | the tween on its node | [A tween and a timeline](tween-and-timeline.md) |
| make a click do something | the action of a clickable and the node that listens | [Input actions](input-actions.md) |
| change a setting | the field in the config file of that setting | [Config and reflection](config-and-reflection.md) |
| make the fountain stronger | the emitter's `effect` in the inspector | [The editor](editor.md) |
| use one node many times | a node file and a `ref` to it | [A prefab and its overrides](prefab.md) |
| keep a scene I changed | Save, into the overlay or the tracked file | [Run the demo](run-the-demo.md) |

## What you did

You found where the data of a game is, saw that a file of the same name in a higher root replaces the whole file, changed a title in an overlay and read the layers in the log, saw a misspelled key become a warning (and an error with `--strict-config`), and ran the cook that a release uses.

Next: [Back to the guide's index](README.md)
