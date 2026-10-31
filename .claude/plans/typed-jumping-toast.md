# Движок как изолированный модуль для отдельных игр (Фаза 1: изоляция в этом репозитории)

## Context

Сейчас Chess и Match3 живут в одном репозитории с движком и собираются в один `MyGame`. Связность в обе стороны:

- `reflection_targets.json` (движок) перечисляет каталоги Chess/Match3 в `scan_dirs`/`include_dirs`;
- `src/Application` + `src/Widgets` + `src/main.cpp` собираются прямо в `MyGame` через `file(GLOB src/*)`, а `GameSelectorWidget` нужен только для мульти-игрового запуска;
- корневой `CmakeLists.txt` смешивает настройку движка (опции, компилятор, venv, `add_reflection_target`) с таргетом приложения;
- `config/` и `assets/` — одна общая папка: конфиги движка (Logger, Assert, SDLWindow, Widgets) лежат рядом с игровыми (`SceneConfig` с `ChessScene`/`Match3Scene`, `TextureLibrary`, `GamesConfig`, `chess_test.png`);
- `tests/` собирает движок, Chess и Match3 в один `BagiEngineTests`.

**Цель:** движок становится самодостаточным ядром, которое ничего не знает об играх, а игра подключает его через публичный CMake API и свои `config/`, `assets/`, `tests/`. Движок подключается к играм как git submodule, но по ответам пользователя **в этой задаче репозитории не создаются**: делаем только изоляцию внутри текущего репо (Фаза 1), так чтобы вынос (Фаза 2) свёлся к `filter-repo` + `git submodule add`.

## Решения пользователя

| Вопрос | Решение |
|--------|---------|
| Физическая раскладка | Сначала изоляция в этом репо, вынос в отдельные репо — отдельной задачей |
| Мульти-игровой запуск | Один exe = одна игра. `IGame`/`GameManager` остаются в движке как контракт «движок ↔ игра». `GameSelectorWidget` удаляется |
| `config/` и `assets/` | Слоистые корни: движок снизу, игра сверху, одноимённые файлы игры перекрывают файлы движка |
| История git / новые репо | Не создавать репо сейчас |

## Целевая раскладка (Фаза 1)

```
BagiEngine/                      # станет репозиторием движка
├── CMakeLists.txt               # project() + include(cmake/BagiEngine.cmake) + tests + games/ (опционально)
├── cmake/BagiEngine.cmake       # весь setup движка + публичные функции be_*
├── config/                      # ТОЛЬКО конфиги движка (Application, AssertHandlers, Logger, SDLWindow, Widgets, Tests, SceneConfig с MainScene)
├── src/{Application,Widgets,main.cpp}   # остаются на месте, собираются в RuntimeModule (не в exe)
├── src/Modules/{BECore,Events,TaskSystem,SDL,Vulkan,Games,...}
├── tests/{BECore,Common}        # движковые тесты + BagiEngineTestSupport
└── games/
    ├── Chess/   { CMakeLists.txt, CLAUDE.md, src/Chess/, config/, assets/, tests/ }
    └── Match3/  { CMakeLists.txt, CLAUDE.md, src/Match3/, config/, tests/ }
```

`games/<Name>/` повторяет будущий корень репозитория игры. Игра обращается к движку только через `BAGI_ENGINE_DIR` и функции `be_*`; удаление `games/` оставляет чистый репо движка. Exe: `BagiChess`, `BagiMatch3` (вместо `MyGame`). Пути `#include <Chess/...>` не меняются (include-корень модуля — `games/Chess/src`).

## Шаги

Порядок выбран так, чтобы сборка и тесты оставались зелёными после каждого шага.

### 0. Базовая линия
`cmake --preset macos-debug`, полная сборка, `ctest --preset macos-debug`: записать число тестов и список `build/Generated/*.gen.hpp` (эталон для шага 3).

