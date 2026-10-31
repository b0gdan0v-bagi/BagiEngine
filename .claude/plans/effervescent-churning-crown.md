# Вынос тестов в отдельный gtest-бинарь

## Context

Сейчас тесты — это `ITest`-классы, которые компилируются **внутрь production-библиотек** (`BECoreModule`, `ChessModule`, `Match3Module` берут их через `GLOB_RECURSE`) и запускаются при каждом старте игры из `CoreManager::OnApplicationPreInit()` (`TestManager::RunAllTests`, список в `config/TestsConfig.xml`). Проблемы:

- Тесты ничего не проваливают: `ASSERT`/`EXPECT` лишь шлют `AssertEvent`, а `Run()` в 8 из 10 тестов заканчивается безусловным `return true`. Результат только в логе.
- В Release `ENGINE_ENABLE_ASSERTS=OFF` превращает `ASSERT(TestFoo())` в `((void)0)`. Тела тестов не выполняются, и они «проходят» вакуумно.
- Тесты тормозят запуск игры и раздувают библиотеки.
- Нет `ctest`, фильтров, CI-кода возврата.

Цель: отдельный исполняемый файл на GoogleTest. Все 10 существующих тестов переезжают в него. Каркас `ITest`/`TestManager`/`TestsConfig.xml` остаётся для будущих runtime-тестов, которым нужен живой движок.

## Решения (согласованы с пользователем)

| Вопрос | Решение |
|---|---|
| Источник gtest | git-сабмодуль `external/googletest` |
| Старый раннер | Остаётся каркасом для runtime-тестов; все 10 тестов уходят в gtest |
| Расположение | Корневая папка `tests/` |
| `ResourceManagerTest` | Переносится в gtest через тестовый bootstrap движка |

## Согласование с планом `abstract-growing-honey.md` (BinarySerializer)

Оба плана правят `ReflectionTest.cpp/.h`, а проверка бинарного плана завязана на встроенный раннер («запустить `MyGame`, в логе `Results: 10 passed, 0 failed`»). Это меняет порядок и часть шагов.

- **Порядок: сначала BinarySerializer, потом перенос в gtest.** Бинарный план уже полностью описан под текущую раскладку, а его `Run()`-проверка через `MyGame` остаётся верной, пока тесты не переехали. Перенос `ReflectionTest` тогда забирает уже настоящие бинарные тесты, а не заглушку. Параллельно вести обе задачи нельзя: общий файл `ReflectionTest.cpp`, конфликт гарантирован.
- **Если же gtest идёт первым:** заглушку `TestBinarySerialization` (в `Run()` она закомментирована, сама функция возвращает `true` без проверок) переносить как `TEST(ReflectionTest, DISABLED_BinarySerialization)`, а не как зелёный тест, чтобы не выдавать пустую проверку за пройденную. Тогда раздел «Тесты» и «Порядок работы и проверка» бинарного плана перенацеливаются: тесты пишутся сразу в `tests/BECore/ReflectionTest.cpp` на gtest-макросах, а шаг 2 проверки — `BagiEngineTests --gtest_filter='Reflection*'` вместо `MyGame`.
- **Что из бинарного плана переносится в gtest как есть:** `TestBinarySerialization` (round-trip `Player`, `GameState`, контейнеры, `SetSkipDefaults`, размер бинарного меньше XML) и `TestBinaryErrors`. Цикл обрезаний буфера «длина 0…N-1 не падает» нужен в виде одного `TEST` с `ASSERT_*` внутри цикла; он как раз выигрывает от gtest (в Release старые `ASSERT` исчезали).
- **Не пересекается:** новые файлы `BinaryFormat.h`, `BinarySerializer.*`, `BinaryDeserializer.*` лежат в `src/Modules/BECore/Reflection/` (production-код, остаётся там), подхватываются `GLOB_RECURSE` и линкуются в тестовый exe через `BECoreModule`. Правки документации разведены: бинарный план правит `reflection-module.md`, этот — `tests-module.md`, `core-module.md`, `cmake-rules.md`, `CLAUDE.md`.

