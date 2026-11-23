# Control JSON: GetJsonIntValue reads an integer that is not a member

- Version: 0.16.2
- Entry: control-json-int-item
- Date: 2026-11-23
- Bump: patch

## Added
- `GetJsonIntValue(const Json&)` (`BECore/Control/Json.h`): the integer of a JSON value that is not an object member, such as an array item, with the rules of `GetJsonInt` (1.5, "3", a bool and an unsigned above `INT64_MAX` are not integers); `GetJsonInt` uses it. Adapt: nothing; a game's own copy of that check can call it.
