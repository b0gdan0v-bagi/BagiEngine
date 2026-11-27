# Ask for a change

You will file a request for the engine as an entry of the todo store, write it so that a coding agent can take it without asking you anything, and learn which entries the agents leave to you and where to look at yours afterwards. Nothing here starts work: filing only writes down the idea.

You need the repository checked out, a Claude Code session open in it, and access to the store from the owner. The owner gives you a sign-in (`python3 tools/todo/todo.py login` with the address the owner hands out) or a token for a git-ignored `.env` file in the checkout. An address or a token is never written on a page, in an entry or in a chat. Without the store everything else still works: configure, build and test never call it (`tools/todo/CLAUDE.md`, "Without the store"), you just cannot file or read entries.

## What an entry is

An entry is a record in the **todo store**, a service the owner runs: an `id`, a `title`, a `body` in markdown and a status, in one **area** (one todo list: the engine has one, and each game has its own). The store is the only copy. No entry is a file in git, so a branch never carries one, and the agents' plans and reviews live there too. A loop of agents takes an entry that is `open`, works on it in its own branch and leaves it for you to review ([The todo loop](todo-loop.md)).

## File it

In a Claude Code session in the repository:

<!-- illustration: the skill's command line, shown and not run on this page; the idea is made up and nothing is filed -->
```text
/todo-add engine: a text component can show a number with a thousands separator
```

`/todo-add` (the skill `todo-add`, [its text](../../.claude/skills/todo-add/SKILL.md)) looks through every entry for the same idea, and offers to add to a match instead of a second entry. Then it picks a short id, writes the body in the format below, picks one to three tags from the ones the store already has, and files the entry as `open` in the area the idea is about. It does not start work and it commits nothing. A vague idea is filed as written; the loop will skip it as `needs detail`, so do not pad it to look finished.

The same by hand is one command, with the body on standard input:

<!-- illustration: the command of tools/todo/CLAUDE.md with made-up values; shown, not run on this page, because it writes to the store -->
```bash
python3 tools/todo/todo.py add text-thousands-separator --area engine --title "A text component can show a number with a thousands separator" --body-file -
```

Leave the priority alone. It is the owner's (`tools/todo/CLAUDE.md`, "The data"); if the work is urgent, say so in the text.

## What a good entry says

The skill writes the body with these lines, and so should you:

<!-- source: .claude/skills/todo-add/SKILL.md -->
```text
   - `**Where:**` only if one or two quick `grep`s show the place in the code; do not invent it;
   - `**What is missing:**` (required: the dashboard takes the summary from it);
   - `**A fix has to cover:**` if the idea gives something to list.
```

- **Where** is the file, the folder or the class the change is in. A wrong place is worse than none, so write it only when you looked.
- **What is missing** is what the engine cannot do now, as an outcome you could check afterwards: not "improve", but "a game can do X and sees Y". The store takes the summary of the entry from this paragraph.
- **A fix has to cover** lists the cases the change must handle, one per line. It is how you say what done looks like.

Two more things make an entry workable. **Name what you assume is already there**: a key binding, a class, a file. An agent checks it before it starts, and an entry whose premise is missing is parked as `BLOCKED` (only a `BLOCKED: question:` becomes a question for you; `.claude/agents/todo-architect.md`, "Gate"). And **one subject per entry**: two ideas in one entry are done together or not at all. When an entry cannot start before another is done, that is a link between the two entries (`todo.py dep add`, which `/todo-add` can make when you say so), not a sentence "waits for X" in the body.

Here is a request in the two shapes, made up for this page; nothing is filed.

<!-- illustration: made up for this page; nothing is filed -->
```text
Bad:
  Title: Numbers look bad
  Body:  Big numbers in the score are hard to read, please fix.

Good:
  Title: A text component can show a number with a thousands separator
  Body:
    **Where:** src/Modules/BECore/Text/
    **What is missing:** a game that shows a score as a text cannot get "12,345"
    from the number 12345; it has to build the string itself in C++.
    **A fix has to cover:**
    - the separator follows the language of the game's string table
    - a negative number and a number below 1000
    - the text component's existing formats keep working
    Assumes: a text component can already take its text from the string table.
```

The bad one has no place, no outcome you could check and nothing it relies on, so the classifier skips it as `needs detail`. The good one says where to look, what the player sees afterwards and what must not break.

## Where it goes

An area is one todo list, and an entry goes to the area of the repository it is about. An idea about the engine that you have in a game goes to the engine's area. If your token does not hold that area, the entry lands in its **inbox**, which you cannot read back, and the loop takes the inbox first (`tools/todo/CLAUDE.md`, "The data"). Look for a duplicate before, since you cannot see it afterwards.

The opposite case: a game's entry that needs a facility the engine does not have is left alone as `needs engine change` until the engine's entry is merged. A game's `.claude/rules/todo-project.md` says so (section "Shared submodule"; the engine's copy of the file names it too). So file the engine's entry first, and link the game's entry to it.

## What the loop leaves to you

The classifier (`.claude/agents/todo-classifier.md`) skips an entry for one of three kinds of reason, and says which:

- **It is vague:** `needs detail`, or `too large` when no first slice can be named.
- **It is yours to decide or do:** `owner decision`, `owner step` (a hosting console, a check on production) or `needs platform` (a system, a graphics backend or a device this machine lacks).
- **It waits:** `blocked by <id>` (a link), `waits for <ids>` (named in the body with no link), or `umbrella` (the entry only gathers others; the work is in its children).

A question for you is never only a line in a chat. The loop files it as an entry of the area `owner`, with the lines `**Question:**`, `**Recommendation:**` (the agent's own choice and why) and `**Blocks:**` (the entries waiting), and parks the entry that asked (`.claude/skills/todo-loop/SKILL.md`, "Owner steps"). A small reversible choice, such as a name or where a file goes, is not a question: the agent takes it and writes it in a list you read at the review.

## Look at it afterwards

<!-- command -->
```bash
python3 tools/todo/todo.py list --status open
python3 tools/todo/todo.py show <id>
python3 tools/todo/todo.py history <id>
```

`list` prints one line per entry with its id, title and status; `show` prints the entry as markdown, with its dependencies; `history` prints every write to it with the time and who made it. All three only read. `todo.py dashboard` prints the address of the store's pages, which your sign-in opens.

## What you did

You learned what an entry is and that it lives in the store and not in git, filed an idea with `/todo-add` or by hand, wrote it with a place, an outcome and the things it assumes, chose the area it belongs to, and saw why the loop leaves an entry to you and how to look at it.

Next: [The todo loop](todo-loop.md)
