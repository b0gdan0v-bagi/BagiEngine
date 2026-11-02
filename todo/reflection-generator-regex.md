---
stage: 5
id: the-reflection-generator-is-regex-based
---
# The reflection generator is regex-based

**Where:** `CI/meta_generator/` (`core/parser.py`, `core/generator.py`, about 2000 lines of Python), `CI/meta_generator/reflection_targets.json`.

**What is wrong** (found by reading the code and the entry [Config Editor: the sink `type` dropdown](config-editor-sink-type.md), not by fuzzing the parser): the parser matches `BE_CLASS` / `BE_REFLECT_FIELD` with regular expressions, so unusual syntax (macros in declarations, nested templates, attributes) can be missed or misparsed without an error. The duplicate `LogSinkType` in that entry is one example of generated and handwritten code disagreeing without a warning.

**A fix has to cover** (not urgent while the generator copes):

- a warning when the generator skips a marker it could not parse;
- a regression suite of tricky declarations for the parser;
- an evaluation of libclang instead of regular expressions, only if the first two do not hold.
