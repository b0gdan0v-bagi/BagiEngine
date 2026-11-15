# tools/schema

Completion and checking of `config/*.xml` in an editor, from the reflection of the running game. Two scripts, standard library only (3.10+); tests in `tools/schema/tests` (pytest; `xmllint`, libxml2, is the judge of the XML and those cases are skipped, with the reason, when it is not on `PATH`).

| File | Purpose |
|------|---------|
| `export_schema.py` | Starts a built game (`<exe>` or `--preset P --game G [--root R]`, `--headless` on the manual clock, with the helpers of `tools/control`), calls the control method `schema` (`src/Modules/BECore/Control/CLAUDE.md`), asks it to quit and writes the JSON (`--out`, else stdout). Exit 0, 1 the game did not start or refused `schema`, 2 a schema of another version, 77 the executable does not exist |
| `schema_to_xsd.py` | The JSON to one XSD 1.0 per config file; `--check` validates each covered file with `xmllint`; the functions (`build_xsd`, `convert_config_dir`, `check_files`) are what the control smoke of `BagiStress` calls |
| `schema_common.py` | The schema version, the names the XML serializers write, the exit codes |

```bash
python3 engine/tools/schema/export_schema.py --preset macos-debug --game BagiMahjong --out build/schema.json
python3 engine/tools/schema/schema_to_xsd.py build/schema.json --config-dir config --out-dir build/xsd --vscode-settings
```

The second command prints `covered N, not covered M`, one line per file, and with `--vscode-settings` the `xml.fileAssociations` block for the XML extension of VS Code (Red Hat's): paste it into `.vscode/settings.json` and the files complete and are checked as you type. The XSD files are made, not kept: a game that changes a reflected class exports again.

## What the XSD says

- **The type of a file**: a `--bind Stem=Type` for it, else the type named like the file's stem (`config/UIConfig.xml` is `UIConfig`), else the type named like its root element (`SceneConfig.xml` has the root `SceneManager`). A file with none is "not covered", never an error: it is read by hand. `Stem=Type@element` says the type sits one element down (`SDLWindowConfig.xml` holds `<window .../>`). The global element is the file's own root element; a content root (`SceneManager`, `node`) also takes `contentVersion`, and a `node` takes the loader's `ref`.
- **The shapes** are those the serializers write (`Reflection/CLAUDE.md`): a scalar or enum field is an attribute (`range` is a `minInclusive` / `maxInclusive`, a `FixedString` capacity a `maxLength`, `required` is `use="required"`, a `tooltip` an annotation), an object field a child element, a list its field element holding `item` elements (a scalar item has a `value` attribute), a map `entry` elements with a `key`, a variant `index` and `value`, a pair `first` and `second`. A field the schema calls `opaque` (a class with a hand-written Serialize, such as a colour) is any content.
- **Factory elements and the XSD 1.0 limit.** XSD 1.0 cannot say that the attributes of an element depend on its `type`, so a factory element (`<widget type="...">`) is declared once per factory base: `type` is an enumeration of the creatable derived types, and its attributes and children are the **union** of those of all derived types, every one optional. Completion and the check catch an unknown name or value; they do **not** catch an attribute that belongs to another derived type (that needs XSD 1.1, which `xmllint` does not check). Where two derived types give one name two shapes, the attribute is a string and the child any content. `type` is required only for a base that cannot be made itself (an abstract one).
- **The element name of a list of a factory** is `elementName` of the schema. The meta generator gives some bases none (`IComponent`, `SceneNode`, `Scene` lists are read whatever the name); then the last word of the class name is used (`component`, `node`, `scene`), which is what the files write.
- A file the XML parser rejects is listed as not covered with its error (pugixml reads some files `xmllint` does not, such as a `--` inside a comment).

## The engine's own files

The control smoke of `BagiStress` (`tools/stress/tests/stress_smoke.py`, scene `reflection`) exports the live schema, makes the XSDs of the engine's `config/` and validates every covered file; the bindings it passes are the engine's whole list of files whose type is not named like their stem (`LoggerConfig` is `LoggerManager`, `AssertHandlersConfig` `AssertHandlerManager`, `TestsConfig` `TestList`, `WidgetsConfig` and `GameWidgetsConfig` `WidgetList`, `SDLWindowConfig` its type inside `<window>`; `ApplicationConfig` and `SceneConfig` need none). A game that wants the check calls the same functions with its own bindings.

**Not covered**, and why: `TextConfig.xml` and `UIConfig.xml` are read by hand (the text system and the UI environment have no reflected type). A game's `UIStrings.xml` is the same case: its keys are the data. The smoke fails when the set of uncovered engine files changes, so a new file says how it is read.
