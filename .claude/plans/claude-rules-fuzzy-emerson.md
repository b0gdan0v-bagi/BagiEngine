# Ревизия `.claude/rules/` и перенос правил

## Context

Правила в `.claude/rules/` отстают от кода: часть описывает API, которого уже нет, часть дублирует `CLAUDE.md`, а три файла
(`ai-assistant.md`, `code-style.md`, `enum-rules.md`) грузятся при чтении любого `.h`/`.cpp` и тратят контекст на повтор.
Сверка с кодом дала список реальных расхождений (ниже). Решения пользователя:

- схлопнуть дубли в `CLAUDE.md`;
- ужать учебные правила (`resource-module.md`, `reflection-module.md`);
- перенести однокаталожные правила во вложенные `CLAUDE.md` рядом с кодом;
- править **все 15 файлов сразу**, в том числе три файла с чужими незакоммиченными правками (см. «Риски»).

Язык: правила и `CLAUDE.md` пишутся по-английски (правило проекта), отчёт пользователю по-русски. Правки только в `.md`,
C++ не трогаем, поэтому `clang-format` и `strip-pch-includes` не нужны.

## Целевая раскладка

Критерий переноса: правило про **один каталог** уезжает во вложенный `CLAUDE.md` (Claude Code подгружает его лениво, когда
читается файл из этого поддерева, то есть так же, как `paths:`). Правило про **несколько каталогов или маску файлов** остаётся
в `.claude/rules/` с `paths:`.

| Было | Станет |
|------|--------|
| `rules/ai-assistant.md`, `code-style.md`, `enum-rules.md` | удалить; уникальное влить в `CLAUDE.md` |
| `rules/core-module.md` | `src/Modules/BECore/CLAUDE.md` |
| `rules/logger-module.md` | `src/Modules/BECore/Logger/CLAUDE.md` |
| `rules/resource-module.md` | `src/Modules/BECore/Resource/CLAUDE.md` (214 → ~75 строк) |
| `rules/events-module.md` | `src/Modules/Events/CLAUDE.md` |
| `rules/tasksystem-module.md` | `src/Modules/TaskSystem/CLAUDE.md` |
| `rules/sdl-module.md` | `src/Modules/SDL/CLAUDE.md` |
| `rules/cmake-rules.md`, `tests-module.md`, `reflection-module.md` (189 → ~110), `reflection-events.md`, `rendering-module.md`, `widgets-module.md` | остаются в `rules/`: они про несколько каталогов или маски файлов |

Во вложенных `CLAUDE.md` frontmatter не нужен. Не создавать `CLAUDE.md` в вендорных каталогах (`EABase`, `EnTT`, `fmt`, `imgui`, `pugixml`).
Переносить через `git mv`, чтобы история файла сохранилась; коммит не делать.

## Найденные расхождения (что исправляем)

**События и логирование**
- `events-module.md`: `SubscribeScoped` нет в коде. `EventBase::Subscribe` требует `PassKey<SubscriptionHolder>`, так что вручную
  `MyEvent::Subscribe<...>(this)` не скомпилируется, подписка идёт только через `SubscriptionHolder::Subscribe<Event, &H>(this)`.
  `Emit` строит `Derived{args...}`, поэтому «fields + constructor» неверно, это агрегатная инициализация. В таблице категорий
  нет `InputEvents.h`, `SceneEvents.h` и `SDL/CoreSDL/SDLEvents.h`.
- `logger-module.md`: `LOG_*` принимают один `string_view`, форматирование делается как `LOG_INFO("x {}"_format(v))`. Тип синка
  в `LoggerConfig.xml` это `OutputSink`/`ConsoleSink`/`FileSink`, а не `Console`/`File`/`Output`. Нет `LogRingBuffer` (lock-free MPSC,
  при переполнении записи теряются) и `LOG_FATAL`. `Fatal` делает `LogEvent::Flush()` и `FlushLogsEvent::Emit()`.

**Ядро, ресурсы, задачи**
- `core-module.md`: `ConfigManager` хранит `UnorderedPoolMap<ConfigEntry>`, а не `...<IntrusivePtrAtomic<XmlDocument>>`. Список
  геттеров `CoreManager` неполный (нет `GetWidgetManager`, `GetRendererManager`, `GetSceneManager`, `GetTextureLibrary`,
  `GetTaskManager`, ...), заменить ссылкой на заголовок. Секции «PoolString Usage» и «File Organization» дублируют `CLAUDE.md`.