## Целевая структура

```
tests/
├── CMakeLists.txt            # цель BagiEngineTests, gtest_discover_tests
├── Common/
│   ├── TestMain.cpp          # main(): InitGoogleTest + глобальное Environment
│   ├── AssertBridge.h/.cpp   # AssertEvent -> ADD_FAILURE()
│   └── EngineEnvironment.h/.cpp  # bootstrap движка (только для ResourceManager)
├── BECore/   PoolStringTest, FormatTest, EnumUtilsTest, ReflectionTest(+.h с данными),
│             ReflectionEventTest(+.h), EventSubscriptionTest, TaskSystemTest, ResourceManagerTest
├── Chess/    MoveGeneratorTest
└── Match3/   Match3GameModelTest
```

## Шаги

### 1. Сабмодуль и корневой CMake
- `git submodule add https://github.com/google/googletest external/googletest` с фиксацией на релизном теге (например `v1.17.0`). Комментарий о фиксации по образцу SDL в `.gitmodules`.
- Корневой `CmakeLists.txt` (`CmakeLists.txt`, имя с маленькой «m»):
  - `option(ENGINE_BUILD_TESTS "Build gtest test executable" ON)`.
  - Если опция включена: `enable_testing()` и `add_subdirectory(external/googletest)` **после** блока `ENGINE_NO_EXCEPTIONS` (после строки ~105). Тогда gtest наследует `-fno-exceptions` и те же defines, что и движок, и не получает расхождения флагов с тестовыми TU.
  - Перед ним: `set(INSTALL_GTEST OFF)`, `set(BUILD_GMOCK OFF)` (gmock не нужен, пока нет моков), `set(gtest_force_shared_crt ON)` (иначе MSVC соберёт gtest со статическим CRT /MT, а движок идёт на /MD).
  - После `add_subdirectory(src/Modules/Match3)`: `if(ENGINE_BUILD_TESTS) add_subdirectory(tests) endif()`.

### 2. `tests/CMakeLists.txt` — цель `BagiEngineTests`
Повторить схему `MyGame` (`CmakeLists.txt:357-405`):
- Источники: `file(GLOB_RECURSE ... tests/*.cpp tests/*.h)`.
- `target_precompile_headers(BagiEngineTests REUSE_FROM BECoreModule)` и force-include `BECore/pch.h` (`/FI` для MSVC, `-include` для Clang/GCC). Тестовые TU включают движковые заголовки без `#include` базовых вещей, как и `ChessTypes.h`.
- `target_compile_definitions(PRIVATE IMGUI_VULKAN_AVAILABLE=1 REFLECTION_GENERATED=1)`. Первое нужно для совпадения с PCH, иначе `-Wclang-cl-pch`.
- `target_include_directories(PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/tests)` — первое для `<Generated/*.gen.hpp>`.
- Линковка: `$<LINK_LIBRARY:WHOLE_ARCHIVE,BECoreModule EventsModule TaskSystemModule>` (регистрации `AbstractFactory` через static-init иначе вырежет линкер), плюс `ChessModule`, `Match3Module`, `GamesModule`, `GTest::gtest`. `BECoreModule` тянет `SDL3-shared` транзитивно (проверено через `nm`: недостающих `SDL_*`/`Vk*`/`ImGui*` символов нет).
- macOS: `BUILD_RPATH "@executable_path"` как у `MyGame` (`libSDL3` лежит рядом из-за общего `CMAKE_LIBRARY_OUTPUT_DIRECTORY`). Windows: `copy_if_different` для `SDL3.dll`, `SDL3_image.dll` по образцу `MyGame`.
- `include(GoogleTest)` + `gtest_discover_tests(BagiEngineTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})`. Рабочая папка нужна из-за `FileSystem::FindRootDirectory`, который ищет `config/` вверх от CWD (`FileSystem.cpp:200-224`).
- `set_target_properties(... FOLDER "Tests")`.
- Если линковка потребует символов `VulkanModule` через `SDLModule` — добавить `VulkanModule` с `WHOLE_ARCHIVE`, как в `MyGame` (проверить на первой сборке).

