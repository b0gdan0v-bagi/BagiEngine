# Editor: the status bar does not show the GPU temperature, load and memory (or the CPU's when there is no GPU reading)

- Version: 0.3.2
- Entry: editor-hardware-stats
- Date: 2026-11-17
- Bump: patch

## Added
- The editor's status bar shows the GPU temperature, load and memory (`GPU 64°C 87% 9.1/12 GB`), coloured by thresholds, with the device in the tooltip; without a GPU reading it shows the CPU load and, where the OS gives it without admin rights, its temperature. Readings are polled about once a second on a worker thread (`Widgets/EditorLayout/Hardware/`); the frame never blocks and a steady frame allocates nothing.
- Readers: NVIDIA through NVML loaded at run time (Windows, Linux; no SDK at build time); macOS through IOKit (`IOAccelerator` statistics, IOHID sensors on Apple Silicon, the SMC on Intel); the CPU through `GetSystemTimes`, `/proc/stat` and hwmon, `host_statistics`. The macOS and Linux readers are not yet built on their platforms. Adapt: nothing; macOS editor builds link IOKit and CoreFoundation.