- `tasksystem-module.md`: `Delay(duration)` нет, есть `DelayMs`/`DelaySec`. Жизненный цикл: `Initialize` вызывается из
  `InitializeCoreServices()` (то есть в PreInit и в `OnTestEnvironmentInit`), а не в `OnApplicationInit`; `Shutdown` ещё и в
  `OnTestEnvironmentDeinit`; все три метода принимают `PassKey<CoreManager>`. «Don't block main thread with `Wait()`» расходится
  с кодом: `ConfigManager` делает `TaskGroup::WaitAll()` на главном потоке при старте.
- `resource-module.md`: `ResumeOnThreadPool()` нет, это `SwitchToBackground()`. `LOG_INFO("...{}", a, b)` не компилируется.
  Загрузчики регистрируются сгенерированной `ResourceLoaderFactory` в `ResourceManager::Initialize()`, а не вручную через
  `RegisterLoader(New<...>())`. Нет `TextureLibrary` (`Resource/TextureLibrary.h`, `config/TextureLibrary.xml`).
  `ResourceManagerTest` теперь в `tests/BECore/`. Раздел «Future Enhancements» удалить (дорожная карта, не правило).

**Рефлексия**
- `reflection-module.md`: `FieldSerializer.h` / `SerializeField` нет, это `Reflection/DataAccessor.h` (`Ser::Save/Load`,
  `DataAccessor<T>`, `FactoryTraits`). В примере ошибок `err.fieldName`, а поле называется `path`. В примере с массивами
  пропущены `BeginArrayElement`/`EndArrayElement`. `IArchive` в коде нет (есть общая база `IArchiveBase`), строку «old IArchive
  deprecated» убрать. Не описаны `AcceptPropertyVisitor`/`IPropertyVisitor`, `ForEachFieldStatic`, `GetTypeMeta()`. Строка
  «Binary planned» пока верна (файлов `Binary*` в `Reflection/` нет), её не трогать. `paths:` содержит мусорные
  `**/*Test*.h|cpp`, из-за них правило на 189 строк грузится при чтении любого теста. Заменить на `src/Modules/BECore/Reflection/**/*`,
  `CI/meta_generator/**/*`, `tests/BECore/Reflection*`.
- `reflection-events.md`: `BE_EVENT` теперь генерирует ещё `Serialize`/`Deserialize` (в таблицах их нет). «Option 1 EventBase /
  Option 2 BE_EVENT» подаётся как альтернатива, хотя для `Emit` нужен `EventBase<T>`; сверить с `Events/SceneEvents.h`. «Zero
  overhead» верно только относительно `BE_CLASS`: `EventBase<T>` наследует `BaseEvent` с виртуальным деструктором, то есть
  vptr есть. `GetStaticTypeHash` в рабочем коде не используется (только в тестах), формулировку «for efficient dispatch» смягчить.

**Рендер, SDL, виджеты**
- `sdl-module.md`: `MainWindowAccessor` лежит в `BECore/MainWindow/`, а не в SDL. Окно и рендерер давно разделены:
  `SDLRendererBackend` реализует `IRenderer`. Нет `SDL3ApplicationFabric`, `SDLUtils`, `SDLWindowConfig`, `SDLEvents.h`.
  Порядок ImGui-кадра обратный: `ImGuiWidget::OnNewFrame` шлёт `RenderEvents::ImGuiNewFrameEvent`, а
  `SDLRendererBackend::OnImGuiNewFrame` вызывает `ImGui_ImplSDL3_NewFrame()` **перед** `ImGui_ImplSDLRenderer3_NewFrame()`.
  Секции Texture Pipeline и Error Handling дублируют `rendering-module.md`, оставить ссылку.
- `rendering-module.md`: `paths:` покрывает `*Texture*` только для SDL, для Vulkan добавить `*Texture*`. Не упомянуты `IRenderTarget`,
  `RendererManager`, то, что `SDLTextureLoader`/`VulkanTextureLoader` реализуют `IResourceLoader`, и шейдерный путь Vulkan
  (GLSL → SPIR-V → заголовок через `Vulkan/cmake/SpvToHeader.cmake`).
