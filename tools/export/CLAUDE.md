# tools/export

Turns a game into a desktop package: a folder that runs without the sources or the build. Python, standard library only.

## Package layout

| In the package | What |
|----------------|------|
| `<exe>` | the executable, with its runtime libraries and `THIRD_PARTY_NOTICES.txt` beside it |
| `content/engine/`, `content/game/` | `config/` and `assets/` of the two content roots, also when one folder is both |
| `bagi-package.txt` | the marker; its first line is the executable's file name |

The executable reads `content/` only because of the marker (`src/Modules/BECore/FileSystem/CLAUDE.md`, "Content roots and layers"), so the names are written twice: `package_desktop.py` and `PackagedContent.h`.

Left out of the content: dot files and folders, `imgui.ini` (the editor's window layout) and `*.artgen.json` (the art pipeline's records). A **release** package ships cooked config (`src/Modules/BECore/Cook/CLAUDE.md`): the packaged executable cooks its own packaged content with `--cook-verify`, so what is checked is what will be read, and each packaged `config/` is replaced by its `layer<k>/config`, `k` counting only the roots that have a `config/`, lowest first. The cook runs in the temp folder, because the executable leaves a log file where it runs. A debug package ships the XML.

Only the libraries the manifest lists travel: a game that links shared libraries of its own has to add them.

## Manifest

`<build folder>/export/<Target>.json`, written at generate time for every desktop game executable of a Ninja build (`cmake/BagiExport.cmake`): `target`, `executable`, `engineRoot`, `gameRoot`, `notices` and `libraries`, a list of `{source, name}`. `name` is the file name the executable asks the system's loader for, which is not always the name of the source file. Every file it names must exist, else the target is not built.

## Safety of the output folder

The export replaces the output folder as a whole, so it refuses, before it writes anything:

- a folder that exists, is not empty and has no marker (the owner's files);
- one that is or contains the project or a content root (a package there would replace the sources);
- one inside a content root's `config/` or `assets/` (the game would read the package as content) or inside the export's build folder.

The checks compare the folders themselves, not their spelling, so a link or another letter case is refused too. The package is made in `<out>.partial` and takes the place of the old one (moved to `<out>.previous` for that moment) only after every step succeeded: a failed or cancelled export leaves the old package as it was. Both siblings obey the first rule. The marker is written first into `<out>.partial`, which is what lets a later export remove the left-over of a killed one.

## Tests

`.venv/bin/python -m pytest tools/export/tests`: a fake built game in a temp folder and a fake runner for the cook; no build is made.
