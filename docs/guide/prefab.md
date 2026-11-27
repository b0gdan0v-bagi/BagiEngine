# A prefab and its overrides

You will write a node once, put three copies of it in a scene, change what is different about each, and then change the original and watch all three follow. Last, you will save from the editor and see that the file keeps only what differs. Every block below is an excerpt of the example folder `docs/guide/examples/prefab/`, so you can open its files next to this page.

You need the build from [Run the demo](run-the-demo.md) and the habit of the overlay folder from [Your first scene](first-scene.md). No C++ is written here: a prefab is data.

## What a prefab is here

If you know Unity or Godot, you are looking for a prefab or a packed scene. The engine has the same thing under another name: a **node file**. It is a node kept in a file of its own, and a scene refers to it by name with `<node ref="nodes/Badge"/>`. No file or doc of the engine says "prefab", so this page uses the word for the idea and "node file" for the thing.

Why use one:

- You write the node once. Change the file and every instance in every scene follows.
- An instance says only what differs from the file, so a scene with fifty badges is fifty short lines, not fifty copies of the same components.

## Copy the example and run it

The example is a folder laid out like a game's content. Copy it into a folder of your own and give that folder to the demo:

<!-- command -->
```bash
mkdir -p $HOME/bagi-prefab
cp -r docs/guide/examples/prefab/config $HOME/bagi-prefab/
build/macos-debug/bin/BagiDemo --content-overlay=$HOME/bagi-prefab
```

![Three bars on a dark background, one under another: two blue ones labelled One and Two and an orange one labelled Three](img/prefab.png)

(The picture is taken with `python3 tools/docs/guide_shots.py --only prefab`.) Three bars, one under another. Two are blue, the third is orange, and each has its own word. They are three instances of one file.

## The prefab file

The file is `docs/guide/examples/prefab/config/nodes/Badge.xml`, and in a game it is `nodes/Badge.xml` inside `config/`. It holds one node, written as you would write it inside a scene:

<!-- source: docs/guide/examples/prefab/config/nodes/Badge.xml -->
```xml
<?xml version="1.0"?>
<node contentVersion="6" name="Badge">
  <components>
    <component type="TransformComponent" x="0" y="0" width="220" height="44" />
    <component type="QuadRendererComponent">
      <color r="70" g="110" b="190" a="255" />
    </component>
    <component type="TextComponent" text="Badge" fontSize="32" align="Center" wrap="false">
      <color r="255" g="255" b="255" a="255" />
    </component>
  </components>
</node>
```

From `docs/guide/examples/prefab/config/nodes/Badge.xml`. It is a bar of 220 by 44 units: a blue quad and a white word on it, centred. The components are the ones of the first scene. The file carries `contentVersion` like every content file, and a tool of `tools/migrate/` keeps it current when the format changes.

## Three instances in the scene

The scene, `config/SceneConfig.xml`, has the `Sky` of the first scene and three instances:

<!-- source: docs/guide/examples/prefab/config/SceneConfig.xml -->
```xml
<node ref="nodes/Badge" name="BadgeOne">
  <components>
    <component type="TransformComponent" x="100" y="100" />
    <component type="TextComponent" text="One" />
  </components>
</node>
<node ref="nodes/Badge" name="BadgeTwo">
  <components>
    <component type="TransformComponent" x="100" y="180" />
    <component type="TextComponent" text="Two" />
  </components>
</node>
<node ref="nodes/Badge" name="BadgeThree">
  <components>
    <component type="TransformComponent" x="100" y="260" />
    <component type="TextComponent" text="Three" />
    <component type="QuadRendererComponent">
      <color r="200" g="110" b="40" a="255" />
    </component>
  </components>
</node>
```

From `docs/guide/examples/prefab/config/SceneConfig.xml`.

`ref` is the file's name without `config/` and without `.xml`. Everything the instance writes is an **override**, and the rules are short:

- `name` and `layer` on the instance replace the file's. The example gives each instance a `name` of its own, so the three can be told apart.
- A `<component type="...">` is merged into the file's component of that type. An attribute you write replaces the file's value (or adds one the file did not have). Every attribute you leave out stays as the file has it: the three bars are 220 units wide, with a font of 32, because no instance says otherwise.
- A part written as child elements, such as `<color .../>`, replaces the file's whole, not field by field. The third bar gives all four numbers of its colour for that reason.
- The instance cannot add a component or a child node. It can only change the ones the file has.