### 3. Рефлексия для тестовых данных
`ReflectionTest.h` (`TestData::Player/Inventory/GameState`) и `ReflectionEventTest.h` (`BE_EVENT`-типы) требуют `.gen.hpp`. Генератор ограничен `CI/meta_generator/reflection_targets.json`.
- В записи `BECoreModule` заменить старые пути `.../Tests` (BECore, Chess, Match3) на `tests/BECore`, `tests/Chess`, `tests/Match3` в `source_dirs`/`scan_dirs`. Оставить `src/Modules/BECore/Tests` для каркаса `ITest`.
- Добавить `tests` в `include_dirs` (`compute_include_path` в `CI/meta_generator/core/generator.py:124` строит `#include` в `.gen.hpp` относительно этих корней).
- Сгенерированные файлы по-прежнему попадают в `${CMAKE_BINARY_DIR}/Generated`. Отдельный `add_reflection_target` для тестового exe **не нужен**: `.gen.hpp` — это заголовки, которые тест включает по `${CMAKE_BINARY_DIR}`.
- Сами тестовые классы больше не наследуют `ITest` и не используют `BE_CLASS`. `BE_CLASS` остаётся только на data-типах, которые сериализуются.

### 4. Мост `ASSERT`/`EXPECT` движка → gtest
Без него `ASSERT` внутри production-кода в gtest-бинаре — молчаливый no-op (подписчиков нет). `AssertMacros.h` шлёт `AssertEvent::Emit(file, line, expr, msg, type)`.
- `tests/Common/AssertBridge`: `SubscriptionHolder`-подписчик на `AssertEvent`, который вызывает `ADD_FAILURE_AT(file, line) << expression << ": " << message`. Для `FatalError` — `GTEST_FAIL_AT`/`FAIL()`.
- Ставить его в `Environment::SetUp()`. Отладочный брейк (`DebugBreakHandler`) не подписывается — в тестовом процессе его нет, в чём и смысл.

### 5. Bootstrap движка для `ResourceManagerTest`
Только этот тест требует живых `FileSystem`, `TaskManager`, `ConfigManager`, `ResourceManager`. `OnApplicationPreInit` принимает `PassKey<Application>`, а `Application` в `MyGame` — вызвать из тестового exe нельзя.
- В `CoreManager` (`GameManager/CoreManager.h/.cpp`):
  - Вынести тело `OnApplicationPreInit` (до `_testManager.RunAllTests()`) в приватный `InitializeCoreServices()`. `OnApplicationPreInit` вызывает его, затем `RunAllTests()` — поведение игры не меняется.
  - Добавить `void OnTestEnvironmentInit(PassKey<Tests::EngineEnvironment>)` (вызывает `InitializeCoreServices()`) и `void OnTestEnvironmentDeinit(PassKey<Tests::EngineEnvironment>)` (вызывает `TaskManager::Shutdown`). Для `PassKey` достаточно forward-declaration `namespace BECore::Tests { class EngineEnvironment; }`.
- `tests/Common/EngineEnvironment`: `::testing::Environment`, `SetUp()` вызывает `OnTestEnvironmentInit({})`, `TearDown()` — `OnTestEnvironmentDeinit({})`. Environment поднимается один раз на процесс; чистые unit-тесты от него не зависят.
- Побочный эффект: `LoggerConfig` включает `FileSink` с `engine.log` в CWD (`WORKING_DIRECTORY` = корень репозитория). Допустимо, но отметить в README к тестам. Если мешает — задать отдельный `LoggerConfig` для тестов на этапе реализации.
- Фикстура `ResourceManagerTest` вызывает `GetResourceManager().ClearCache()` в `SetUp()`, чтобы тесты не зависели от порядка (старый тест мутировал глобальный кэш).