### 1. CMake API движка (`cmake/BagiEngine.cmake`)
- Вынести из корневого `CmakeLists.txt` в `include()`-модуль: зависимости, опции, определение компилятора/платформы, output dirs, assert-опции, venv/Python, `add_reflection_target`, `add_subdirectory` движковых модулей. `include()` (а не `add_subdirectory`) нужен, чтобы `add_compile_definitions`/`-fno-exceptions` действовали и на игры.
- Ввести `BAGI_ENGINE_DIR` (каталог движка) и заменить им `CMAKE_SOURCE_DIR` в engine-CMake: путь к `meta_generator.py`, `.venv`, `--project-root`, `external/Vulkan-Loader`. Для вендорного `src/Modules/imgui/CMakeLists.txt:21` (`${CMAKE_SOURCE_DIR}/external/SDL/include`, править нельзя) добавить в `BagiEngine.cmake` `target_include_directories(imgui PRIVATE ${BAGI_ENGINE_DIR}/external/SDL/include)`.
- Новые функции (заменяют ~30 скопированных строк в `Games/Chess/Match3/CMakeLists.txt` и в `tests/CMakeLists.txt`):
  - `be_add_module(NAME ... SOURCE_DIR ... LINK ...)`: STATIC-библиотека, `REUSE_FROM BECoreModule`, force-include `BECore/pch.h`, `IMGUI_VULKAN_AVAILABLE=1` (должно совпадать с BECore, иначе PCH отвергается), `REFLECTION_GENERATED=1`, include-пути;
  - `be_link_engine(TARGET)`: `WHOLE_ARCHIVE` для `BECore/Events/TaskSystem/SDL/Vulkan/Runtime` (иначе статические инициализаторы `AbstractFactory` выкидываются линкером);
  - `be_add_game_executable(NAME GAME_ROOT ... MODULES ...)`: добавляет `src/main.cpp`, `be_link_engine`, модули игры с `WHOLE_ARCHIVE`, определения `BAGI_ENGINE_ROOT` и `BAGI_GAME_ROOT`, копирование SDL3/SDL3_image/Vulkan DLL, `BUILD_RPATH @executable_path`, natvis, `WIN32`-флаг;
  - `be_add_game_tests(NAME SOURCES ...)`: exe на `BagiEngineTestSupport`, `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)` без `WORKING_DIRECTORY`;
  - `be_add_game_reflection(TARGET SOURCE_DIRS ...)`: см. шаг 3.
- Переименовать `CmakeLists.txt` → `CMakeLists.txt` (на Linux с регистрозависимой ФС текущее имя не подхватится). На macOS делать двумя `git mv` через временное имя; поправить упоминания в документации.
- `RuntimeModule`: новый `src/CMakeLists.txt`, STATIC из `src/Application/*` и `src/Widgets/*` (без `main.cpp`). Пути и include-пути остаются, поэтому `reflection_targets.json` для виджетов не меняется. `main.cpp` компилируется в каждый игровой exe.

### 2. Слоистые корни: `FileSystem`, `ConfigManager`, `ImGuiWidget`
Файлы: `BECore/FileSystem/FileSystem.{h,cpp}`, `BECore/Config/ConfigManager.cpp`, `BECore/GameManager/CoreManager.cpp`, `src/Application/Application.{h,cpp}`, `src/Widgets/ImGuiWidget.cpp`, `tests/Common/EngineEnvironment.*`.
- `FileSystem` хранит упорядоченный список корней (младший — движок, старший — игра) и для mount-точки `config`/`assets` список слоёв. `ResolvePath` ищет с верхнего слоя вниз; `GetMountedPath` / `GetRootPath` возвращают верхний слой (запись редакторов идёт в слой, где файл найден, как и сейчас). `Mount` на существующем имени добавляет слой. Проверить все вызовы `Mount`/`Unmount`/`GetRootPath`/`SetRootPath` перед правкой.
- Источник корней: `BAGI_ENGINE_ROOT` и `BAGI_GAME_ROOT` из `main.cpp` передаются в `Application::Initialize(...)` и дальше в `CoreManager` до `InitializeCoreServices()`. Без определений остаётся нынешний поиск вверх от cwd (`FindRootDirectory`), поэтому gtest-раннеры и старый способ запуска не ломаются.
- `ConfigManager::Initialize`: сканировать все слои `config` (движок → игра), одноимённые по stem файлы дедуплицировать до запуска параллельных задач (побеждает верхний слой), иначе будет гонка за `_configs`.
- `ImGuiWidget`: `imgui.ini` писать в верхний слой `config` (раскладка окон на игру).
- gtest в `tests/BECore`: слои (приоритет, дедупликация stem, возврат к cwd-поиску) на временных каталогах.