That last rule is checked. Say in `BadgeOne` that it overrides a component the file does not have, for example swap its `TextComponent` line for `<component type="SpriteRendererComponent" spriteId="DemoGem" />`, and run. The log says:

<!-- illustration: the error the engine writes for an override of a component the node file lacks; the time of day is whenever you run it -->
```text
[Error] [12:00:00.000] Config 'SceneConfig': scene 'DemoScene': node reference 'nodes/Badge': node file has no component 'SpriteRendererComponent'
```

The engine does not stop. It drops the **whole** instance, so `BadgeOne` is gone, and loads the rest: the sky and the other two bars stay. The same happens for a repeated component, an unknown attribute on the reference and a node file it cannot find. Put the line back and the bar returns.

## Change the prefab, and all three follow

In your copy open `$HOME/bagi-prefab/config/nodes/Badge.xml` and change the quad's colour to a green:

<!-- illustration: the edit in the copy of Badge.xml; the line is the quad's colour, the numbers are any colour you like -->
```xml
<color r="60" g="160" b="90" a="255" />
```

Run the demo again. `BadgeOne` and `BadgeTwo` are green now, with their words and places unchanged, and `BadgeThree` is still orange: it overrides the colour, so the file's colour never reaches it. You edited one file and two bars changed.

## Save writes only what differs

Start the editor on your copy:

<!-- command -->
```bash
build/macos-debug/bin/BagiDemo --editor --content-overlay=$HOME/bagi-prefab
```

Open `Root` in the hierarchy of the Scene Editor tab. The three bars are there. Select `BadgeOne`, change `x` of its `TransformComponent` in the inspector, and press `Save`. The editor writes the scene back into the copy, and it writes an instance as a `<node ref>` with the overrides that differ from the file. It does not write the file's components into the scene again.

The block below was not made in the editor's window. It is from a run that made two changes, `x` of `BadgeOne` to 300 and the font size of `BadgeTwo` to 40, through the control API call `scene_set`, and then saved with `scene_save`. That call runs the same writer as the `Save` button. After it:

<!-- command -->
```bash
diff -r docs/guide/examples/prefab/config $HOME/bagi-prefab/config
```

<!-- illustration: the output of that diff after the two changes and one save; the paths are the ones of the run -->
```text
diff -r docs/guide/examples/prefab/config/SceneConfig.xml $HOME/bagi-prefab/config/SceneConfig.xml
8c8
<             <component type="TransformComponent" x="0" y="0" width="1280" height="720" />
---
>             <component type="TransformComponent" width="1280" height="720" />
16c16
<             <component type="TransformComponent" x="100" y="100" />
---
>             <component type="TransformComponent" x="300" y="100" />
23c23
<             <component type="TextComponent" text="Two" />
---
>             <component type="TextComponent" text="Two" fontSize="40" />
29d28
<             <component type="TextComponent" text="Three" />
32a32
>             <component type="TextComponent" text="Three" />
```

Read it from the top:

- `nodes/Badge.xml` is not in the output: it is the same. Save never writes a node file.
- Lines 16 and 23 are the two changes. `BadgeOne` now overrides `x` with 300; `BadgeTwo` got a `fontSize` override and nothing else. The other instances are as they were.
- Lines 8 and the last two are Save's own tidying, not your change. Save writes only values that differ from the default of their field, so `x="0" y="0"` of the sky is gone (both are zero anyway). It also writes the components of an instance in the order of the node file, so the `TextComponent` of the third bar moved below its colour. The file is its normal form, as in [Run the demo](run-the-demo.md), not a byte-equal copy of what you typed.

An instance that no longer fits its file is written out in full instead, with a warning in the log: for example one that gained a component the file lacks. The data is kept, but the link to the file is not, so a later change of the file no longer reaches it.

## Where the file is found

The reference `nodes/Badge` is looked up as `nodes/Badge.xml` in the `config` folders of the content layers, from the top one down, and the first layer that has the file wins. Your overlay is the top layer, so your file wins over a file of the same name below it, the same way a scene file does. A game keeps its node files in a `nodes` folder of its own `config`.

A release build does not read these files at run time. The cook expands every `<node ref>` into a plain node and checks the file's version, so a mistake like the one above fails the cook and never reaches a player (`src/Modules/BECore/Cook/CLAUDE.md`).

## What you did

You wrote a node once, used it three times, and said only what differs: a place and a word each, and a colour for the third. You changed the file and saw the instances follow, you broke an override and saw the engine drop that one instance and keep going, and you saw Save write back only the differences.

Next: [A tween and a timeline](tween-and-timeline.md), which move and fade a node like these bars.