### 6. Порт тестов
Правила для всех: один sub-тест (`TestFoo()`) → один `TEST(Suite, Case)`; `ASSERT(x, "msg")` → `ASSERT_TRUE(x) << "msg"` там, где следующая строка разыменовывает результат, иначе `EXPECT_*`; сравнения — `EXPECT_EQ/NE/...`. Блоки `TestCompileTime()` с `static_assert` выносятся на уровень файла (`.cpp`) — регресс ломает сборку, как раньше, — плюс пустой `TEST` для видимости в отчёте.

| Тест | Особенности порта |
|---|---|
| `PoolStringTest`, `FormatTest`, `EnumUtilsTest` | Механический порт. `TestColor` из заголовка `EnumUtilsTest.h` переносится в `.cpp` (анонимный namespace). |
| `EventSubscriptionTest` | Использует `static entt::dispatcher` внутри `EventBase<T>` — состояние общее на процесс. Каждый тест подписывается и отписывается через `SubscriptionHolder`, как сейчас. |
| `ReflectionEventTest` | Его `TestCompileTime` не `constexpr` и содержит рантайм-`ASSERT` (`ReflectionEventTest.cpp:17-38`) — перенести в обычные `TEST`. |
| `ReflectionTest` (969 строк) | Самый объёмный. Сейчас ошибки идут через `LOG_ERROR` + `return false` — заменить на `EXPECT_*`. Data-типы остаются в `ReflectionTest.h` с `BE_CLASS`/`BE_REFLECT_FIELD`. Бинарные тесты (`TestBinarySerialization`, `TestBinaryErrors`): переносятся настоящими, если BinarySerializer уже реализован; иначе заглушка идёт как `DISABLED_` (см. раздел согласования выше). |
| `TaskSystemTest` | Свои `ThreadPool`, не `TaskManager`. 100 ms `sleep` на строке ~182 оставить, но проверить на флейки. |
| `ResourceManagerTest` | Фикстура на базе `EngineEnvironment`, см. шаг 5. Читает `config/ApplicationConfig.xml` с диска. |
| `MoveGeneratorTest`, `Match3GameModelTest` | Чистая игровая логика. `Match3GameModelTest` использует тестовые хуки `SetCellForTest`/`ResetScoreForTest` в production-коде (`Match3GameModel.h:89-92`) — оставить как есть. |

После порта удалить из `src/Modules/BECore/Tests/` все `*Test.{h,cpp}`, из `Chess/src/Chess/Tests/` и `Match3/src/Match3/Tests/` — тоже. Тесты перестают попадать в `GLOB_RECURSE` библиотек.

### 7. Каркас `ITest` и его минимальный потребитель
- Остаются: `ITest.h/.cpp`, `TestManager.h/.cpp`, вызов в `CoreManager`, `config/TestsConfig.xml`.
- **Риск:** `ITest` помечен `FACTORY_BASE`. Генератор делает `CORE_ENUM(..., uint8_t, <список наследников>)`. Если наследников нет, получится `CORE_ENUM(Name, uint8_t, )` с пустым списком, и сборка, скорее всего, сломается. Проверить на первой же сборке (пустого ли enum достаточно для краха).
- Если сломается — оставить один крошечный `ITest`-наследник, например `EngineSmokeTest` в `BECore/Tests/` (проверяет, что после PreInit подняты `ConfigManager`/`Logger`/`ResourceManager`). Он же — живой пример runtime-теста для документации и единственная запись в `TestsConfig.xml`. Если enum с нулём значений собирается — `TestsConfig.xml` остаётся пустым `<tests/>`, и в `TestManager::RunAllTests` предупреждение `LOG_WARNING("No tests loaded")` снижается до `LOG_INFO`.
- `ValidTest` и `tests-module.md` обновить: раннер описывается как «runtime-тесты в живом движке».

