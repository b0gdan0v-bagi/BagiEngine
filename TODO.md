# TODO

The list of known problems and ideas is the **todo store**: the service of the owner's BagiTodo that holds every entry, its status, the roadmap stages, the plans and the reviews; `tools/todo/` holds only the loader of its command line (`tools/todo/CLAUDE.md`). Nothing of the store is in this repository. `python3 tools/todo/todo.py dashboard` gives the address of the dashboard, and the direction of the engine is the owner plan `engine-roadmap.md` (`todo.py plan get engine-roadmap.md --stdout`).

To record an idea, run `/todo-add <the idea in a sentence or two>`. `/todo-db` lists and shows entries; `todo-loop` works the entries through in their own branches, and `/todo-merge` merges a finished branch into `main` after a report.
