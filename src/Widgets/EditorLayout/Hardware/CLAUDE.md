# Hardware readout of the editor's status bar

What the status bar shows besides the frame time: the GPU's temperature, load and memory (`GPU 64°C 87% 9.1/12 GB`) or, when no GPU reading exists, the CPU's load and temperature (`CPU 45% 58°C`), coloured by thresholds, with a tooltip naming the device. Nothing at all when nothing can be read. The folder is editor-only: `src/CMakeLists.txt` leaves `Widgets/EditorLayout/` out of `RuntimeModule` without `BE_WITH_EDITOR`, and the tests are in the regex of `tests/CMakeLists.txt`.

## Files

| File | What |
|------|------|
| `HardwareTypes.h` | `HardwareKind`, `HardwareReadings` (plain data), `IHardwareReader`, `NormalizeReadings`, `SetHardwareName` |
| `HardwareText.*` | thresholds, `ClassifyTemperature`, `ClassifyMemory`, `FormatHardware` (readings to segments, label, tooltip), `FormatHardwareLine` |
| `HardwareMonitor.*` | `SampleHardware` (which reader wins) and `HardwareMonitor` (the worker thread, the hand-over to the frame) |
| `CpuLoad.*` | `CpuLoadTracker` (ticks to a percent) and the pure parsers of the Linux files: `/proc/stat`, `/proc/cpuinfo`, a hwmon temperature and name; every platform |
| `NvmlReader.*` | `NvmlApi` (the NVML functions as a table), `LoadNvmlApi` and `NvmlReader` (device 0); built everywhere, used on Windows and Linux |
| `HardwarePlatform.*` | `MakePlatformGpuReader` / `MakePlatformCpuReader` (nullptr where a platform has none) and `MakePlatformHardwareMonitor` |

No ImGui and no platform code in the first two: they are tested alone (`HardwareTextTest`).

## The readings and the reader contract

- `HardwareReadings` holds integers only (°C, percent, MiB), so "changed" is exact equality and the bar never formats again for noise. A value that could not be read is empty, never 0. The name is zero-filled after its terminator (`SetHardwareName`, `NormalizeReadings`), because equality compares the bytes.
- `NormalizeReadings` is the one place that decides what is absurd: load above 100 is 100, a temperature outside -20..150 °C is missing. The sampler and `FormatHardware` both apply it.
- `IHardwareReader::Read` is called by one thread only (the monitor's worker), may take milliseconds, allocates nothing in a steady call, fills everything but `kind` and returns false (or a result without a value) for "nothing to show". A reader needs no lock for its own state.

## The monitor thread

- **Why a thread:** the art pipeline runs on the same GPU, and exactly then a driver call can stall (NVML starts in hundreds of milliseconds, a query on a saturated or sleeping GPU takes tens, an IOHID read on a Mac takes milliseconds). Polled on the frame, one stall is one visible hitch. The worker (`Thread` "Hardware stats", about once a second, `kHardwarePollPeriod`) reads at once on `Start` and then waits on a `ConditionVariable` with the stop flag as the predicate, so `Stop()` does not wait for the period.
- **The frame never waits:** `Fetch()` is one atomic load when the version did not move, otherwise a `try_lock`; a failed `try_lock` is a false answer and the frame asks again next time. The worker reads **outside** the lock and holds it only to compare and store, and moves the version only when the readings differ from the published ones, so an unchanged hardware costs the frame nothing and the bar formats nothing. A test that races a fast worker must expect a false `Fetch` now and then and ask again.
- **Readers belong to the worker.** They are touched by that one thread (or by `PollOnce`, the test hook, which asserts that no worker runs), so a reader needs no lock and may keep handles from its first `Read`.
- **Shutdown:** `Stop()` (also in the destructor) sets the flag under the lock, notifies and joins; it is idempotent, and the monitor can be started again. `Start` and `Stop` are the main thread's. Where `Thread::kAvailable` is false (the single-thread build) nothing starts and the bar shows nothing.
- `SampleHardware` is the policy: the GPU reader wins when it answers true with at least one value, else the CPU reader, else `None`; the sampler sets `kind` and normalizes.

## What the bar shows

- Segment order: GPU temperature, load, memory; CPU load, temperature (the CPU's memory is never shown). A missing value leaves its segment out, and the tooltip says so (`temperature not available`).
- Memory is MiB shown as GB with one decimal for the used amount; the total is whole when within 0.05 GB of a whole number. The unit follows the total (below 1 GiB: whole MB), without a total the used amount. A zero total is an unknown one. Memory of a `unifiedMemory` reading ends the tooltip with `unified memory, shared with the system`.
- Thresholds (named constants in `HardwareText.h`): GPU 80 / 90 °C, CPU 85 / 95 °C, memory 90 / 97 % used. Load is never coloured: a saturated GPU is the normal state while the art pipeline works.
- Everything is formatted into fixed `char` buffers and cut at their size, so a steady frame allocates nothing.

## The bar

- `StatusBar` owns the monitor. `StartHardwareMonitor()` (once, from `EditorLayoutWidget::Initialize`) builds the platform's readers and starts the worker only when at least one exists, so a platform with none costs no thread and shows nothing; `SetHardwareMonitor()` is the tests' way in (not started, driven by `PollOnce`). The monitor is joined when the bar dies: a mode switch or the end of the program.
- `Refresh()` calls `Fetch()` every frame and formats (`FormatHardware`) only when it answered true. The group sits in the left half after the allocations: the device's icon (`ICON_LC_MICROCHIP` GPU, `ICON_LC_CPU` CPU) and label in the disabled colour, then each segment in the mono font in its level's colour (`Normal` the text colour, `Warning` and `Error` the palette's), every piece with the device's tooltip. Without a visible text nothing is drawn, not even a gap.

## NVML (the GPU of Windows and Linux)

- **No SDK at build time.** `NvmlApi` declares the signatures of `nvml.h` itself and `LoadNvmlApi` resolves them with `SDL_LoadObject` / `SDL_LoadFunction` (`nvml.dll`, then the `NVSMI` folder, on Windows; `libnvidia-ml.so.1` on Linux). A machine with no NVIDIA driver loads nothing and the reader says false. A test drives `NvmlReader` through a table of fake functions and loads only a bogus library name, so it passes with and without a driver.
- **The reader loads on the worker.** `NvmlReader()` loads the library and calls `nvmlInit_v2` inside its first `Read`, never in the constructor, because both take hundreds of milliseconds. A failed load, init, device count, handle or name is final: the reader remembers it and answers false at once, and nothing is loaded again (a driver installed while the editor runs is seen after a restart).
- Device 0 only. Temperature, utilization and memory are three queries, and one that fails leaves only its field missing. Memory is rounded to MiB; the memory is the card's own (`unifiedMemory` false). The destructor shuts NVML down (only after a successful init) and unloads the library.
- `CpuLoadTracker` takes cumulative ticks: the first call, an unchanged total and counters that went back give no value and make the new sample the baseline.
