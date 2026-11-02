---
status: review 2026-11-02 branch todo/config-editor-the-sink-type-dropdown-offers-values-the: the sink type dropdown lists the factory's class names and shows the current value, unknown sink types are logged as errors, edit-save-reload is covered by a round-trip test; 1194 of 1194 ctest tests pass
id: config-editor-the-sink-type-dropdown-offers-values-the
---
# Config Editor: the sink `type` dropdown offers values the logger factory cannot resolve

**Where:** `src/Widgets/ConfigEditor/ConfigEditorWidget.cpp:22` (`schema.RegisterEnum<LogSinkType>("LoggerConfig"_intern, "sink/@type")`), `ConfigFieldSchema.h` (`RegisterEnum`), `FieldInspectors.cpp` (`RenderEnum`), `src/Modules/BECore/Logger/LogSinkType.h`.

**What is wrong** (found by reading the code, not reproduced in a running editor):

1. The dropdown options come from `EnumUtils<LogSinkType>::Names()`: `Console`, `File`, `Output`. `config/LoggerConfig.xml` stores the sink **class names**: `ConsoleSink`, `FileSink`, `OutputSink`. `RenderEnum` looks for the current value among the options, finds nothing, `selectedIndex` stays `-1` and the combo shows no current selection.
2. Choosing an option and saving writes `type="Console"` into the file. On the next load `LoadElement` (`Reflection/DataAccessor.h`) creates the sink with `AbstractFactory<ILogSink>::Create(typeString)`, which looks the type up by the hash of the class name. `"Console"` is not registered, the result is null, and `ILogSink` is abstract, so the entry is dropped without any message. If every entry is dropped, `LoggerManager` silently falls back to a `ConsoleSink`.
3. `LogSinkType` is defined twice: by hand in `BECore/Logger/LogSinkType.h` (`Console, File, Output`) and by the generator in `build/Generated/EnumLogSink.gen.hpp` (`File, Console, Output`, different order). Only the handwritten one is included anywhere; including both in one translation unit would be a redefinition, and the value order already disagrees.

**A fix has to cover:**

- the dropdown lists exactly the strings the factory accepts, and shows the current value as selected;
- one definition of the sink type enum (or none: the editor can take the registered class names from `AbstractFactory<ILogSink>::GetRegisteredTypes()`);
- a visible error when a config names a sink type that is not registered, instead of dropping the entry silently;
- a check that edit → save → restart keeps every sink working (a round-trip test of `LoggerConfig` through the editor's save path, or at least through `ConfigManager::SaveConfig` / `ReloadConfig`).

The same pattern (an enum hint whose names differ from what the loader expects) can exist for other registered fields: `LogLevel` for `sink/@minLevel` is fine because `minLevel` is read through `EnumUtils`, but check any new `RegisterEnum` call against the loader that reads the attribute.
