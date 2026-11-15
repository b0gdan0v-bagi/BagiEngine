# Rules (BECore)

Selector rules baked into dense tables, for a game's "who x what x how" interaction tables (a piece against a piece, a verb against a kind, a combo of two specials). Written as one cell per combination such a table is a spaghetti of exceptions and grows with the square of the kinds; written as a few rules with selectors (`*`, a kind, a tag and its subtree) it stays small, but then "what happens in this cell" is hidden. So the rules are *the source* and the table is a **derived, visible artifact**: `Bake` decides every cell once, a conflict is a load error that names both rules, `Explain` says why a cell holds its value, and `ExportText` prints the whole table for a golden file whose diff is the review. Pure data: no scene, no services, no reflection (so no content-format step), no `std::` types outside the `expected` boundary.

| File | What |
|------|------|
| `TagTree.h` | hierarchical tags (`match.3`) numbered so that a subtree is an id range |
| `RuleAxis.h` | `RuleAxis` (one dimension: value names, tags per value), `RuleSelector`, selector text and specificity |
| `RuleBaker.h` | `BakeRules`: which rule wins each cell, conflicts; the non-template core |
| `RuleTable.h` | `RuleTable<Value>` (rules from selector texts), `BakedRuleTable<Value>` (lookup, `Explain`, `ExportText`) |
| `RuleReport.h` | the non-template part of the explanations and the export |

## Tags

`TagTree::Add("a.b")` creates `a` and `a.b` (idempotent; an empty segment, a leading or trailing dot, `*`, whitespace or more than `kMaxTagCount` tags is an error and changes nothing). `Bake()` numbers the tags by a depth-first preorder walk with the children in the order of their **first `Add`**: a tag and its whole subtree are the contiguous range `[id, id + subtree size - 1]` (`Range`; `DescendantRange` leaves out the tag), so "is this tag under that one" is two comparisons. Declaring `a.x`, `b`, `a.y` numbers `a=0, a.x=1, a.y=2, b=3`. This order is a contract: the ids and the order of `OfTags` values follow it. `Add` after `Bake` is a contract violation. An axis keeps its own copy of the tree.

## Axes and selectors

`RuleAxis::Named(name, valueNames[, tags])` is a dimension of named values (an element kind) whose values may carry tags (`SetTags`); `RuleAxis::OfTags(name, tree)` is a dimension whose values *are* the tags (value `i` is tag `i`, named by its full path: the "how" of an action). Value names are unique. A selector text, parsed by `ParseSelector` (spaces around it are trimmed):

| Text | Matches |
|------|---------|
| `*` | every value |
| `<name>` | the value of that name |
| `tag:<path>` | the values that carry the tag or any tag under it (`special` does not match `specialist`: a subtree, not a text prefix) |
| `tag:<path>.*` | the values that carry a tag strictly under it |

An unknown name or tag, and a selector that matches no value of the axis, are errors that name the text and the axis.

## Which rule wins a cell

Each selector has a specificity number: `*` 0, `tag:p` `2 * Depth(p)`, `tag:p.*` `2 * Depth(p) + 1`, an id `kIdSpecificity` (above every tag). So `match` < `match.*` < `match.3` < an id. A rule is the vector of its selectors' numbers, one per axis.

Rule A *dominates* B when A's number is >= B's on every axis and > on at least one. The rules matching a cell are reduced to the **maximal** ones (no other matching rule dominates them). If the maximal rules all carry the same value, the cell has it and the winner is the first declared of them; otherwise the table does not bake: a `RuleConflict` names two of the rules (origins, numbers, texts) and the first cell. That covers equal specificity with different values and **two rules neither of which dominates the other**: `(kind=bomb, verb=*)` against `(kind=*, verb=Hit)` meet in `(bomb, Hit)`, and the author writes the explicit `(bomb, Hit)` rule. Nothing is summed, so no cell is decided silently by an accident of numbers. The result does not depend on the order the rules were added in, except that the rule numbers (the position of `AddRule`, which every message and `Winner` speak) follow it. `RuleError::message` lists at most `kMaxListedRuleConflicts` conflicts and `and N more`; `conflicts` has all. A rule that wins no cell has `WonCells` 0, which is not an error. The product of the axis sizes is at most `kMaxRuleCells`.

Rules with equal values (`==`) share a value class, so two rules of the same value never conflict. `AddSymmetric(a, b, value, origin)` adds the rule and its mirror (the first two selectors swapped; the mirror's text ends with `(mirrored)`) for tables whose first two axes have one domain.

## The baked table

`Lookup(ids...)` is one multiply-add chain and one index into a dense `Vector<Value>`: no hash, no branch on the kind of rule; the number and range of the ids are `ASSERT`ed. A cell no rule matches holds the default value given to `Bake`. The cells are row-major over the axes in order, the last axis fastest (`CellIndex`, `LookupCell`). `Winner(ids...)` is the rule number or empty.

**Explain.** `Explain(ids...)` gives `CellExplanation {winner, overridden}`: the winning rule and every other matching rule, each with number, origin and text. `ExplainText(valueToText, ids...)` is the one-line form (the callable comes first because the ids are a pack):

```
stone x Hit x blast = <value text>  <- #12 [<origin>] kind=stone verb=Hit tag=*; overridden: #3 [<origin>] kind=tag:blocker verb=* tag=*
```

Several overridden rules are joined with `, `; a cell no rule won ends with `<- default`; a newline in the value text is written `\n`. A rule's text is `<axis>=<selector>` per axis, and an empty origin or text is left out.

## The export format (pinned)

`ExportText(name, valueToText, options)` returns the table for a golden file; `valueToText` is any callable `String(const Value&)`, called once per distinct value and for the default. A game's snapshots depend on the format, so **a change needs a note here**.

```
# rules <name>: <axis>[<count>] x <axis>[<count>] x ...
# default: <value text>
<value name of axis 0> | <value name of axis 1> | ... = <value text>
```

One line per cell **a rule won**, in row-major order; every line, the last too, ends with `\n`. A newline in a value text (also the default's) is written as the two characters `\n`; nothing else is escaped. Nothing depends on the machine or on the order the rules were added in. With `options.withRules` each cell line ends with ` <- #<rule> <origin>` (no origin: ` <- #<rule>`); it is off by default because rule numbers move when a rule is added and a golden file wants the table only.

## How a game uses it

One `RuleTable` per interaction table, built from the game's own content (a loader that passes selector texts and an origin such as `file:line`). A baked table is what the game looks up at run time. The game's test bakes the table, `ExportText`s it and compares with a golden file in its `tests/`; a changed rule shows up in review as the diff of exactly the cells it wins. A load error (`RuleError::message`) goes to the author as it is.

Left out: reading rules from XML or another content format (the game's loader), ordered first-match rows with conditions, the linter extras (unused tags, `Transform` cycles, level analysis) and a diff tool.

Tests: `tests/BECore/TagTreeTest.cpp`, `RuleAxisTest.cpp`, `RuleBakerTest.cpp`, `RuleTableTest.cpp`, `RuleReportTest.cpp`; the helper `RuleTestSupport.h` has a brute-force winner written from the definition and a 100 x 8 x 20 fixture of about 300 rules.
