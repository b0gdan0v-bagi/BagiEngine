# CMake Presets + VS Code integration + `build` skill

## Context

Сборка сейчас живёт в трёх несогласованных местах:

- `.vscode/tasks.json` (локальный, в `.gitignore`) жёстко собирает в плоский `build/`;
- `CI/BagiEngine.code-workspace` (единственный отслеживаемый VS Code-конфиг) тоже ссылается на `build/` и на хардкод-пути
  `build/ClangmacOS/MyGame`, а `cmake.generator` / `cmake.buildDirectory` расходятся с тем, что реально делает launcher
  (`build/<Compiler><Platform>Solution`);
- в репозитории нет `CMakePresets.json`, поэтому расширение CMake Tools (уже стоит: `ms-vscode.cmake-tools-1.24.42`) не знает,
  как конфигурировать проект, и пользователь вынужден руками гонять `cmake -B build ...`.

Цель: один источник правды для конфигураций (`CMakePresets.json`), которым пользуются и VS Code (CMake Tools: выбор пресета,
F7, F5), и CLI, и Claude через новый скилл `build` (сборка проекта поверх этих пресетов).

Решения пользователя: платформы **macOS + Windows**; каталог сборки **`build/<имя-пресета>`**.

## Что нашли в коде (влияет на дизайн)

| Факт | Следствие |
|------|-----------|
| `CmakeLists.txt:130-138` — `OUTPUT_BASE_DIR = build/{Clang\|MSVC\|GCC}{macOS\|Windows\|Linux}` не зависит от `binaryDir` | Exe всегда в `build/ClangmacOS/MyGame`, **Debug и Release (Ninja) затирают друг друга**. CMakeLists не трогаем, оговорка идёт в скилл. Для VS-генератора CMake сам добавит `Debug/`/`Release/`. |
| `CmakeLists.txt:149-156` — «в Release выключать ASSERT/EXPECT» не работает: `option()` выше уже создал cache-запись, `NOT DEFINED CACHE{}` всегда false | Release-пресеты явно ставят `ENGINE_ENABLE_ASSERTS=OFF`, `ENGINE_ENABLE_EXPECTS=OFF` (это заявленное в комментарии намерение). VS-пресеты не ставят — build type там выбирается на этапе сборки. |
| Reflection (`add_reflection_target`) вызывается через `execute_process` на **configure**, custom command нет; выход в `${CMAKE_BINARY_DIR}/Generated` | После правок `BE_CLASS`/`BE_REFLECT_FIELD` нужен повторный configure, а не только build. Каждый пресет получает свой `Generated/`. Критично для скилла. |
| `glslc` лежит в `/usr/local/bin` (PATH), на macOS `VULKAN_SDK` по умолчанию (`C:/VulkanSDK/...`) не существует → должен сработать `find_program(glslc)`. **Не срабатывал**: `src/Modules/Vulkan/CMakeLists.txt` делал `set(_GLSLC "")` перед `find_program`, а `find_program` считает уже заданную переменную найденной (воспроизведено в `cmake -P`). Старая сборка работала только из-за `VULKAN_SDK` в кэше. | **Исправлено удалением `set(_GLSLC "")`** (обнаружено при верификации, вне исходного списка файлов). Пресеты `VULKAN_SDK` не задают; машинные значения — в `CMakeUserPresets.json` (gitignore). |
| Meta-generator regex-based, `LIBCLANG_PATH` нужен только `format_sources.py` / `tidy_fix.py` | В пресеты не включаем. |
| `tidy_fix.py:76-93` ищет `build/*/compile_commands.json`, берёт первый по алфавиту | С `build/<preset>/` находится автоматически; при нескольких конфигурациях — `--build-dir build/<preset>` (пишем в скилл). |
| Корневой файл называется `CmakeLists.txt` (не `CMakeLists.txt`) | На macOS/Windows работает (case-insensitive FS). На Linux сломается — вне scope, упомянуть в итоговом отчёте. |

## Изменения

### 1. `CMakePresets.json` (новый, в корне, коммитится)

`version: 6`, `cmakeMinimumRequired 3.31.0`. Все `binaryDir` = `${sourceDir}/build/${presetName}`.

Hidden-базы (композиция через `inherits`):