### 8. Пресеты и CI-удобство
- `CMakePresets.json` (version 6): сейчас `build-base` собирает только `MyGame`. Добавить скрытый `build-tests-base` с `targets: ["BagiEngineTests"]` и видимые `<имя>-tests` build-пресеты (macOS: `macos-debug-tests`, `macos-release-tests`; Windows — по аналогии) плюс секцию `testPresets` (`ctest --preset`, `output.outputOnFailure: true`).
- Обновить список пресетов в `CLAUDE.md` и в навыке `.claude/skills/build/SKILL.md` (правило из `cmake-rules.md`).
- В отличие от игры, тест-бинарь запускается напрямую: `build/ClangmacOS/BagiEngineTests --gtest_filter=...`, либо `ctest --preset macos-debug`.

### 9. Документация и правила
- `CLAUDE.md`, раздел «Running Tests»: теперь стандартный gtest (`cmake --build --preset ...-tests`, `ctest --preset ...`). Убрать «there is no standalone test binary».
- `.claude/rules/tests-module.md`: два режима — gtest (unit/модульные) и `ITest` (runtime). Путь `tests/**/*` в `paths:` frontmatter. Чек-лист нового gtest-теста. Исправить устаревшее «auto-generated from ITest inheritance tree».
- `.claude/rules/core-module.md`: порядок инициализации и фраза про тесты.
- `.claude/rules/cmake-rules.md`: gtest в таблице внешних зависимостей (сабмодуль `external/googletest`); `ENGINE_BUILD_TESTS`; заметить, что `Math` как CMake-модуля нет (это уже расхождение в доках, по пути поправить).
- `.gitmodules` — запись для googletest.

### 10. Завершение (по правилам CLAUDE.md)
- Навык `clang-format` на изменённых файлах (`tests/`, `CoreManager`, `TestManager`).
- Навык `strip-pch-includes` на `tests/` (сначала dry-run в подпапке) — тесты включаются через PCH.
- `.clang-format:57` уже содержит правило порядка include для `gtest/` — это совпадает.

## Критичные файлы

- `CmakeLists.txt` (корень) — опция, gtest, `enable_testing`, `add_subdirectory(tests)`.
- `tests/CMakeLists.txt` (новый) — цель, PCH, WHOLE_ARCHIVE, discover.
- `src/Modules/BECore/GameManager/CoreManager.{h,cpp}` — вынос `InitializeCoreServices`, вход для тестов.
- `src/Modules/BECore/Tests/` — остаются только `ITest`, `TestManager` (+ возможный `EngineSmokeTest`).
- `CI/meta_generator/reflection_targets.json` — пути `tests/*`, `include_dirs`.
- `CMakePresets.json`, `CLAUDE.md`, `.claude/rules/{tests-module,core-module,cmake-rules}.md`, `.claude/skills/build/SKILL.md`.

Переиспользуемое: `SubscriptionHolder` / `EventBase` (`Events/`) для моста `AssertEvent`; паттерн PCH-force-include из `Chess/CMakeLists.txt:19-28`; `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>` из корня (`CmakeLists.txt:381-396`); `PassKey` (`BECore/Utils/PassKey.h`).

## Неочевидные риски

