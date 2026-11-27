# Config and reflection

You will follow one field from the C++ class that declares it to the XML file, the editor's inspector and the control API, and see that the three views are one declaration. Then you will give a game a config file of its own, see what the log says about a typo, and change a config file while the game runs.

You need the build from [Run the demo](run-the-demo.md) and the example of [A tween and a timeline](tween-and-timeline.md): its folder `docs/guide/examples/tween/` is the one scene this page uses. Reading the C++ needs no tools; you type no C++ into the engine.

## What reflection is here

You mark a class and its fields. The build then writes the code that reads the fields from XML, writes them back, shows them in the inspector, finds them by name and describes them to tools. You write the field once; nothing else has to be kept equal by hand. One marked field gives:

- the **XML attribute** of its name, read at load and written by Save;
- a **row in the editor's inspector**, with the slider limits of its `range` and the hint of its `tooltip`;
- **`get_property` and `set_property`** of the control API, and `scene_get` and `scene_set` for a component of a node: the same name, the same limits;
- an entry in the **schema** (`describe_type`, `schema`): the tools and the agents that edit your files read the field list from the engine, not from a copy.

## The marks

The demo's `DemoTweenComponent` from the tween page is a small class that carries them all:

<!-- source: tools/demo/src/Demo/Components/DemoTweenComponent.h -->
```cpp
class DemoTweenComponent : public IComponent {
    BE_CLASS(DemoTweenComponent, ICON = move)
...
    BE_REFLECT_FIELD BE_FIELD_ATTRS(range(-1280, 1280), tooltip("How far to the right the far end is, in scene units")) float _offsetX = 0.0f;
...
    BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0.1, 30), step(0.1), tooltip("Seconds of one way")) float _duration = 1.0f;
...
    BE_REFLECT_FIELD BE_FIELD_ATTRS(tooltip("The curve of one way")) EaseType _ease = EaseType::SineInOut;
```

From `tools/demo/src/Demo/Components/DemoTweenComponent.h`. What each mark does:

