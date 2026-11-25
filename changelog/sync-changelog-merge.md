# Engine branches: EngineVersion.h and CHANGELOG.md no longer conflict at a merge; a merge driver regenerates them

- Version: 0.22.2
- Entry: sync-changelog-merge
- Date: 2026-11-25
- Bump: patch

## Added
- A git merge driver `bagi-changelog` for `src/Modules/BECore/Utils/EngineVersion.h` and `CHANGELOG.md` (`.gitattributes`), and the commands `changelog.py merge-driver`, `install-driver` and `settle`: both files are rebuilt from the fragments instead of conflicting. `sync-engine` installs the driver and settles the two files after a merge; `todo-merge` does the same. Adapt: a game takes nothing; a clone of the engine that merges branches runs `python3 tools/changelog/changelog.py install-driver` once (`sync-engine` does it).