- `base` — `binaryDir`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`
- `ninja` — `base` + `generator: Ninja`
- `debug` — `CMAKE_BUILD_TYPE=Debug`
- `release` — `CMAKE_BUILD_TYPE=Release`, `ENGINE_ENABLE_ASSERTS=OFF`, `ENGINE_ENABLE_EXPECTS=OFF`
- `clang` — `CMAKE_C_COMPILER=clang`, `CMAKE_CXX_COMPILER=clang++`
- `host-macos` / `host-windows` — `condition: hostSystemName equals Darwin / Windows`
- `msvc-env` — `architecture {x64, strategy: external}`, `toolset {host=x64, strategy: external}`: CMake Tools сам подгружает
  `vcvars` для Ninja на Windows; `CMAKE_*_COMPILER=cl`

Видимые configure-пресеты:

| Имя | Состав | Проверено |
|-----|--------|-----------|
| `macos-debug`, `macos-release` | ninja + host-macos + clang + debug/release | да (macOS) |
| `windows-msvc-debug`, `windows-msvc-release` | ninja + host-windows + msvc-env + debug/release | нет |
| `windows-clang-debug`, `windows-clang-release` | то же + clang | нет |
| `windows-vs2022` | `base` + host-windows, `Visual Studio 17 2022`, `-A x64` (multi-config) | нет |
| `windows-vs2022-clangcl` | то же + `toolset ClangCL` | нет |

Build-пресеты: по одному на каждый Ninja-конфиг (`configurePreset` = то же имя), для VS — `windows-vs2022-{debug,release}`,
`windows-vs2022-clangcl-{debug,release}` (`configuration`). Все с `targets: ["MyGame"]` (как в текущем `tasks.json`) и `condition`
по хосту, чтобы чужие не светились в списке. Test/workflow-пресетов нет: отдельного тестового бинаря нет (тесты идут при старте
движка), а `configure && build` — две команды.

### 2. VS Code

`.vscode/` в `.gitignore`, поэтому правим **оба** места:

- **`CI/BagiEngine.code-workspace`** (отслеживается; его открывает launcher `open_vscode`):
  - settings: убрать `cmake.buildDirectory`, `cmake.generator`, `cmake.preferredGenerators`, `C_Cpp.default.compileCommands`
    (в режиме пресетов не работают / указывают на старый `build/compile_commands.json`; compile commands отдаёт CMake Tools как
    `configurationProvider`); добавить `cmake.useCMakePresets: "always"`;
  - tasks: `CMake: configure` / `CMake: build` → задачи `type: "cmake"` (работают с выбранным пресетом, метка `CMake: build`
    сохраняется для `preLaunchTask`); удалить `configure (MSVC)`; `Run MyGame` → `${command:cmake.launchTargetPath}`;
  - launch: `program` → `${command:cmake.launchTargetPath}` во всех трёх конфигах.
- **Локальные `.vscode/{settings,tasks,launch}.json`** (не попадут в git): те же замены — `build`/`configure`/`clean` становятся
  `type: "cmake"`, `program` в CodeLLDB-конфигах → `${command:cmake.launchTargetPath}`, в settings добавить
  `cmake.useCMakePresets: "always"` (существующие `LIBCLANG_PATH` не трогаем).

### 3. `.gitignore`

Добавить `CMakeUserPresets.json` (в блок «user-specific»).

### 4. Скилл `.claude/skills/build/SKILL.md` — «собрать проект»

Это скилл-**действие**, а не справочник по пресетам: после `/build` Claude сам выбирает пресет, при необходимости конфигурирует,
собирает и честно докладывает результат. Формат как у `clang-format` / `tidy-fix`: frontmatter `name: build` +
`description` с «Use when …» (build / compile / rebuild / сборка / собери / пересобери / «не собирается», смена Debug↔Release),
затем разделы. Аргументы (`$ARGUMENTS`, все необязательные): `debug|release` (по умолчанию `debug`), `clean` (= `--clean-first`),
`configure` (принудительный configure), имя конкретного пресета (тогда авто-выбор пропускается).

**Алгоритм (основной раздел скилла):**

1. **Пресет.** Явное имя из аргументов → иначе по хосту (`uname -s`): Darwin → `macos-{debug,release}`; Windows →
   `windows-msvc-{debug,release}`. Сверить с `cmake --list-presets` (если пресета нет — остановиться и показать список,
   не придумывать).
2. **Configure нужен, если** (любой пункт): нет `build/<preset>/CMakeCache.txt`; передан `configure`; в `git status --porcelain`
   есть добавленные/удалённые/переименованные файлы в `src/` (`GLOB_RECURSE` без `CONFIGURE_DEPENDS`); менялись `*.cmake`, `CMakePresets.json`,
   `CMakeUserPresets.json`; или в диффе затронуты `BE_CLASS` / `BE_REFLECT_FIELD` (reflection генерируется **только на configure**).
   Правки `CMakeLists.txt` Ninja подхватит сам. Сомневаешься — конфигурируй (дёшево по сравнению со сборкой).
   Команда: `cmake --preset <p>`.
3. **Build.** `cmake --build --preset <p> [--clean-first]` из корня проекта. Полная первая сборка долгая (SDL, SDL_image, ImGui) —
   запускать в фоне с выводом в лог в scratchpad и дожидаться завершения, а не поллить; таймаут не использовать для обрыва сборки.
4. **Разбор падения.** Из лога достать первые ошибки (`error:` / `FAILED:` / `CMake Error`), показать как `file:line` с
   одной-двумя строками контекста; различать фазы: configure-ошибка (в т.ч. `Meta-generator failed`, `glslc not found`) /
   компиляция / линковка. Править код только если пользователь просил «собери и почини»; цикл правка→сборка — не больше трёх итераций,
   потом отчитаться. Предупреждения — только счётчик, если не просили.
5. **Отчёт.** Успех заявлять только при коде возврата 0; указать пресет, что выполнялось (configure+build / только build),
   путь к exe `build/{Clang|MSVC}{macOS|Windows}/MyGame[.exe]` и время. Если часть шагов пропущена или не удалась — сказать прямо.

**Справочные разделы (коротко):**

- Таблица пресетов (имя → компилятор/генератор/ОС) и команды `--list-presets`, `--target`, `--verbose`, `-j`; для VS-пресетов
  `configuration` уже в build-пресете (`windows-vs2022-{debug,release}`, `windows-vs2022-clangcl-*`), Ninja-multiconfig не используется.
- **Грабли:** Ninja Debug/Release делят `build/<Compiler><Platform>/MyGame` (последняя сборка выигрывает); `compile_commands.json`
  лежит в `build/<preset>/` (для `tidy-fix` передавать `--build-dir build/<preset>`); не использовать `cmake -B build` (старый плоский каталог);
  не править содержимое `build/`; корень проекта — `CmakeLists.txt` (регистр!) .
- **Не запускать exe** после сборки без просьбы — открывается окно и при старте прогоняются тесты (`CoreManager::OnApplicationPreInit`);
  для запуска/скриншота отсылать к скиллу `run`.
- **VS Code** (для объяснений пользователю): `CMake: Select Configure Preset` / `Select Build Preset`, F7 — сборка, F5 — отладка,
  `CMake: Delete Cache and Reconfigure` — аналог `configure`.
- **Локальные переопределения:** пример `CMakeUserPresets.json` (`inherits: macos-debug`, свой `VULKAN_SDK`, `ccache` через
  `CMAKE_CXX_COMPILER_LAUNCHER`); файл в `.gitignore`.
- **После сборки C++-правок:** напомнить про скиллы `clang-format` и `strip-pch-includes` (требование CLAUDE.md), сами не запускать.
- Ссылки на файлы относительные, как в других скиллах.

### 5. Документация

- `CLAUDE.md`: блок «Build Commands» — сначала presets (`cmake --preset macos-debug && cmake --build --preset macos-debug`),
  прежние команды оставить как «без пресетов»; строка 119 — добавить `build` в список скиллов.
- `.claude/rules/cmake-rules.md`: короткий раздел «Presets» (структура hidden-баз, `build/<preset>`, `CMakeUserPresets.json`).
- `WORKSPACE_README.md` (≈ строки 180-195): добавить presets рядом с «CMake с Ninja / Visual Studio».

C++ не меняется, поэтому шаги `clang-format` и `strip-pch-includes` из CLAUDE.md не применяются.

## Верификация (после одобрения плана)

1. JSON валиден: `python3 -m json.tool CMakePresets.json`; для `.code-workspace` (JSONC) — парсинг без комментариев.
2. `cmake --list-presets` показывает ровно `macos-debug`, `macos-release` (Windows-пресеты скрыты `condition`; файл при этом
   целиком парсится CMake, так что синтаксис Windows-секций тоже проверяется). То же для `--list-presets=build`.
3. `cmake --preset macos-debug` — успешный configure в `build/macos-debug`, в `CMakeCache.txt`: `CMAKE_BUILD_TYPE=Debug`,
   `CMAKE_GENERATOR=Ninja`, clang, есть `compile_commands.json` и `Generated/`.
4. `cmake --build --preset macos-debug` (полная сборка, в фоне) — зелёная, `build/ClangmacOS/MyGame` обновился по mtime.
5. `cmake --preset macos-release` — только configure: в кэше `CMAKE_BUILD_TYPE=Release`, `ENGINE_ENABLE_ASSERTS=OFF`,
   `ENGINE_ENABLE_EXPECTS=OFF`. Release не собираем, чтобы не затереть debug-бинарь.
6. `git check-ignore -v CMakeUserPresets.json` — игнорируется; `CMakePresets.json` — нет.
7. Скилл `build`: frontmatter как у остальных (`name`, `description` с «Use when …»), скилл виден в списке; прогнать его алгоритм
   вручную по тексту: выбор пресета на этой машине даёт `macos-debug`, условия configure на чистом дереве дают «только build»
   (после шага 3 кэш уже есть), команды из шагов 2-3 исполнимы. Негативный кейс: несуществующий пресет → остановка со списком.
8. `git status` / `git diff --stat` — показать итог; отдельно отметить, что **не проверено**: Windows-пресеты (MSVC env, ClangCL,
   VS 2022), поведение UI CMake Tools (F5/F7, выбор пресета) — это нужно проверить пользователю в VS Code.

## Риски / открытые моменты

- Первая сборка в `build/macos-debug` полная (SDL, SDL_image, ImGui…), старый `build/` не используется и не удаляется.
- Debug/Release-коллизия exe — осознанно оставлена (правка `OUTPUT_BASE_DIR` меняет пути в launch/launcher; можно сделать отдельной задачей).
- Windows-пресеты написаны по launcher'у и документации CMake Tools (`architecture.strategy: external`), без запуска.
