# TODO

The list of known problems and ideas is the **todo store**: a service on Railway that holds every entry, its status, the roadmap stages, the plans and the reviews (`tools/todo/CLAUDE.md`). Nothing of it is in this repository. Vikunja (project `BagiEngine`) shows it to a person and takes back what a person changes there; `python3 tools/todo/todo.py dashboard` gives the address of the dashboard, and the direction of the engine is the owner plan `engine-roadmap.md` (`todo.py plan get engine-roadmap.md --stdout`).

To record an idea, run `/todo-add <the idea in a sentence or two>`. `/todo-db` lists and shows entries; `todo-loop` works the entries through in their own branches, and `/todo-merge` merges a finished branch into `main` after a report.
