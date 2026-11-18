# Run mode: `--editor` and `--game` choose the mode for one run

- Version: 0.1.28
- Entry: editor-cli-mode
- Date: 2026-11-16
- Bump: patch

## Added
- `--editor` / `--game` on the command line override `mode` of `ApplicationConfig.xml` for one run; the file is not touched, so opening a game in the editor (a person, an agent, a live check) leaves nothing to restore. Both flags together stop the start-up with a log line.
- `RequestedRunMode(fileMode, args)` and `RunModeArguments` in `BECore/Application/RunMode.h`; `WidgetManager::CreateWidgets` and `Application::Initialize` use it.
