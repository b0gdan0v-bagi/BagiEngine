# Nodes: a formula written as a tree of objects

The tree form of a computed value (`<damage><tree type="CalcByLevel" table="FireballDamage"><index type="CalcRead" path="ability.level"/></tree></damage>`): reflected classes with the factory base `CalcNode`, for an editor or a generator that builds a formula as data instead of writing its text. The text form is `Values/Programs/CLAUDE.md`; loading is `Values/CLAUDE.md`, "Loading".

## One pipeline

A tree has no rules of its own. `BuildFormulaText(root)` adds each node to an untyped `ExprAst` (`Append`, operands first, as the parser does) and prints it with `PrintExpression`; that text then goes through the parser, the checker and the lowering of an `expr`. The parser stays the only validator of numbers, names and strings, an error of the checker has the columns of the printed text, and the program's source is that text. A tree is saved as the text it prints.

## The classes

One class per shape of the AST, not per operator, so a new operator is a row of `ProgramOperation` and no class: `CalcNumber{text}`, `CalcBool{value}`, `CalcText{text}`, `CalcRead{path}` (a path or an enumerator), `CalcUnary{op, operand}`, `CalcBinary{op, left, right}`, `CalcCall{function, args}` (every function of the language, `at` for an inline table), `CalcMember{base, name}` and `CalcByLevel{table, index}`. The elements of `args` are `<node type="...">`, the element name of the base (`ELEMENT = node`). The `Calc` prefix is there because the registry knows a class by its simple name, which `Number` or `Read` would share with the rest of the engine.

A leaf prints its text as it is, so the build checks that it lexes as the one token the node stands for (`1 + 2` as a `CalcNumber` or `a' + 'b` in a `CalcText` would print as another tree): the reason is an error of the node. `true` and `false` are `CalcBool`, never a `CalcRead`. What a token may be (a number in range, a string of printable ASCII) is left to the parser. An `op` that is not an operator of the node's arity is an error that names it.

## Limits

Enforced while the tree is walked (`CalcBuildState`), so a hostile tree stops at the limit and the recursion never goes past it: `ExprLimits::kMaxNodes` parts and `kMaxTreeHeight` levels (a level of the tree is a level of the AST it builds). The nesting and the bytes of the printed text are the lexer's and the parser's. An error says what is wrong and ends with the path of the node from the root (`... (at tree.left)`): a missing child is `CalcBinary: 'right' is missing (at tree)`, at the owner's path. It has no column (no text exists yet). A failed build touches no pool.

## Loading

The key `tree` of a `Value` (`ValueAccessor.h`, `ReadFormulaTree`): the node is read as any factory field is, so XML, binary and JSON are one code, and `type` names the class of each node (`<args><node type="...">` for a list: `ELEMENT = node`). `ReadFormulaTree` is not a template, because the generated code that registers the classes lives in `CalcNodes.cpp` and a header full of `Value` fields must not see it. The build's errors are reported under `tree`; the text then goes to the compile of `expr`, whose errors have the columns of that text, so the report carries it.
