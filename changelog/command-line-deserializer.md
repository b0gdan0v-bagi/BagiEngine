# The command line is read by reflection: a CommandLineDeserializer over --name=value flags

- Version: 0.18.0
- Entry: command-line-deserializer
- Date: 2026-11-24
- Bump: minor

## Added
- `CommandLineDeserializer` (`BECore/Application/CommandLineDeserializer.h`): an `IDeserializer` over the `--name=value` flags of a `CommandLine`, so a reflected struct is filled from the command line by its field names in kebab case, with the strict grammar of the config (`BECore/Reflection/StrictText.h`, now shared with `XmlDeserializer`); an argument near to a known flag (`--contol-port`) is a warning. `CommandLine::Find(name)` gives the first `--name` or `--name=value`. Adapt: nothing; a game with flags of its own can read them into a `BE_CLASS` the same way.

## Changed
- The control flags are read through it over a copy of the file's `ApplicationConfig` and the new reflected `ControlFlags` (`--replay`, `--replay-exit`, `--window`, `--density`), so each flag overrides one field and an absent flag leaves the file's value; a mistyped control flag is logged as `Config 'Command line': ...`. Adapt: nothing.

## Breaking
- A boolean control flag takes a value: `--control=false`, `--headless=0`, `--manual-clock=false` were ignored (only the bare flag counted) and now switch off what the config turned on, and `--control=1` turns it on; a value that is not `true`, `false`, `1` or `0` (`--control=maybe`) was ignored and now stops the start-up with an error. Adapt: a launcher or script that passes such a flag with a value checks that the value is one of the four.
- The flag constants of `BECore/Control/ControlSettings.h` name the flag, not its prefix: `kControlPortPrefix`, `kReplayPrefix`, `kWindowPrefix`, `kDensityPrefix` are `kControlPort`, `kReplay`, `kWindow`, `kDensity` (`--control-port`, without `=`). Adapt: use the new name and `CommandLine::Find`; no game uses them today.