- `widgets-module.md`: `Initialize(const XmlNode&)` теперь `Initialize(IDeserializer&)`. Нет `DrawScreen()` (порядок
  `UpdateAll` → `DrawScreenAll` → `DrawAll`). В `WidgetsConfig.xml` нет `name`/`priority`, только `type` и дочерние элементы
  (`<clearColor .../>`). Не описаны `ImGuiPropertyVisitor` и `ConfigEditor` (`ConfigFieldSchema`, `FieldInspectors`, `StagedChanges`).

**CMake, тесты, CLAUDE.md**
- `cmake-rules.md`: `Math` не CMake-модуль (в `CmakeLists.txt` его нет), реальный порядок: BECore, Events, TaskSystem, SDL, Vulkan,
  Games, Chess, Match3, tests. В таблице внешних зависимостей нет `Vulkan-Headers`, `Vulkan-Loader` (каталог есть, записи в
  `.gitmodules` нет, уточнить статус) и `googletest`. Нет опций `ENGINE_BUILD_TESTS`, `ENGINE_ENABLE_ASSERTS/EXPECTS`. Не
  сказано главное для нового модуля: нужна запись в `CI/meta_generator/reflection_targets.json` и вызов `add_reflection_target`.
  Проверить `/FI`, `/W4`, `-Wall -Wextra` и `REUSE_FROM` по корневому `CmakeLists.txt`, `tests/CMakeLists.txt`,
  `Chess/CMakeLists.txt` (у Chess другой паттерн PCH).
- `tests-module.md`: использует `ctest --preset <preset>`; проверить, есть ли `testPresets` в `CMakePresets.json`. Если нет,
  привести к `ctest --test-dir build/<preset>` как в `CLAUDE.md`.
- `CLAUDE.md`: цепочка `BECore → Math → Events → ...` устарела. Таблица Key subsystems кладёт `Events/` и `TaskSystem/` внутрь
  `BECore/`, хотя это соседние модули `src/Modules/Events` и `src/Modules/TaskSystem`. Строка про `.claude/rules/` перечисляет
  «enums» и не знает про вложенные `CLAUDE.md`.

## Что вливаем в CLAUDE.md

- Из `ai-assistant.md`: язык (код, документация и коммиты на английском, объяснения пользователю по-русски); строки таблицы
  Pattern Selection, которых нет в «Key patterns» (конфиги, сериализация, логирование, enum, форматирование строк); пункты «Don't
  Suggest» добавить в DON'T (без `+` для `eastl::string`, `Format()`, без магических чисел).
- Из `code-style.md`: расширенная таблица нейминга (константы, template-параметры, `_intern`), правила `*`/`&`, группировка
  include. Формат проверить по `.clang-format` (в правиле «~120 символов», `ColumnLimit` может быть другим). Противоречие «`std::optional`
  vs EASTL» разрешить по тому, что реально используется в коде. Секцию про assert-макросы не копировать: она уже есть в `tests-module.md`.
- Из `enum-rules.md`: строка про `EnumUtils<E>` (`ToString`, `FromString`, `Cast`, `ToPoolString`); сверить имена с
  `BECore/Utils/EnumUtils.h`. Запрет `enum class` в `CLAUDE.md` уже есть.
- Прирост `CLAUDE.md`: не больше ~30 строк, без повторов.

## Порядок выполнения

1. **Preflight.** `git status --short .claude src` и `git diff -- .claude/rules`. Перечитать текущие версии `core-module.md`,
   `tests-module.md`, `cmake-rules.md` (в них чужие правки, брать за основу рабочую копию, не `HEAD`). Проверить, влит ли план
   TaskSystem (`TaskHandle::Wait`, `CancellationToken::CreateLinked`) и появились ли файлы `Reflection/Binary*`.
2. **Подтвердить непроверенные факты** перед тем, как их писать: `Format()` и `_format` в `BECore/Format/Format.h`, путь
   `BECore/Utils/Singleton.h`, API `EnumUtils`, `ColumnLimit`, `New<T>` против `MakeIntrusive<T>`, как строится `ResourceLoaderFactory`,
   `LogSinkType`, выбор бэкенда в `RendererManager`, `SceneEvents.h`, `testPresets`, порядок `UpdateAll`/`DrawScreenAll`/`DrawAll` в
   `Application`, список `FACTORY_BASE`.