1. **Пустой `CORE_ENUM` для `ITest`** — см. шаг 7; проверяется первой сборкой.
2. **Версионный шов PCH:** тестовый TU должен собираться с теми же `-D`/флагами, что `BECoreModule` (`IMGUI_VULKAN_AVAILABLE=1`), иначе `-Winvalid-pch`/`-Wclang-cl-pch`.
3. **Глобальное состояние между тестами:** `PoolString` (глобальная таблица интернирования), `static entt::dispatcher` в `EventBase<T>`, `AbstractFactory<T>`, `LogRingBuffer`. Это не мешает при последовательном запуске, но делает небезопасными `--gtest_shuffle`/параллельный `ctest -j` внутри одного процесса. `gtest_discover_tests` запускает каждый тест отдельным процессом, так что `ctest -j` безопасен, а `--gtest_shuffle` в одном процессе — под вопросом.
4. **Линковка:** `BECoreModule` публично тянет `SDL3-shared` и `imgui` (с Vulkan-бэкендом), поэтому конфигурация тестов требует тех же SDK, что и игра. Развязка `BECore` от SDL/ImGui — отдельная большая задача, в этот план не входит.
5. **Конфигурация Release:** gtest-макросы работают всегда, но движковые `ASSERT`/`EXPECT` в Release отключены (`ENGINE_ENABLE_ASSERTS=OFF`). Мост из шага 4 в Release слеп; основные проверки идут через `EXPECT_*`/`ASSERT_*` самого gtest, поэтому тесты не становятся вакуумными.
6. **Каталог вывода:** `build/{Compiler}{Platform}/` общий для Debug и Release, поэтому `BagiEngineTests` перезаписывается так же, как `MyGame`.

## Проверка

1. `git submodule update --init external/googletest`, затем `cmake --preset macos-debug` — конфигурация проходит, в выводе есть `Reflection enabled ... files generated`, тестовые `.gen.hpp` появились в `build/macos-debug/Generated/`.
2. `cmake --build --preset macos-debug` — игра собирается как раньше (`MyGame` без тестов; бинарь меньше).
3. `cmake --build --preset macos-debug-tests` — собирается `BagiEngineTests`.
4. `./build/ClangmacOS/BagiEngineTests` — все тесты зелёные. Перед переносом снять базовую линию: запустить `MyGame` на старой раскладке и записать, какие тесты и подтесты проходят (по логу `[TestManager]`). Сверить список с `--gtest_list_tests`: каждый старый подтест должен иметь парный `TEST`, ни один не пропал. Если BinarySerializer уже влит, в списке есть бинарные тесты и нет строки `SKIPPED`/`DISABLED_`.
5. `ctest --preset macos-debug --output-on-failure` — те же тесты, нулевой код возврата.
6. Негативный контроль: временно сломать один `EXPECT_EQ` и один движковый `ASSERT` внутри тестируемого кода — оба должны дать FAILED и ненулевой код возврата (проверка моста из шага 4).
7. Release-проверка: `cmake --preset macos-release` + сборка тестов + запуск. Тесты должны выполняться по-настоящему (не вакуумно), потому что gtest-макросы не зависят от `ENGINE_ENABLE_ASSERTS`.
8. Запуск игры (`MyGame`): стартует без прогона 10 тестов, в логе нет `[TestManager] Running: ...` для старых тестов.
9. Windows-пресет (`windows-msvc-debug` или `windows-vs2022-debug`) на отдельной машине — проверка `gtest_force_shared_crt` и копирования DLL.

## Статус: реализовано (macOS Debug и Release проверены)

Результат: 81 gtest-тест (+1 `DISABLED_BinarySerialization`), `ctest --preset macos-{debug,release}` зелёный, `MyGame` собирается и при старте гоняет только `EngineSmokeTest`. Windows-пресеты не проверялись (шаг 9 проверки).

### Отличия от плана

