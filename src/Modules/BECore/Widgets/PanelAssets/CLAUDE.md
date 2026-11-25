# PanelAssets: an editor panel described as data

A panel asset is a file `config/panels/<Name>Panel.xml` that says what a simple tool panel shows (text, fields, buttons, a table of a list) and which editor command each button runs, so changing a panel is a file edit and a hot reload, not C++ and a rebuild. This folder is the pure half: the classes of the asset, the loader and validator, the registry of named sources, the reads of a bound value and the one-field visitor. It has no ImGui and is compiled in every build, because a reader of untrusted XML must be testable and fuzzable everywhere (`FuzzPanelAsset`). **What is not here:** the interpreter that draws an asset, the widget that hosts it, the model interface and the hand-written drawers (`PanelInterpreter`, `AssetPanelWidget`, `IPanelModel`, `PanelSlots`) are the editor's, in `src/Widgets/PanelAssets/` (its `CLAUDE.md`).

A model (C++) owns the logic and the data: it registers a reflected object under a name and handles the commands. The asset says how it looks and what it is bound to. A button never calls code: it emits `EditorCommandEvent{command, argument}`, and the model that owns that command subscribes (`EditorCommandEvent::argument` is a `String`, empty for a button with no argument).

## The asset format

```xml
<PanelAsset id="FileChanges" title="File changes" command="view.fileChanges" open="false" width="640" height="320">
  <sources>
    <source name="files" model="FileChangesPanelModel"/>
  </sources>
  <elements>
    <element type="PanelEmptyState" id="notRunning" icon="file-diff" text="The file watcher is not running" visible="!files.running"/>
    <element type="PanelRow" id="toolbar" visible="files.running">
      <elements>
        <element type="PanelButton" id="applyAll" text="Apply all" command="fileChanges.applyAll" enableWhen="files.hasPending"/>
      </elements>
    </element>
    <element type="PanelTable" id="pending" source="files.pending" visible="files.hasPending">
      <columns>
        <column type="Text" id="path" field="path"/>
        <column type="Button" id="apply" icon="check" command="fileChanges.apply" argument="path"/>
      </columns>
    </element>
  </elements>
</PanelAsset>
```

