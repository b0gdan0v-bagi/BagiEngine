# tools/migrate

Rewrites the content files of a game to the format the engine reads now. Standard library only, Python 3.10+; the engine does not use it, a game runs it (`python3 engine/tools/migrate/migrate.py <game folder>`). It exists because an engine change that renames or moves what a scene file may say would otherwise be redone by hand in every game, and a file nobody migrated would load wrong without a word.

| File | Purpose |
|------|---------|
| `migrate.py` | the command, the step API (`step`), the registry, the scan; `main(argv, engine_steps)` and the pieces are importable (the tests call them without a subprocess) |
| `xmltext.py` | the lossless XML model the steps edit: the contract is its module docstring |
| `steps/` | the engine track: one module per step, `s0001_<name>.py` (a module starting with `_` is not loaded: `_ui_button.py` is the mapping `UIButton` to components that steps 1 and 2 share, `_ui_value.py` that of `UIToggle` and `UISlider` for step 3; a step finds it by putting its own folder on `sys.path`) |
| `tests/` | pytest: `.venv/bin/python -m pytest tools/migrate/tests`; the step fixtures are `tests/fixtures/<step>/<case>.before.xml` and `.after.xml` |

## The format version

A **content file** is an `*.xml` with a `config` folder in its path whose root element is `SceneManager` (a `SceneConfig.xml`) or `node` (a node file of `config/nodes/`). Its root carries `contentVersion="N"`; absent is 1 (the format before versions), `0` is older than anything and malformed is an error of the file. The engine has one number, `kContentVersion` (`BECore/Config/ContentVersion.h`, the contract of the loader in `src/Modules/BECore/Config/CLAUDE.md`), and the highest `to_version` of the engine track must equal it (`tests/test_agree_with_engine.py`). Other files (`UIConfig.xml`, `TextureLibrary.xml`, ...) are not content files and no step touches them; the first step that needs one adds its root element to `CONTENT_ROOTS` in `migrate.py` and in `ContentVersion.h`, with a rule in this file.

Inline XML in the C++ strings of a test is not a content file; the tool does not see it.

## Steps

```python
from migrate import step

@step(from_version=1, name="ui-button-to-components")          # track="engine" by default
def run(doc, ctx):                                              # doc: xmltext.Document
    for e in doc.find_all(lambda e: e.get("type") == "UIButton"):
        ...
        ctx.report(e, "Play")                                   # one note per element changed
    ctx.fail(e, "reason")                                       # the element cannot be changed: the file stays as it was
```

- A step takes a file at version N to N+1. The registry refuses a gap, a duplicate or a step over two versions, naming the module. The steps of the engine track are `steps/*.py` in the order of the names.
- A step is a plain function over the model, never reads another file, and says in its module docstring what it rewrites and what it **refuses**. A step that cannot give the same behaviour for a shape fails on it (`ctx.fail`) instead of guessing: a migration never changes how a game looks silently.
- A step raising an exception is an error of that file only (the file is left, the run goes on, exit 2).
- Idempotent: what a step already rewrote must not match it again.
- The steps so far: 1 `ui-button-to-components` (every `UIButton` that is no group child), 2 `ui-group-children-to-components` (the `UIButton` segments of a `UISegmented` and options of a `UIDropdown`; a file already at 2 never meets step 1 again, which is why the group needed a step of its own), 3 `ui-toggle-slider-to-components` (every `UIToggle` and `UISlider`, at any depth; the off track and the thumb derived from a literal `textColor` are written out, a token `textColor` needs a `trackColor`; the caption gets `fadeWhenDisabled`, since the class faded it). A file at 1 runs all three.

## The command

```
python3 engine/tools/migrate/migrate.py [FOLDER | --root DIR] [--steps DIR]... [--check] [--diff]
```

- Finds the content files under the root (default: the current folder); skips `build*`, `.git`, `node_modules`, `.venv`, `external` and, at the root, a folder `engine/` that is a submodule (has `.git`) or holds this tool, so a game does not rewrite the engine's content.
- For each file: reads the version of every track, applies the pending steps in order, stamps the new version (the last attribute of the root; a root with its attributes on separate lines gets it on a line of its own), writes only when **every** step succeeded, through a temporary file renamed over the file. A file at the newest version is not touched. Line endings, a BOM and a missing final newline stay.
- Output: `path: 1 to 2 (step-name): N elements` and one indented note per element, problems on stderr as `path:line: step: reason`, one summary line.
- `--diff` prints a unified diff and writes nothing (exit 0). `--check` writes nothing and exits 1 when a file needs a step (what CI or a game's test runs). Exit 2 for an error (not well-formed, a bad version, a failed step, a bad registry), also when other files were written.
- Run it twice: the second run changes nothing.

### A game's own steps

`--steps DIR` loads the step modules of a game; they declare their own `track`, and the version of that track is the attribute `contentVersion.<track>` (the engine's loader does not read it: the game's own test runs the tool with `--check`). The engine track runs first, so a game's step sees files already at the engine's version. The track `engine` is not allowed there.

## How an engine change adds its step

A change that renames or removes a reflected class or field a content file can name, moves a field from one class to another, or turns a control into components, changes the format. **Never edit a game's content files by hand**: they are in other repositories and a hand edit is not repeatable. Instead, in the same branch:

1. A module `steps/sNNNN_<name>.py` with `@step(N, "<name>")`, N the current `kContentVersion`.
2. A fixture pair `tests/fixtures/<name>/<case>.before.xml` / `.after.xml` per shape (a whole real file where one exists, plus a case per rule and per refusal) and a pytest that checks the output equals the `after` exactly and that the `after` migrates to itself. A failure case has a `before` and no `after`.
3. A C++ test of the `after` fixtures on its own: `tests/BECore/ContentMigrationAfterTest.cpp` (built on `tests/Common/MigrationTwin.h`) loads every final `after` fixture and checks the composed form (validator, `ui_tree`, clicks). A twin that compares with the class's draw exists only for as long as the class does; steps 1 to 3 had one and lost it when their `before` side was removed.
4. `kContentVersion` becomes N+1.
5. `python3 tools/migrate/migrate.py --root .` in the engine repository for its own content (`config/`, `games/Match3/config/`); the diff goes in the commit. The games are migrated by their owners after the engine is updated.

## The text model

`xmltext.py` is a tokenizer, not a pretty printer: the file comes back byte for byte except the start tags a step edited, and new children take the indentation and the line ending of the file. This is why the diff of a migration stays reviewable (a library such as pugixml re-indents the file and rewrites every attribute). `Element.set` keeps an attribute's place and quotes; a new attribute goes last, on a line of its own when the tag spans lines. A malformed file is one `XmlTextError` with line and column and the tool leaves it.