### 3. Reflection вне движка
Файлы: `CI/meta_generator/meta_generator.py`, `core/generator.py`, `reflection_targets.json`, `cmake/BagiEngine.cmake`.
- Из `reflection_targets.json` убрать Chess/Match3 (`scan_dirs`, `include_dirs`); `src/Widgets` и `tests/BECore` остаются.
- Генератор: `--reference-target <имя>` (и `--reference-dir`): заголовки движка парсятся только ради фабричных баз, родителей и `CORE_ENUM`, но не попадают в `files_to_generate`; `--manifest <файл>` пишет список реально сгенерированных `.gen.hpp`.
- `be_add_game_reflection`: свой `--cache-dir` на цель (`${CMAKE_BINARY_DIR}/.meta_cache/<target>`), общий `Generated/`, `include_dirs = [<src игры>, src/Modules, src]`, чтобы `compute_include_path` (берёт первое совпадение) давал `Chess/ChessGame.h`. Источники цели берутся из манифеста, а не из `file(GLOB Generated/*.gen.hpp)`. Тот же переход на манифест для `add_reflection_target` движка, иначе движок подхватит файлы игры.
- Проверено по сгенерированному коду: в gen-файлах движка (`SceneNode`, `Scene`, `IComponent`) нет списков классов Chess/Match3; регистрация идёт самоподпиской каждого производного класса (`AbstractFactory<IComponent>::Register` в его собственном gen-файле), так что отдельный прогон на игру достаточен.

### 4. Вынос игр в `games/`
- `git mv src/Modules/Chess games/Chess` (и Match3), далее `src/Chess/...` на месте, `CLAUDE.md` едет вместе с игрой, `CMakeLists.txt` игры переписать на `be_add_module` + `be_add_game_reflection` + `be_add_game_executable` + `be_add_game_tests`.
- Конфиги: `games/Chess/config/SceneConfig.xml` (только `ChessScene`), `games/Chess/config/TextureLibrary.xml`, `games/Chess/assets/chess_test.png`; `games/Match3/config/SceneConfig.xml` (только `Match3Scene`). Движок оставляет `config/SceneConfig.xml` только с `MainScene`. Удалить `config/GamesConfig.xml` (`GameManager` и так берёт первую зарегистрированную игру, код менять не нужно) и `config/TextureLibrary.xml` из движка. Учесть: `TextureLibrary` должен переживать отсутствие файла (сейчас есть проверка `HasConfig`).
- Удалить `src/Widgets/GameSelector/` и запись `GameSelectorWidget` из `config/WidgetsConfig.xml`; убрать упоминания из `Games/CLAUDE.md`.
- Побочный эффект, который надо знать: в игре больше не загружается `MainScene` (её перекрывает `SceneConfig` игры). Это снимает симптом второго пункта `TODO.md` (компоненты неактивных сцен реагируют на глобальные события). Обновить текст этого пункта.

### 5. Тесты
- `tests/Common` → STATIC `BagiEngineTestSupport` (`EngineEnvironment`, `AssertBridge`, `Printers.h`, `TestMain.cpp`), `GTest::gtest` как PUBLIC. Глоб `tests/CMakeLists.txt` исключает `Common/`.
- `BagiEngineTests`: только `tests/BECore`, линкуется с движком (включая `SDLModule`, который раньше приходил транзитивно через `ChessModule`), без `ChessModule`/`Match3Module`.
- `games/Chess/tests/MoveGeneratorTest.cpp` → `ChessTests`, `games/Match3/tests/Match3GameModelTest.cpp` → `Match3Tests` (через `be_add_game_tests`); все три раннера видны одному `ctest`.

### 6. Окружение и документация
- `CMakePresets.json`: `targets: ["BagiChess", "BagiMatch3"]` во всех build-пресетах.
- `.vscode/tasks.json`, `.vscode/launch.json`, `CI/BagiEngine.code-workspace`: заменить `MyGame` на `BagiChess` (+ вариант для Match3). `VS_STARTUP_PROJECT` — `BagiChess`.
- Документы: `CLAUDE.md` (команды, цепочка модулей, раскладка), `.claude/rules/{cmake-rules,reflection-module,tests-module}.md`, `.claude/skills/build/SKILL.md` (цель `MyGame`, путь exe), `WORKSPACE_README.md`, `src/Modules/Games/CLAUDE.md` («Adding a game» теперь про `games/<Name>/`), `tests`-секции. Старые планы в `.claude/plans/` не трогать.
- В конце: skill `clang-format` на изменённых C++ файлах, skill `strip-pch-includes` (с dry-run), если менялись include'ы.

## Что переиспользуется

- `add_reflection_target` и CLI генератора (`--source-dir/--scan-dir/--include-dir`, `--cache-dir`) — обобщаются, а не переписываются.
- Блок «PCH + force-include» из `Games/Chess/Match3/CMakeLists.txt` — становится телом `be_add_module`.
- `FileSystem::FindRootDirectory` — остаётся как запасной вариант.
- `GameManager::Initialize` (создаёт все зарегистрированные `IGame`, откат на первую) — без изменений.

## Риски