- `BE_CLASS(Name)` makes the class known by that name: the XML says `type="DemoTweenComponent"`, and the control API says the same. The options after it are optional: `ICON = move` is the icon of the component in the editor.
- `BE_REFLECT_FIELD`, on the same line as the field, makes it a part of the class's data. A field without it is not saved, not shown and not found by name.
- `BE_FIELD_ATTRS(...)` is what you tell the tools about the field: `range(min, max)` limits the value (the inspector's slider, and a write over the control API outside it is refused), `step(s)` is the slider's step, `tooltip("...")` is the hint. There are also the flags `readonly`, `hidden` and `required`.
- **The name drops the underscore.** A private member is `_offsetX`, and its name in the file and in the control API is `offsetX`. The file of the tween page has `offsetX="640"` and no `_offsetX`.
- A field may be a number, a `bool`, a text (`String`, `PoolString`), an enum declared with `CORE_ENUM` (the file holds its value's name, `ease="QuadInOut"`), another reflected class, or a `Vector` or `HashMap` of these. The other containers (`Optional`, `Variant`, the fixed ones, the other maps) follow their own rules, and a few have none: the table is in `.claude/rules/reflection-module.md` ("Serialization").

After you add or change a `BE_CLASS` or a field, run the configure step again. The generator runs there, and the sources of the build are found by a glob, so a new file needs it too:

<!-- command -->
```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
```

The generated code for the class lands in `build/macos-debug/Generated/DemoTweenComponent.gen.hpp`. Never edit it: the next configure writes it again.

## One field, three views

Take `offsetX` of the slider. In the **file** it is an attribute of the component:

<!-- source: docs/guide/examples/tween/config/SceneConfig.xml -->
```xml
<node name="Slider">
  <components>
    <component type="TransformComponent" x="160" y="170" width="72" height="72" />
    <component type="SpriteRendererComponent" spriteId="DemoStar" />
    <component type="DemoTweenComponent" offsetX="640" duration="2" ease="QuadInOut" />
  </components>
</node>
```

In the **editor** it is a row. Start the demo with the example and `--editor`, select the node `Slider` in the hierarchy and open its `DemoTweenComponent`:

<!-- command -->
```bash
mkdir -p $HOME/bagi-reflection
cp -r docs/guide/examples/tween/config $HOME/bagi-reflection/
build/macos-debug/bin/BagiDemo --editor --content-overlay=$HOME/bagi-reflection
```

![The editor with the node Slider selected: its DemoTweenComponent is open and shows the rows offsetX, offsetY, turn, duration, delay and ease; a hint over offsetX reads how far to the right the far end is, in scene units, with its type, range -1280 to 1280 and default 0](img/reflection-inspector.png)

(The picture is taken with `python3 tools/docs/guide_shots.py --only reflection-inspector`. The mouse rests on the `offsetX` row.) Each field of the class is a row, in the order of the class. The number fields with a `range` are sliders between its two ends, `ease` is a list of the values of the enum, and the hint of `offsetX` is the `tooltip` text with the type, the range and the default under it. A row that differs from the default has a small reset arrow before it.

Over the **control API** it is a path. This script starts the demo with a copy of the example, reads the field, writes it and tries a value outside its range:

<!-- illustration: a script that exists in no file; it is the Python session of tools/control (the control API calls scene_get and scene_set) -->
```python
import os
import shutil
import sys
import tempfile

sys.path.insert(0, "tools/control")
from bagi_control_client import ControlError
from bagi_session import GameSession

scratch = tempfile.mkdtemp(prefix="bagi-reflection-")  # the game writes into its overlay, so it gets a copy
overlay = os.path.join(scratch, "content")
shutil.copytree("docs/guide/examples/tween", overlay)
session = GameSession()
try:
    session.launch(exe="build/macos-debug/bin/BagiDemo", content_overlay=overlay)
    session.step(2)
    field = {"node": "Slider", "component": "DemoTweenComponent", "path": "offsetX"}
    print(session.call("scene_get", field)["value"])
    print(session.call("scene_set", {**field, "value": 300})["value"])
    try:
        session.call("scene_set", {"node": "Slider", "component": "DemoTweenComponent", "path": "duration", "value": 1000})
    except ControlError as error:
        print(error)
finally:
    session.close()
    shutil.rmtree(scratch, ignore_errors=True)
```

Save it as a file and run it from the root of the repository with `python3`. It prints:

<!-- illustration: the three lines the script prints -->
```text
640.0
300.0
out_of_range: path 'duration': 1000 is outside the range 0.1..30
```

The write of 300 returns the value read back, and the slider now moves 300 to the right; it starts over from its own place, as the tween page says. The write of 1000 is refused, with the range of the field, and changes nothing: the same `range` that limits the slider limits the API. `describe_type` with `{"name": "DemoTweenComponent"}` answers the whole field list, with the types, ranges and tooltips; that is what a tool or an agent reads before it edits a file.

## A config file of your own

A **config** is a file `config/<Name>.xml` of a game, read by name. The engine's own settings are such files (`ApplicationConfig.xml`, `UIStrings.xml`), and a game adds its own. The recipe: a reflected struct, a file, one line to read it.

The struct. It is a plain reflected class: the attributes of the file's root element are its fields.

<!-- source: tests/BECore/GuideConfigExample.h -->
```cpp
CORE_ENUM(SpawnShape, uint8_t, Ring, Line, Scatter)

/// The settings of a spawner: the root attributes of `config/GuideSpawnerConfig.xml`, the file's name without `.xml` is the config's name
struct GuideSpawnerConfig : RefCounted {
    BE_CLASS(GuideSpawnerConfig)

    BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0.1, 30), step(0.1), tooltip("Seconds between two spawns")) float _interval = 1.0f;
    BE_REFLECT_FIELD BE_FIELD_ATTRS(range(1, 100), tooltip("How many pieces one spawn makes")) int32_t _count = 5;
    BE_REFLECT_FIELD bool _enabled = true;
    BE_REFLECT_FIELD SpawnShape _shape = SpawnShape::Ring;
    BE_REFLECT_FIELD String _label;
};
```

From `tests/BECore/GuideConfigExample.h`. The code of this and the next two blocks is compiled into the engine's tests and run there (`GuideConfigExampleTest`), so it does what the page says. The struct is a `RefCounted`, which is the shape `MakeFromConfig` makes. The file is `docs/guide/examples/config/config/GuideSpawnerConfig.xml`:

<!-- source: docs/guide/examples/config/config/GuideSpawnerConfig.xml -->
```xml
<GuideSpawnerConfig interval="0.5" count="8" enabled="true" shape="Line" label="first wave"/>
```

The read. In a game it is one line, `MakeFromConfig<GuideSpawnerConfig>("GuideSpawnerConfig")`, which finds the file by its name, reads the fields and logs what is odd. The example spells the same steps out, down to the scope that `Value` fields of a config read under, over a manager that you pass in, so that a test can point it at a folder of its own:

<!-- source: tests/BECore/GuideConfigExample.h -->
```cpp
inline IntrusivePtr<GuideSpawnerConfig> ReadGuideSpawnerConfig(const ConfigManager& configs) {
    auto config = New<GuideSpawnerConfig>();
    if (auto deserializer = configs.CreateDeserializer(GuideSpawnerNames::config)) {
        {
            const ValueLinkScope scope(GuideSpawnerNames::config.View());
            config->Deserialize(*deserializer);
            deserializer->FinishLoad();
        }
        LogDeserializeErrors(GuideSpawnerNames::config.View(), *deserializer);
    }
    return config;
}
```

From `tests/BECore/GuideConfigExample.h`. The file is found **by name** at the next start: drop a file `GuideSpawnerConfig.xml` into the `config` folder of your game and nothing else is registered. A game's file of the same name as an engine file replaces the engine's file as a whole (configs are not merged), so give your own configs names of their own. A config the Config Editor of the editor shows as typed rows (combos for an enum, number fields) is registered once more for it (`src/Modules/BECore/Cook/CLAUDE.md`, "Registering a config"); without that the editor shows the file as text, and the game reads it all the same.

A typo does not fail silently. Write `inteval` for `interval` and `count="800"` for `count="8"`, and the read logs two warnings (the test pins these exact lines):

<!-- illustration: the two warnings of the read for the file with a misspelled key and a count outside its range; the time of day is whenever it runs -->
```text
[Warning] [12:00:00.000] Config 'GuideSpawnerConfig': inteval: Unknown attribute 'inteval', did you mean 'interval'?
[Warning] [12:00:00.000] Config 'GuideSpawnerConfig': count: Value 800 is outside the range [1, 100]
```

The line names the config, the place in the file and what is wrong. The game still starts: the misspelled key leaves `interval` at its default of 1, and the value outside the range is kept as read (800). To make such a line fail a run, start the game with `--strict-config`: the findings are then errors in the log. A game's test can check all of its files the same way (`ExpectConfigFilesClean`, `src/Modules/BECore/Config/CLAUDE.md`).

## Change a config while the game runs

In a Debug build and in the editor the engine watches the `config` folder. The setting is `fileWatch` of `ApplicationConfig.xml`: `Auto` (the default) watches in the editor and in Debug builds, `On` and `Off` are as written, and the web build never watches. A game started with it says `File watching: on` once in its log.

Try it on the running demo. Start it as above, with `--editor` and the copy in `$HOME/bagi-reflection`, then open `$HOME/bagi-reflection/config/SceneConfig.xml` in your editor of text, change `offsetX="640"` to `offsetX="300"` and save. Within a second or two the log has

<!-- illustration: the log line of a reload; the time of day is whenever it happens -->
```text
[Info] [12:00:00.000] [ConfigManager] Reloaded config 'SceneConfig'
```

and the corner of the editor shows a toast, `Config reloaded: SceneConfig`. The engine read the file again. **The scene that is running did not change**: `scene_get` still says `offsetX` is 640, and the star still goes to 800. The manager holds a new document, and a scene that was built from the old one keeps what it built; it reads the new file the next time the scene loads, for example at the next start. A config that something follows, as the next section shows, does change at once.

Now break the file: delete the last `>` of `SceneConfig.xml` and save. The log has an error and the toast reads `Config reload failed: SceneConfig`:

<!-- illustration: the error of a file that does not parse; the path is the file of your copy and the time is whenever it happens -->
```text
[Error] [12:00:01.000] [ConfigManager] ReloadConfig: failed to reload 'SceneConfig' from /Users/you/bagi-reflection/config/SceneConfig.xml
```

The engine **keeps the old document** and says so; a file that does not parse is a failure and not a change, so nothing that follows the config sees it. Put the `>` back and the next save reloads. When the engine's own editor saves a config, the file changes on disk too, and the watcher would read it again: the manager remembers the hash of the text it read and acts only when the text differs, so its own save is not read twice (`src/Modules/BECore/Config/CLAUDE.md`, "Hot reload and `ConfigChangedEvent`"). The Config Editor, if the changed config is the one it has open, drops its unsaved edits and says "changed on disk" (`src/Widgets/ConfigEditor/CLAUDE.md`).

### A class of your own follows the file

A reload ends in an event, `ConfigEvents::ConfigChangedEvent` with the name of the config and a cause (`Saved`, `Reloaded` for a change on disk, `Edited`). A class that holds a config subscribes to it and reads the file again:

<!-- source: tests/BECore/GuideConfigExample.h -->
```cpp
class GuideSpawnerSettings : public SubscriptionHolder {
public:
    explicit GuideSpawnerSettings(const ConfigManager& configs) : _configs(configs), _config(ReadGuideSpawnerConfig(configs)) {
        Subscribe<ConfigEvents::ConfigChangedEvent, &GuideSpawnerSettings::OnConfigChanged>(this);
    }
...
private:
    void OnConfigChanged(const ConfigEvents::ConfigChangedEvent& event) {
        if (event.name == GuideSpawnerNames::config) {
            _config = ReadGuideSpawnerConfig(_configs);  // a node or a deserializer taken before this event is stale
        }
    }
```

From `tests/BECore/GuideConfigExample.h`. `GuideConfigExampleTest` changes the file on disk and sees the class hold the new count, writes a file that does not parse and sees the old value stay with no change event, and saves through the manager and sees one event, not two. Two traps in plain words:

- **What you took before the event is stale.** An `XmlNode` or a deserializer that you got from the manager before the event points into the old document. Read again in the handler, as above; do not keep them.
- **The event comes on the main thread**, after the manager released its lock, so the handler may call the manager and may change the game. It is not called from the thread of the file watcher.

## What you did

You followed `offsetX` from its `BE_REFLECT_FIELD` to the attribute of the scene file, the row of the inspector and the path of the control API, and saw that the `range` is one rule in all three. You read the three parts of a config of your own, a struct, a file and a read by name, and the warning of a typo. You changed a config while the game ran, and saw what the engine does for a good file (reads it, tells you, leaves the running scene alone) and for a broken one (keeps the old one and tells you), and how a class of yours follows.

Next: [Input actions](input-actions.md), how a press becomes a named action that the game answers.