- **The root** (`PanelAsset`): `id`, `title`, `command` (the editor command that shows and hides the panel), `open` (its state at start), `width` / `height` (0: the editor's default), `sources`, `elements`. The loader does not check the name of the root element; the schema tool finds the type of a file by it, and the `Panel` suffix of the file stem keeps a panel from colliding with a real config (`ConfigManager` keys configs by the stem).
- **Sources:** `source` has a `name` (`[A-Za-z_][A-Za-z0-9_]*`, unique in the asset) and a `model` (the name of the model class the editor resolves; here it is only a text, checked through `PanelValidationContext::isKnownModel` when the editor passes one). A binding may also name a source registered in `PanelDataSources` that the asset does not list. The writer of the reflection names the items of `sources` and `columns` `<item>`, and the reader takes any tag name, so a hand-written `<source>` / `<column>` loads and the schema says `item`.
- **Elements** all derive from `PanelElement` (`id`, `visible`, `tooltip`, `elements`; the factory element name is `element`, its attribute `type` the class name): `PanelSection` (collapsing header: `title`, `defaultOpen`, children), `PanelRow` (children on one line), `PanelText` (`text` literal or `bind`, `style` `Normal|Disabled|Success|Warning|Error`, `wrap`, `mono`), `PanelField` (one top-level field of the object `bind` names, drawn as the inspector draws it: `bind`, `label`), `PanelFields` (every field of that object), `PanelInput` (a text field bound to a `String` field: `bind`, `hint`, `icon`, `width` in em), `PanelButton` (`text`, `icon`, `command`, `argument`, `enableWhen`), `PanelTable` (`source` a list, `columns`, `header`, `borders`, `maxRows`; a `PanelColumn` has `type` `Text|Button|Selectable`, `title`, `field`, `styleField`, `command`, `argument`, `icon`, `tooltip`; `styleField` is a path relative to the row to a `PanelTextStyle` field, the colour of a `Text` cell per row), `PanelSeparator` (`title`), `PanelEmptyState` (`icon`, `text`), `PanelSlot` (`name`: a hand-written drawer registered under it). Only `PanelSection` and `PanelRow` hold `elements`. A button's condition is `enableWhen`, not `enabled`: the factory writes and reads `enabled` itself on every element of a factory list, so a binding text there would make the element vanish at load.
- **A binding** is `source.path`: the part before the first `.` names a source, the rest is a `PropertyPath` (`Reflection/PropertyPath.h`) of the source's object: `files.pending`, `game.fields`, `files.status.text`. A **condition** (`visible`, `enableWhen`) is a binding of a bool field with an optional leading `!`; empty is always true. A table column's `field` and `argument` are property paths **relative to the row** (the list's element; empty for a list of scalars). `ParseBinding` / `ParseCondition` check the syntax only; whether the source and the field exist is found when the panel reads, and a miss is shown as `?`, never an error. A bound field is a top-level field of a source object (`PanelField`); a nested object is reached by the model exposing it as a source of its own.
- **Limits** (`PanelAssetLimits.h`, the loader reports anything beyond them as an issue): nesting depth, elements, columns in all, sources, bytes of a text field, an `id` of 1 to 64 bytes of `[A-Za-z0-9_.-]` that is unique in the asset (column ids are unique per table), issues per load. The size of the file is the reader's (`ReaderLimits`).
- **Strings are `String`, never `PoolString`**, in every class here, in the registry and in the compiled-path cache keys: an asset is untrusted and read again at every hot reload, and the pool never frees (`Reflection/CLAUDE.md`, "Rules").

## The loader

`LoadPanelAsset(text)` / `LoadPanelAsset(IDeserializer&)` returns the asset and the `PanelIssue`s (`{where, message}`: an element id, or a path such as `elements[2].columns[0]` when the id is bad). **`issues.empty()` is the only meaning of "valid".** An asset with issues is still safe to hold and to walk, so a panel can keep drawing the last good one under a banner; `asset` is null only for a text that does not parse. No input asserts or crashes, the same text gives the same issues, issues are short with no control character and the list is bounded (the last one says that more were dropped). The deserializer runs in `DiagnosticsMode::Error` with strict values, so an unknown attribute, a bad number, a wrong enum name and an unknown `type` (with `did you mean`) are issues too. The names a file cannot verify itself (commands, models) are checked only when the caller passes `PanelValidationContext` predicates; this folder includes no editor header.

A failed hot reload of the file is `ConfigEvents::ConfigReloadFailedEvent` (`Config/CLAUDE.md`, "Hot reload"): the panel that shows the asset subscribes to it and to `ConfigChangedEvent`. `PanelAssetHolder` is the rule a host applies to both: `Load` reads the config through its deserializer under a `ValueLinkScope`, `Current()` is the last asset read with no issue and a later bad file only sets `Issues()` (the host draws a banner and the last good asset below it), a first read that failed leaves `Current()` null, and `Failure()` is the reason of an unreadable reload until the next read replaces it. It is here, not in the editor folder, so the rule is tested in every build.

## Reading a bound value

`PanelDataSources` (a `Singleton`, main thread only) maps a name to a provider that gives the source's `TypedRef` **now**, so an object that appears or dies (the active game) is found again at every read. A model registers with `Register`; the returned `PanelSourceHandle` unregisters when destroyed, and a name registered again replaces the provider and orphans the older handle. A registry may have a parent (`SetParent`): a name the own sources do not hold is looked up in the parent, so a widget keeps its models in a registry of its own (two panels may both name a source `files`) with the global one behind it.

`PanelBindingReader` reads a bool, a number, a text, a list's row count and a row's cell through a `PanelReadStatus`; any status but `Ok` is a `?` in the panel, never an assert or a crash (a gone source, a wrong path, a field of another kind, a row past the end). A path is compiled against the object's type once and cached (keyed by `TypeInfo*` and the text, bad paths too, bounded by `kMaxCompiledPaths`), so a steady frame allocates nothing: a field held by value (a bool, a number, an enum, a `String`, a `PoolString`, also inside embedded objects and list rows) is read in place. A path through an optional, a pointer or a list position, and a `FixedString` text, go through `GetProperty` and allocate: bind those rarely. (`TypeDesc::isEngineString` exists so a `String` cell can be told from an overflowing `FixedString`, which cannot be read in place.) A list sits at a fixed path of the source.

`SingleFieldVisitor` is a decorator over an `IPropertyVisitor`: it lets one top-level field (and everything inside it) through to the inner visitor and nothing else, so the inspector's own drawing (`ImGuiPropertyVisitor`) draws one bound field. A name that is no field gives no call. Keep one per widget so its text scratch keeps its capacity.

## Tests and fuzzing

`PanelAssetTest` (classes, round trip), `PanelAssetLoadTest` (issues), `PanelAssetHolderTest`, `PanelBindingTest`, `SingleFieldVisitorTest` in `tests/BECore`; the reflected probe types of the binding tests are `PanelBindingProbe.h` and `SingleFieldProbe.h`. `FuzzPanelAsset` (`tests/Fuzz/CLAUDE.md`) holds the reader's contract against a walk of the returned tree and replays `tests/Fuzz/corpus/panel_asset/`; a new kind or field needs the walk of the target extended in the same change. The generated reflection code of the classes is in the build folder (`BE_CLASS`, `.claude/rules/reflection-module.md`); a change of a field of a class that a shipped file names needs a step in `tools/migrate/steps/` and a `kContentVersion` bump (`tools/migrate/CLAUDE.md`).