3. **CLAUDE.md**: влить уникальное, поправить цепочку модулей и таблицу subsystems (Events/TaskSystem вынести из BECore),
   переписать строку про правила (вложенные `CLAUDE.md` + оставшиеся `rules/`).
4. **Переносы** (`git mv`, затем правка содержимого и удаление frontmatter) в 6 вложенных `CLAUDE.md`.
5. **Правки остающихся** 6 файлов в `rules/` (список расхождений выше), сузить `paths:` у `reflection-module.md`.
6. **Удалить** `ai-assistant.md`, `code-style.md`, `enum-rules.md`.
7. **Подправить ссылки на пути правил** в планах `abstract-growing-honey.md` (`tasksystem-module.md` уехал) и
   `effervescent-churning-crown.md`: одна строка-примечание вверху, чтобы агенты, которые по ним работают, не пересоздали старый файл.
8. Если TaskSystem-план уже влит: добавить в `TaskSystem/CLAUDE.md` строку про `Wait()` и `CreateLinked()` (как в плане); если нет,
   оставить как есть и сказать об этом в отчёте.

## Риски

- **Параллельные правки.** `tasksystem-module.md` и `reflection-module.md` ждут правок от плана TaskSystem/Binary, а три файла
  (`core`, `tests`, `cmake`) уже изменены gtest-задачей и не закоммичены. Если агент выполнит `Write` по старому пути
  `rules/tasksystem-module.md`, он молча воссоздаст удалённый файл. Отсюда шаг 7 и обязательный preflight. Если за время работы
  появятся новые чужие изменения в этих файлах, остановиться и показать diff, а не перезаписывать.
- **Ленивая подгрузка вложенных `CLAUDE.md`.** Они попадают в контекст только после чтения файла из своего поддерева. Это та же
  семантика, что у `paths:`, но проверить её можно только в живой сессии (см. проверку).
- `GLOB_RECURSE` в модулях собирает только `*.cpp`/`*.h`, `.md` в каталогах модулей сборку не затронут; убедиться, что парсер
  `meta_generator` тоже читает только C++ (расширения в `CI/meta_generator/core/parser.py`).

## Проверка

1. Скрипт в scratchpad: из каждого изменённого `.md` вытащить идентификаторы в обратных кавычках и пути файлов и проверить, что они
   находятся в `src/`, `tests/`, `CI/`, `config/`. Нулевой остаток означает, что «призраков» (как `SubscribeScoped` или
   `FieldSerializer.h`) больше нет.
2. Для каждого оставшегося `rules/*.md` проверить, что каждая маска `paths:` совпадает хотя бы с одним существующим файлом.
3. `ls .claude/rules` должен показать ровно 6 файлов; `find src -name CLAUDE.md -not -path '*/EABase/*'` ровно 6 вложенных. Сумма
   строк правил и вложенных файлов около 700 против 1243 исходных; прирост `CLAUDE.md` не больше ~30 строк.
4. `git status --short`: переносы видны как rename, файлов вне `.claude/`, `CLAUDE.md` и вложенных `CLAUDE.md` нет, коммитов нет.
5. Вручную в живой сессии: прочитать файл из `src/Modules/Events/`, `.../TaskSystem/`, `.../BECore/Resource/` и выполнить `/memory`.
   Вложенные `CLAUDE.md` должны появиться в списке загруженных. Сделать это я не могу, сессия неинтерактивная.

## Вне объёма (замечено попутно, без вашего подтверждения не трогаю)

- `src/Modules/Math/src/Math/Color.cpp` отслеживается в git, не входит в сборку и дублирует `BECore/Math/Color.cpp`. Предлагаю `git rm`.
- `BECore/Reflection/TypeTraits.h:54`: комментарий ссылается на несуществующий `IArchive&`.
- `.claude/settings.local.json` содержит разрешения с путями `/d/AHobbyProjects/...` (чужая машина).
- Нет правил для `Vulkan`, `Games`/`Chess`/`Match3` (`IGame`), `Scene` (`SceneNode`/`IComponent`), `Config`/`FileSystem`.
  Могу завести вложенные `CLAUDE.md` отдельной задачей.