1. **Генератор рефлексии** — самое хрупкое место (reference-режим, правильные include-пути в gen-файлах, лишняя перегенерация и пересборка движка). Страхуется сравнением списка и содержимого `Generated/` с эталоном шага 0.
2. **Слои `FileSystem`/`ConfigManager`**: порядок приоритета, гонка за `_configs` при дубликатах, куда пишет редактор. Покрывается gtest из шага 2.
3. **`WHOLE_ARCHIVE` и статические инициализаторы**: забытая игра/модуль в `be_link_engine` даёт молчаливое «тип не зарегистрирован».
4. **PCH**: любое расхождение определений с `BECoreModule` отвергает PCH.
5. **Debug/Release Ninja** по-прежнему перезаписывают общий `build/{Compiler}{Platform}/`; теперь там два exe, а не один.

## Проверка

1. `cmake --preset macos-debug && cmake --build --preset macos-debug`: собираются `BagiChess` и `BagiMatch3`, без новых предупреждений.
2. `cmake --build --preset macos-debug --target BagiEngineTests ChessTests Match3Tests` и `ctest --preset macos-debug`: все тесты зелёные, их суммарное число не меньше эталона шага 0 плюс новые gtest слоёв.
3. Запуск `build/ClangmacOS/BagiChess` и `BagiMatch3`: игра стартует, в логе `ConfigManager` грузит конфиги из обоих слоёв, шахматная доска и спрайты на месте, в редакторе нет `GameSelectorWidget`; `imgui.ini` создаётся в `games/<Name>/config/`.
4. Граница движка: `grep -rniE "chess|match3" src cmake config tests CI CMakeLists.txt CMakePresets.json` вне `games/` даёт пусто (кроме `.claude/plans`, истории и осознанных упоминаний в документации).
5. Репетиция выноса (без создания репо): скопировать `games/Chess` в scratchpad как корень будущего репо, положить рядом копию движка как `engine/`, написать минимальный корневой `CMakeLists.txt` (`include(engine/cmake/BagiEngine.cmake)`, `add_subdirectory(.)` игры) и сконфигурировать/собрать `BagiChess`. Если это работает, Фаза 2 механическая.

## Вне этой задачи (Фаза 2)

`git filter-repo --subdirectory-filter games/Chess` (и Match3) → новые репозитории; в каждом `git submodule add <BagiEngine> engine`, корневой `CMakeLists.txt` игры (`project`, `include(engine/cmake/BagiEngine.cmake)`, `add_subdirectory(src)`), свой `CMakePresets.json` (`include` пресетов движка), удаление `games/` из движка. Тут же решить, нужен ли репозиторий-хаб с общим запуском всех игр (сейчас сознательно нет).

## Статус выполнения (ветка `engine-game-isolation`, от `main`)

Шаги 0-6 выполнены, не закоммичено. Проверено на `macos-debug`: сборка `BagiChess`, `BagiMatch3`, `BagiEngineTests`, `ChessTests`, `Match3Tests` без предупреждений; `ctest` 145/145 (эталон 136 + 9 новых `FileSystemTest`); оба exe стартуют, в логе `Config 'SceneConfig' from games/<Name>/config/... overrides config/SceneConfig.xml`, одна зарегистрированная игра; репетиция выноса (игра как корень репозитория + `engine/`) собралась, 11/11 тестов, exe стартует.

Отличия от плана:
- **Генератор:** помимо `--reference-target`/`--manifest` добавлен признак `is_abstract` (чисто виртуальный метод в теле класса). Без него фабричная база без наследников (`IGame` в прогоне движка) регистрировала саму себя и не компилировалась. Версия кэша 2.0 -> 2.1. Режим `DIRS` у `add_reflection_target` убран (никто не использовал), общая часть вынесена в `_be_run_meta_generator`.
- **CMake:** добавлены `be_deploy_runtime_libs`, `CMakeLists.txt` каждой игры самодостаточен (блок `CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR` -> `project()` + `include(engine/cmake/BagiEngine.cmake)`), проект переименован в `BagiEngine`, корневой файл теперь `CMakeLists.txt` (не `CmakeLists.txt`), `add_subdirectory(games)` только если каталог есть.
- **Контент:** из движковой `MainScene` убраны 12 шахматных спрайтов (ссылались на `assets/chess_test.png`, который переехал в игру); осталось 3 квада.
- **Скрипты:** `format_sources.py`, `tidy_fix.py`, `strip_pch_includes.py` и диалог лаунчера обходят и `games/`.
- `.gitignore`: `games/*/config/imgui.ini`.

Не проверялось: Windows/Linux пресеты, `macos-release` (конфигурация и сборка), запуск с Vulkan-валидацией. Фаза 2 (вынос в отдельные репозитории) не начиналась.
