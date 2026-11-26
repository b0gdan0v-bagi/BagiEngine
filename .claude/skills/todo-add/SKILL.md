---
name: todo-add
description: Quickly records a new idea or known problem as an entry in the todo store (checks for a duplicate, picks an id, writes the entry in the project format). Use when the user asks to add a todo, record this idea, note a problem for later, or says "заведи запись в todo".
---

# todo-add

`/todo-add [<area>:] <the idea in one or two sentences>`. Goal: a correct entry in the todo store, in the right area, with as little research as possible, so ideas are not lost and `todo-loop` can pick them up. The store and its CLI are the store's docs that section `Areas` of the rules file names (the commands, "Inbox", "Dependency", "Tags"); `todo` below is `python3 <engine>/tools/todo/todo.py --actor todo-add` (settings from the git-ignored `.env`). The areas (`<area>`, `<engine>`) are in section `Areas` of `.claude/rules/todo-project.md`; with one area there is nothing to choose. With several, an idea goes to the area of the repository it is about (for example an idea about the engine to the engine's area, one about the game to the game's, one about a service of a workspace to that service's); the prefix `<area>:` decides, otherwise the place the idea names, and when that is unclear ask in one line. `todo area list --json` lists every area, `held` true for those of this checkout's token; an entry for an area it does not hold goes to that area's inbox by the same `add` (step 5; the store's docs, "Inbox"); the owner's token holds every area.

## Steps

1. **No idea text:** ask for it in one line and do nothing else.
2. **Duplicate check.** `todo list --all --json` (every area, closed entries too) and look at the titles and summaries for the key words of the idea; `todo show <id>` for a close one. If an entry about the same thing exists, say which one and offer to add to it instead (`todo edit <id> --body-file -`); do not create a new entry without the user's consent. A closed one that matches is worth naming too. The list holds only the areas this token holds: for another area say in the reply that its entries were not checked.
3. **Id.** A short English kebab-case id in the style of the existing ones (`no-ci`, `prefabs`, `render-output-size`), at most about 40 characters. Ids are unique over every area; the store refuses an existing one (HTTP 409): add `-2`. The id is what branches and plans are named by, so it is not changed later.
4. **Content.** The title (English, even if the idea was given in Russian) goes to `--title`; the body, without a title line, to `--body-file -` (stdin):
   - `**Where:**` only if one or two quick `grep`s show the place in the code; do not invent it;
   - `**What is missing:**` (required: the dashboard takes the summary from it);
   - `**A fix has to cover:**` if the idea gives something to list.

   Status `open`. `--priority now|high|normal|low` only when the text makes it obvious (the owner says it is urgent or can wait), else unset, and say so.

   **Tags** (themes, one vocabulary over the store: the store's docs, "Tags"): `todo tag list` and pick 1-3 that fit (`--tag editor,ui`); the entries of the area with the same words show which ones are used. A new tag only when no existing one fits, with `--new-tag <name>` (lowercase, `-`), and name it in the reply so the owner can merge it. None fits and nothing is worth a new one: no tag, say so.
5. **Add.** `todo add <id> --area <area> --title "<title>" [--priority P] [--tag T,...] [--new-tag N] --body-file -` with the body on stdin. An area the token does not hold: the same `todo add <id> --area <area> --title "<title>" --body-file -`, without `--priority` (that area's owner or loop sets them); it lands in that area's inbox and this token cannot read it back, so check for a duplicate before (step 2 sees only the held areas: say so in the reply). A step only the owner can take (Railway, Google, a bank, Telegram, a check on production, a decision): first `todo list --area owner --status open,deferred` (an existing step that fits gets only the link of step 6), else `todo add <id> --area owner [--for-area <area>] --title "<title>" --body-file -` (no secret, token or production URL in it: it goes to the owner's phone).
6. **Links.** When the idea says it waits for another entry (or another entry waits for it), and the whole entry waits, not one part of it: `todo dep add <id> <blocker>` (the store's docs, "Dependency"). A part that can be done before is a note in the body, not a link. An **umbrella** (an entry that only gathers others: title ending `(umbrella)`, body listing the child entries, "nothing is done here directly") is linked as blocked by each open child (`todo dep add <umbrella> <child>`), so no loop, dispatcher or panel offers it while the children are open; it gets no `--priority` (the children carry it).
7. **Reply** in a few lines: the id, the area, the title, the tags (a new one marked as new), and `todo dashboard` for the page; for an entry of another area "filed into the inbox of <area>; its loop takes it first" (this token no longer reads it). Nothing is committed: the entry lives only in the store. Do not close entries or edit other people's entries.

A vague idea is still recorded as written; `todo-loop` skips what it cannot work on with the reason "needs detail", so do not make the entry up to look complete.
