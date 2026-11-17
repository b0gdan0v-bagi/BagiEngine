# Hardware readout of the editor's status bar

What the status bar shows besides the frame time: the GPU's temperature, load and memory (`GPU 64°C 87% 9.1/12 GB`) or, when no GPU reading exists, the CPU's load and temperature (`CPU 45% 58°C`), coloured by thresholds, with a tooltip naming the device. Nothing at all when nothing can be read. The folder is editor-only: `src/CMakeLists.txt` leaves `Widgets/EditorLayout/` out of `RuntimeModule` without `BE_WITH_EDITOR`, and the tests are in the regex of `tests/CMakeLists.txt`.

## Files

| File | What |
|------|------|
| `HardwareTypes.h` | `HardwareKind`, `HardwareReadings` (plain data), `IHardwareReader`, `NormalizeReadings`, `SetHardwareName` |
| `HardwareText.*` | thresholds, `ClassifyTemperature`, `ClassifyMemory`, `FormatHardware` (readings to segments, label, tooltip), `FormatHardwareLine` |

No ImGui and no platform code in these two: they are tested alone (`HardwareTextTest`).

## The readings and the reader contract

- `HardwareReadings` holds integers only (°C, percent, MiB), so "changed" is exact equality and the bar never formats again for noise. A value that could not be read is empty, never 0. The name is zero-filled after its terminator (`SetHardwareName`, `NormalizeReadings`), because equality compares the bytes.
- `NormalizeReadings` is the one place that decides what is absurd: load above 100 is 100, a temperature outside -20..150 °C is missing. The sampler and `FormatHardware` both apply it.
- `IHardwareReader::Read` is called by one thread only (the monitor's worker), may take milliseconds, allocates nothing in a steady call, fills everything but `kind` and returns false (or a result without a value) for "nothing to show". A reader needs no lock for its own state.

## What the bar shows

- Segment order: GPU temperature, load, memory; CPU load, temperature (the CPU's memory is never shown). A missing value leaves its segment out, and the tooltip says so (`temperature not available`).
- Memory is MiB shown as GB with one decimal for the used amount; the total is whole when within 0.05 GB of a whole number. The unit follows the total (below 1 GiB: whole MB), without a total the used amount. A zero total is an unknown one. Memory of a `unifiedMemory` reading ends the tooltip with `unified memory, shared with the system`.
- Thresholds (named constants in `HardwareText.h`): GPU 80 / 90 °C, CPU 85 / 95 °C, memory 90 / 97 % used. Load is never coloured: a saturated GPU is the normal state while the art pipeline works.
- Everything is formatted into fixed `char` buffers and cut at their size, so a steady frame allocates nothing.
