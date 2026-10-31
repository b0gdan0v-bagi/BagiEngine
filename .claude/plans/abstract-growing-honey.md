# Закрыть ветку `tmp/delete_cursor`

## Context

Вы вернулись к проекту и не помните, где остановились. По репозиторию видно: последняя сессия (30 окт) закончила CMake-пресеты,
настройку VS Code и скилл `build`. Всё закоммичено, дерево чистое, ветка на 9 коммитов впереди `main`
(Cursor → VS Code, фикс macOS-сборки, vendored Vulkan loader, `CMakePresets.json`, скилл `build`).

Цель: подтвердить, что ветка в рабочем состоянии, подчистить хвосты от прошлых задач и смерджить её в `main`,
чтобы дальше работать с чистого места.

Хвосты, которые видны сейчас:

- В [chess-плане](chess-mvp-module_5d6e2126.plan.md) пункт `build-and-format` всё ещё `in_progress`, хотя в match3-плане такой же пункт
  `completed` и код закоммичен (`1c8c99a1 MVP chess / match3 game`). Неизвестно, прогонялись ли `clang-format` и `strip-pch-includes`
  на Chess/Match3/Games (`clang-format` в PATH нет, скилл работает через `LIBCLANG_PATH`).
- Свежесть сборки: `build/macos-debug/compile_commands.json` от 18:20, exe `build/ClangmacOS/MyGame` от 18:13.
  Это не доказывает, что итоговое состояние ветки собирается и тесты зелёные.
- Не проверено (из [плана пресетов](cmake-presets-wobbly-mist.md)): Windows-пресеты и UI CMake Tools (F5/F7, выбор пресета).

## Шаги

1. **Сборка.** Скилл `build` (`macos-debug`). Конфигурация не нужна: `git status` чистый, `BE_CLASS`/`BE_REFLECT_FIELD` после последнего
   configure не менялись. Если скилл решит иначе, подчиняемся ему. Сборка идёт в фоне, лог в scratchpad.
2. **Тесты при старте.** Запустить `build/ClangmacOS/MyGame` (скилл `run`): тесты из `config/TestsConfig.xml` идут в
   `CoreManager::OnApplicationPreInit`. Смотрим в логе результаты 10 включённых тестов (в том числе `MoveGeneratorTest`,
   `Match3GameModelTest`), затем закрываем окно. Заодно быстро проверить в `GameSelectorWidget` переключение Chess ↔ Match3.
3. **Форматирование.** Скилл `clang-format` на `src/Modules/Games`, `src/Modules/Chess`, `src/Modules/Match3`. Если есть правки,
   они идут отдельным коммитом (`Apply clang-format to Games/Chess/Match3`), чтобы не мешаться с остальным.
4. **Includes.** Скилл `strip-pch-includes`: сначала dry-run по тем же трём модулям, применять только если нашлись дубли.
   После правок повторить шаг 1, чтобы убедиться, что сборка не сломалась.
5. **Chess-план.** В `.claude/plans/chess-mvp-module_5d6e2126.plan.md` поменять `build-and-format` на `completed`, если шаги 1-4 зелёные.
6. **Merge.** Мердж `tmp/delete_cursor` → `main` и push — внешнее действие, поэтому перед ним спрошу, как вы хотите: PR через `gh`
   или локальный merge. Ветку удалять только по вашей просьбе.

## Критичные файлы и что переиспользуем

- Скиллы: `.claude/skills/build`, `clang-format`, `strip-pch-includes`, `run` (ничего нового не пишем).
- [CMakePresets.json](../../CMakePresets.json): пресеты `macos-debug` / `macos-release`.
- [config/TestsConfig.xml](../../config/TestsConfig.xml): список включённых тестов.
- [chess-mvp-module_5d6e2126.plan.md](chess-mvp-module_5d6e2126.plan.md): правка статуса одного пункта.

## Верификация

- Шаг 1 завершился с кодом 0, `build/ClangmacOS/MyGame` обновился по mtime.
- В логе запуска нет упавших тестов; Chess и Match3 переключаются в `GameSelectorWidget`.
- После форматирования и strip-pch: `git diff --stat` содержит только ожидаемые файлы, повторная сборка зелёная.
- `git status` чистый после коммитов; `git log main..HEAD` показывает ожидаемый набор коммитов перед merge.

## Что остаётся вам

- Проверить в VS Code: выбор пресета в CMake Tools, F7 (сборка), F5 (отладка). Я это не могу проверить отсюда.
- Windows-пресеты (MSVC env, ClangCL, VS 2022) не запускались ни разу; их нужно прогнать на Windows-машине.

## Вне scope (на потом)

`BinarySerializer`/`BinaryDeserializer`, TODO в `TaskSystem`, пересоздание swapchain в Vulkan, разделение выходных каталогов Debug/Release.