- **Шаг 5:** движок поднимается лениво (`EngineEnvironment::EnsureCoreServices()` из `SetUpTestSuite` фикстуры `ResourceManagerTest`), а не в `Environment::SetUp()`. Так чистые тесты не читают `config/` и не пишут `engine.log`. `Environment::SetUp()` ставит только `AssertBridge`. Дополнительно `OnTestEnvironmentInit` отключает `DebugBreakHandler`: без этого упавший `ASSERT` убил бы процесс тестов трапом.
- **Шаг 7 (риск №1 сбылся, но иначе):** пустого `CORE_ENUM` нет, фабрика `ITest` регистрируется в `.gen.hpp` наследников. Но если у `FACTORY_BASE` нет наследников, генератор регистрирует сам абстрактный `ITest` и сборка падает. Поэтому оставлен `EngineSmokeTest` (runtime-пример, единственная запись в `TestsConfig.xml`).
- **Шаг 8:** отдельные build-пресеты не нужны: `--target BagiEngineTests` в командной строке заменяет `targets` пресета (проверено). Добавлены только `testPresets`.
- **Шаг 2:** `WORKING_DIRECTORY` у `gtest_discover_tests` не задаётся (ни аргументом, ни в `PROPERTIES`): иначе `cmake_test_discovery_*.json` пишется в корень репозитория. Тесты идут из `build/<preset>/tests`, `FileSystem` находит `config/` подъёмом по родителям.
- **Шаг 6, `ResourceManagerTest.AsyncLoading`:** оригинал вызывал `task.GetResult()` на корутине, которая паркуется в пуле потоков (`SwitchToBackground`); `GetResult()` в цикле делает `resume()` и гоняется с пулом (`bad_variant_access`). Тест запускается через `TaskManager::Run` и ждёт `TaskHandle`. Падал ли оригинал при старте игры, не проверялось.
- **Шаг 6, `TaskSystemTest`:** фиксированный `sleep 100 ms` заменён опросом условия с таймаутом.
- **Шаг 10:** `strip_pch_includes.py` обрабатывает только `src/` и `config/`, `tests/` пропускает; include'ы в `tests/` проверены вручную по `pch.h`.
- Добавлена понятная ошибка конфигурации, если сабмодуль `external/googletest` не инициализирован.

### Остаётся

- План `abstract-growing-honey.md` (BinarySerializer): его разделы «Тесты» и «Порядок работы и проверка» теперь нужно перенацелить на `tests/BECore/ReflectionTest.cpp` (gtest-макросы) и `BagiEngineTests --gtest_filter='Reflection*'`; заглушку `DISABLED_BinarySerialization` заменить настоящими тестами.
- Проверка на Windows-пресетах (`gtest_force_shared_crt`, копирование DLL).

## Статус: бинарная сериализация (BinarySerializer / BinaryDeserializer)

После переноса тестов закрыт отложенный пункт: `DISABLED_BinarySerialization` заменён реализацией и тестами.

- Код: `Reflection/BinaryFormat.h`, `BinarySerializer.h/.cpp`, `BinaryDeserializer.h/.cpp` (production-код в `BECoreModule`, подхвачен `GLOB_RECURSE`).
- Формат: заголовок `"BEBN"` + `u16 version` + `u32 payloadSize`, записи `u8 tag | u16 nameLen | name | payload`, размеры объектов и массивов патчатся при закрытии. Отличие от черновика в плане BinarySerializer: в заголовок добавлен `payloadSize`, иначе обрезание ровно по границе верхнеуровневой записи не отличить от целого потока.
- Поведение: поиск по имени, повторная запись читается как last-write-wins, `Read` сообщает «Element not found», `ReadAttribute`/`BeginObject` молчат, несовпадение типа всегда ошибка (без неявного расширения).
- Устойчивость: весь поток валидируется при загрузке (заголовок, размеры, теги, глубина <= 128), после успешной загрузки чтение не выходит за границы.
- Тесты: 29 кейсов `ReflectionTest.Binary*` в `tests/BECore/ReflectionTest.cpp`. Негативный контроль: пять намеренно внесённых дефектов (проверка `payloadSize`, лимит глубины, счётчик элементов массива, last-write-wins, проверка типа) каждый ловится своими тестами. Первый прогон показал, что без положительного контроля `BinaryRejectsBadHeaders` проходил бы вакуумно, поэтому контроль добавлен.
- Результат: `ctest --preset macos-debug` и `macos-release` — 110/110.
- Не проверено: сборка под MSVC/Windows (в формате используются `memcpy` и `static_assert` на little-endian), ASan/UBSan-прогон мутационных тестов.

