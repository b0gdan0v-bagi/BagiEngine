# Внешнее управление игрой: MCP, WebSocket, headless, Chess против Stockfish

## Context

Запись TODO.md «No external control» (строки 56-77) и пункт 2 роадмапа: игру должен уметь разрабатывать, запускать и тестировать модель (через MCP) или внешняя программа (WebSocket + JSON) без человека у окна. Критерий приёмки: партия Chess против Stockfish. Основа уже есть: `Time::Advance`, режим `mode="Game"`, `TaskManager` с очередью главного потока. Нет: канала управления, интерфейса игры, шага кадров, headless, чтения пикселей, легальных шахмат.

Все факты ниже проверены чтением кода (три разведки + проверка `Application.cpp`, `CoreManager.cpp`, `main.cpp`, `Match3GameModel.cpp`). Игра не запускалась, поэтому всё про macOS headless помечено «проверить спайком».

## Решения владельца (получены)

| Вопрос | Решение |
|--------|---------|
| Транспорт | Свой RFC 6455 (рукопожатие SHA-1 + base64, text/ping/pong/close, сокеты POSIX/Winsock), без внешней библиотеки |
| JSON | **nlohmann/json**, исключения выключены (см. «Правила nlohmann без исключений») |
| Порядок | Сначала движок (этапы 1-5, доказываем на Match3), потом шахматы (6-8), потом MCP и Stockfish (9) |
| Headless | `--headless` форсирует `SDLRendererBackend`: скрытое окно + software-рендер; скриншот через `SDL_RenderReadPixels`. Vulkan-readback отложен до решения по бэкендам |

## Решения по умолчанию (не спрашивал, скажите, если не так)

- Match3: заменить `std::uniform_int_distribution` на `1 + _rng() % kGemTypeCount` (распределение не переносимо между stdlib; тесты от конкретных значений seed не зависят, проверено). Платформо-независимый результат для (seed, список действий). Меняет доски для существующих seed.
- Chess: автоничья по троекратному повторению и правилу 50 ходов, без «заявления»; превращение пешки при клике мышью по умолчанию в ферзя; взятие на проходе, рокировка, превращение, мат, пат, недостаточный материал.
- Безопасность: bind только на `127.0.0.1`, проверка `Origin`, без токена, выключено по умолчанию.
- Имена: модуль `RemoteControl`, порт 8765, флаги `--control`, `--control-port=N`, `--headless`, `--manual-clock`, атрибуты `ApplicationConfig.xml`: `control`, `controlPort`, `headless`, `manualClock`.
- Python-зависимости MCP в отдельном `tools/control/requirements.txt`. Коммит `.mcp.json` в корень репозитория: решить на этапе 9, не раньше.

## Архитектура

### A1. Ядро команд (`BECore/Control/`, член `CoreManager`)

`BECore` не видит `IGame` (слой ниже `Games`), поэтому ядро общее, обработчики регистрируют модули выше.

- `ControlService` (обычный класс, не singleton, чтобы тесты создавали свой), `CoreManager::GetControlService()`.
- Очередь: `std::mutex` + `eastl::deque`, строгий FIFO. `TaskScheduler::Schedule` для команд **не подходит**: очередь главного потока это `priority_queue` без стабильности, задачи, поставленные из задачи, идут в следующий кадр.
- Drain на главном потоке в точке «1b» кадра: между `ProcessEvents` и `TaskManager::Update` (`CoreManager.cpp:69-70`). Команда видит состояние конца прошлого кадра плюс ввод этого кадра; применённое действие подхватывается логикой и рендером того же кадра.
- Барьер: `Drain()` останавливается на первой отложенной команде и продолжает после её завершения, порядок конвейера клиента сохраняется. `quit` обходит барьер.
- Отложенный ответ (шаг N кадров, скриншот) без повторного входа в `OnGameCycle`: обработчик вызывает `call.DeferFrames(frames, secondsPerFrame, done)`, счётчик уменьшает `ControlService::EndFrame()` после `LogEvent::Flush()`; при 0 вызывается `done`, ответ уходит через `ReplyFn`. При остановке все ожидающие вызовы получают `shutting_down`.
- `ReplyFn` и event-sink вызываются только на главном потоке; транспорт кладёт их в свою очередь отправки.
- На ввод клиента никаких `ASSERT/EXPECT/FATALERROR`; ошибки клиента это `std::expected<..., ControlError{code, message}>`. Имена методов в `eastl::hash_map<eastl::string, ...>`, **не** `PoolString` (клиент не должен раздувать пул).
- Протокол: запрос `{"id":1,"method":"perform_action","params":{...}}`, ответ `{"id":1,"ok":true,"result":{...}}` или `{"id":1,"ok":false,"error":{"code":"illegal_action","message":"..."}}`, push `{"event":"state_changed","data":{...}}`. Коды ошибок строками.
- Встроенные методы в BECore: `ping`, `status` (кадр, время, режимы), `step`, `quit`. Игровые методы (`list_games`, `switch_game`, `reset`, `get_state`, `list_actions`, `perform_action`, `set_state`) регистрирует `Games/GameControl.cpp` из `GameManager::Initialize()`. `screenshot` регистрирует `SDL3ApplicationFabric::Create` (только там есть рендерер и PNG-энкодер).

### A2. Правила nlohmann без исключений

- Вендорить `nlohmann/json.hpp` + `json_fwd.hpp` версии 3.11.3 в `src/Modules/nlohmann/` (проверить SHA-256 по релизу), интерфейсный CMake-таргет с `JSON_NOEXCEPTION`. Определение должно быть одинаковым во всех единицах трансляции (ODR). В `pch.h` **не** добавлять (тяжёлый).
- В заголовках интерфейсов только `<nlohmann/json_fwd.hpp>`; полный `json.hpp` в `.cpp`. `BECore/Control/Json.h` даёт `using Json = nlohmann::json;` и небросающие помощники: `JsonString(const Json&, key) -> std::optional<eastl::string>`, `JsonInt`, `JsonObject` и т.п.
- Разбор входа только `Json::parse(text, nullptr, /*allow_exceptions=*/false)` + проверка `is_discarded()`. Доступ только после `is_object()/contains()/find()/is_*()`. Запрещено на данных клиента: `.at()`, `operator[]` на const по отсутствующему ключу, `.get<T>()` без проверки типа, `.value()` без проверки, что это объект: в режиме NOEXCEPTION они делают `abort`. Правило записать в `Control/CLAUDE.md`, покрыть тестом на мусор.
- Лимит размера сообщения 1 MiB на транспорте ограничивает вложенность (разбор и деструкция nlohmann итеративные).
- Взаимодействие с правилом «EASTL вместо `std::`»: nlohmann использует `std::string`/`std::map` внутри; на границе (`Json.h`) конвертируем в `eastl::string`. Вендоренный код не правим.

### A3. Интерфейс игры (`Games/IControllableGame.h`)

Простой класс без `BE_CLASS` (генератор берёт только первый базовый класс). `IGame` получает `virtual IControllableGame* AsControllable() { return nullptr; }` (предпочтён `dynamic_cast`: `IGame::Cast<T>()` сравнивает точную мету и интерфейс не найдёт).

```cpp
class IControllableGame {
public:
    virtual ~IControllableGame() = default;
    virtual Json GetState() const = 0;
    virtual Json GetActions() const = 0;
    virtual std::expected<Json, ControlError> PerformAction(const Json& action) = 0;
    virtual std::expected<void, ControlError> Reset(const Json& options) = 0;       // Match3: {"seed":N}
    virtual std::expected<void, ControlError> SetState(const Json&) { /* unsupported */ }  // Chess: {"fen":...}
    virtual uint64_t GetStateVersion() const = 0;   // растёт при каждом принятом изменении
};
```

- Push `state_changed`: frame-end hook в `GameControl` сравнивает `GetStateVersion()` с прошлым, вызывает `ControlService::Publish`. Работает и для кликов человека, в моделях кода push нет.
- Каждая игра владеет адаптером (`ChessGameControl`, `Match3GameControl`); чистые правила (`Game/`) остаются без JSON.
- Chess: состояние `{game, version, fen, turn, status, reason, winner, in_check, halfmove, fullmove, moves[], last_move, board[8]}`; действия `{"type":"move","uci":"e2e4"}` (превращение `e7e8q`, без буквы `promotion_required`).
- Match3: состояние `{game, size, board[], score, seed, legal_swap_count, status}`; действие `{"type":"swap","from":{col,row},"to":{col,row}}`, каждая пара один раз. Модель принадлежит `Match3BoardComponent` (`GetActive()` может быть null) → ответ `no_active_model`. Добавить `WouldSwapMatch(a,b) const` (копия 64 клеток, swap, `MatchFinder::HasAnyMatch`) и `GetLegalSwaps() const`, статус `no_moves`.

### A4. Ручные часы и шаг (`Application::Run`)

```cpp
while (_isRunning) {
    if (control.IsManualClock()) {
        if (!control.TryBeginStep()) { CoreManager::GetInstance().OnIdleCycle({}); continue; }
        CoreManager::GetTime().Advance(control.GetStepSeconds());
    } else {
        CoreManager::GetTime().Tick({});
    }
    RenderEvents::NewFrameEvent::Emit();
    CoreManager::GetInstance().OnGameCycle({});   // + control.Drain() после ProcessEvents
    RenderEvents::RenderPresentEvent::Emit();
    LogEvent::Flush();
    control.EndFrame();
}
```

- Вся машина состояний (`TryBeginStep`, счётчик, `EndFrame`) в `ControlService`, чтобы тестировать в `BagiEngineTests` (`RuntimeModule` туда не линкуется, `Application` напрямую не тестируется).
- `OnIdleCycle`: `ProcessEvents` → `Drain` → `LogEvent::Flush` → ожидание работы на condition variable до 8 мс. Окно остаётся живым, `QuitEvent` завершает цикл.
- «Кадр» = вся итерация включая present. `step {frames, dt}`: `dt` по умолчанию фиксированный шаг 1/60, диапазон [0, 0.25] (как зажим `Advance`), `frames` ограничен константой `kMaxStepFrames`. В режиме реального времени `step` и `screenshot` ждут N реальных кадров тем же кодом. Скриншот в ручном режиме = один кадр с `dt = 0`.
- Аргументы: `main(argc, argv)` → `Application::SetArguments`; для Windows `WinMain` берёт `__argc/__argv`. Разбор и слияние в `BECore/Control/ControlSettings.{h,cpp}` (чистая функция, под gtest). Приоритет: CLI > атрибут `ApplicationConfig.xml` > умолчание (всё выключено). Неизвестные аргументы игнорируются. Ошибки конфигурации завершают запуск (`manualClock` без `control`, `headless` при `mode="Editor"`). Настройки применяются в `Application::Initialize` до `ApplicationFabric::Create`, потому что окну они нужны.

### A5. Headless и скриншот

- `--headless`: рендерер принудительно `SDLRendererBackend`, **видеодрайвер SDL `dummy`** (`SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy")` до `SDL_Init`) и `SDL_HINT_RENDER_DRIVER=software`; окно создаётся без `SDL_WINDOW_VULKAN`. Реального окна нет вообще. Проверено спайком S1 (этап 0): скрытое окно на драйвере `cocoa` + `software` **не работает** ("software not available"), драйвер `offscreen` требует OpenGL/GLES и не создаёт окно. Игры по умолчанию на Vulkan; Vulkan на macOS headless не умеет (у MoltenVK нет `VK_EXT_headless_surface`).
- API: `struct PixelBuffer { uint32_t width, height; eastl::vector<uint8_t> rgba; }` (RGBA8, плотно, верхняя строка первой); в `IRenderer` непурный `virtual void RequestCapture(CaptureCallback)`, по умолчанию отвечает ошибкой (так Vulkan остаётся в строю). По правилу `rendering-module.md` (сначала интерфейс, потом оба бэкенда) отклонение — Vulkan документирует fallback.
- Захват в `SDLRendererBackend::OnRenderPresent` (`:107-111`) перед `SDL_RenderPresent`: `SDL_RenderReadPixels` → конвертация в RGBA32 → callback синхронно. Ответ собирается позже в `done` на `EndFrame`.
- `SDLModule/CoreSDL/SDLScreenshot.{h,cpp}`: опциональный downscale (`maxSize`, по умолчанию 1024), PNG через `IMG_SavePNG_IO` в `SDL_IOFromDynamicMem`, base64 из нового `BECore/Utils/Base64.h`. Результат `{width, height, png_base64}`.
- Отдельный offscreen-рендер не нужен: Game mode рисует прямо в бэкбуфер.

### A6. WebSocket (`src/Modules/RemoteControl/`)

- Файлы: `Sha1`, `WebSocketHandshake`, `WebSocketFrame`, `Socket`, `WebSocketServer` (+ `CLAUDE.md`). Модуль в порядке модулей между TaskSystem и SDL; зависит только от `BECoreModule` и `EventsModule`, JSON не видит: гонит текстовые кадры в `ControlService::Post(text, ReplyFn)`.
- Сервер: `127.0.0.1`, проверка `Origin`, до 4 соединений, сообщение до 1 MiB, на соединение поток чтения и поток записи с outbox (медленный клиент не блокирует игру). `Socket.cpp`: `#ifdef PLATFORM_WINDOWS` Winsock (`ws2_32`), на macOS `SO_NOSIGPIPE`. Порт 0 = эфемерный, выбранный порт в логе и в `status`.
- Разбор клиентских кадров: маскирование обязательно (немаскированные отвергаются), фрагментация, ping → pong, close.

## Этапы

Каждый этап собирается и проходит тесты самостоятельно, коммитится отдельно. Проверка по умолчанию: `cmake --preset macos-debug`, сборка `BagiEngineTests ChessTests Match3Tests`, `ctest --preset macos-debug`. Оценка 20-25 рабочих дней.

### Этап 0. Спайки (0.5-1 день, одноразовые, результат записать в TODO-запись) — СДЕЛАНО на macOS (AppleClang 17, SDL 3.2.0)
- **S1 headless** (отдельная программа на собранных SDL и SDL_image, `SDL_RenderReadPixels` перед `Present`, пиксели сверены, PNG осмотрен):
  - `cocoa` + скрытое окно + `software`: **не работает** (`CreateRenderer failed: software not available`).
  - `cocoa` + скрытое окно + Metal (по умолчанию): работает, пиксели верные, ~20 мс на кадр с `SDL_Delay(8)`.
  - **`dummy` + `software`: работает**, пиксели верные, ~10 мс на кадр; PNG побайтно совпал с Metal (3391 байт, `cmp` без отличий). Выбран как путь headless.
  - `offscreen`: `Could not initialize OpenGL / GLES library`, не годится.
  - Не проверено: Windows (драйвер `dummy` есть на всех ОС, но проверить на этапе 3/5).
- **S2 nlohmann 3.11.3** с `-fno-exceptions -DJSON_NOEXCEPTION`, C++23, AppleClang: собирается (~0.8 с на единицу). Мусорный ввод (`""`, `{`, `not json`, `{"a":}`, `[1,2`, не-UTF-8) даёт `is_discarded()`, без падений. 1 МиБ вложенных `[` разбирается и уничтожается без переполнения стека. **Подтверждено: `.at()` по отсутствующему ключу, `.get<int>()` на строке, `.value()` на массиве и `operator[]` по отсутствующему ключу в const объекте завершают процесс (`abort`, код 134)**, правила A2 основаны на факте. MSVC не проверен. Релиз 3.11.3 не публикует `.sha256` (404); SHA-256 `json.hpp` = `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6` (совпало с известным значением для v3.11.3; в шапке файла версия 3.11.3).
- **S3** `IMG_SavePNG_IO` в `SDL_IOFromDynamicMem` (`closeio=false`, размер через `SDL_GetIOSize`, память через `SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER`, потом `SDL_CloseIO`): работает, заголовок `89 50 4e 47`.
- **S4** цикл «`SDL_PollEvent` + `SDL_Delay(8)`» 30 кадров на скрытом окне (cocoa) и на dummy: не зависает.

### Этап 1. Ядро команд (~3 дня)
- Создать: `src/Modules/nlohmann/`, `BECore/Control/{ControlTypes,Json,ControlService,ControlBuiltins}.{h,cpp}` + `CLAUDE.md`, `BECore/Utils/Base64.{h,cpp}`, `tests/BECore/{ControlServiceTest,JsonTest,Base64Test}.cpp`.
- Изменить: `cmake/BagiEngine.cmake` (вендоренная цель), `CoreManager.h/.cpp` (член, `GetControlService()`, `Drain()` в `OnGameCycle`, `OnIdleCycle`).
- Тесты (фейковый транспорт = прямой `Post` с лямбдой `ReplyFn`): неизвестный метод, битый JSON, отсутствующие параметры, **мусор не вызывает abort** (запрещённый доступ nlohmann), FIFO от нескольких потоков, барьер, обход барьера `quit`, завершение отложенного вызова на `EndFrame`, `shutting_down`, publish; base64 по векторам RFC 4648.
- Docs: `BECore/CLAUDE.md` (строка «1b Commands», список CoreManager), корневой `CLAUDE.md`, `.claude/rules/tests-module.md`.
- Проверка: `ctest -R "Control|Json|Base64"`.

### Этап 2. Интерфейс игры и Match3 (~2-3 дня)
- Создать: `Games/{IControllableGame.h,GameControl.h,GameControl.cpp}`, `games/Match3/src/Match3/Match3GameControl.{h,cpp}`, `tests/BECore/GameControlTest.cpp` (фейковая управляемая игра; `GamesModule` в `BagiEngineTests` уже линкуется), тесты адаптера Match3.
- Изменить: `IGame.h` (`AsControllable`), `GameManager::Initialize` (регистрация), `Match3GameModel` (`WouldSwapMatch`, `GetLegalSwaps`, `_seed`, RNG без `uniform_int_distribution`), `Match3Game`, `games/Match3/CLAUDE.md`, `Games/CLAUDE.md`.
- Тесты: перебор допустимых обменов против грубого `TrySwap` на копии; один seed даёт одну доску; повтор списка действий даёт тот же счёт; нелегальное действие не меняет состояние; `no_moves`.
- Проверка: `ctest -R "Match3|GameControl"`.

### Этап 3. WebSocket и опции запуска (~3-4 дня)
- Создать: `src/Modules/RemoteControl/…` (A6), `BECore/Control/ControlSettings.{h,cpp}`, `tests/BECore/{WebSocketProtocolTest,WebSocketServerTest,ControlSettingsTest}.cpp`, `tools/control/bagi_control_client.py`, `tools/control/tests/smoke.py`.
- Изменить: корневой `CMakeLists.txt` и `cmake/BagiEngine.cmake` (`add_subdirectory`, WHOLE_ARCHIVE-строка в `be_link_engine`), `src/CMakeLists.txt` (`RuntimeModule` линкует `RemoteControlModule`), `tests/CMakeLists.txt`, `main.cpp` и `Application.h/.cpp` (`SetArguments`, старт сервера после `GameManager::Initialize`, остановка первой в `Cleanup`). Новая функция `be_add_game_control_tests(<ExeTarget> [SCRIPT ...])` в `cmake/BagiEngine.cmake`: регистрирует ctest, запускающий `smoke.py --exe $<TARGET_FILE:...>`, `SKIP_RETURN_CODE 77`. Регистрировать её из `games/<Name>/CMakeLists.txt`, а не из `tests/`, иначе движковый CMake назовёт конкретную игру.
- Тесты: SHA-1 «abc» → `a9993e36…`; рукопожатие из RFC 6455 (ключ `dGhlIHNhbXBsZSBub25jZQ==` → `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`); кадры RFC 6455 §5.7 (маскированный «Hello» `81 85 37 fa 21 3d 7f 9f 4d 51 58`, немаскированный `81 05 48 65 6c 6c 6f`); фрагментация, превышение размера, немаскированный клиентский кадр, ping→pong; loopback с сырым клиентским сокетом на эфемерном порту, отказ по `Origin`; приоритет и валидация `ControlSettings`.
- Docs: корневой `CLAUDE.md` (строка порядка модулей, раскладка, список nested `CLAUDE.md`), `.claude/rules/cmake-rules.md`, `RemoteControl/CLAUDE.md`.
- Проверка: `ctest -R "WebSocket|ControlSettings"`; вручную `BagiMatch3 --control` + `websocat ws://127.0.0.1:8765` и `{"id":1,"method":"get_state"}`; **loopback-тесты собрать и прогнать на Windows сейчас, не в конце проекта**.

### Этап 4. Ручные часы, шаг, push состояния (~2 дня)
- Изменить: `Application::Run`, `CoreManager::OnIdleCycle`, `ControlService` (`TryBeginStep`, `GetStepSeconds`, `EndFrame`), builtin `step`, расширить `status`, frame-end hook `state_changed` в `GameControl`, сценарии шага в `smoke.py`.
- Тесты: `step 3` даёт три `TryBeginStep` true, потом false; ответ уходит в конце третьего кадра; `Time` = 3 × dt (сброс `Time` в `TearDown`); команды ждут за шагом; валидация `dt`.
- Docs: таблица порядка кадра и раздел «Manual clock» в `BECore/CLAUDE.md`, TODO.
- Проверка: `ctest -R Control`, потом smoke с `--manual-clock`.

### Этап 5. Headless и скриншот (~3 дня)
- Создать: `CoreSDL/SDLScreenshot.{h,cpp}`, `tests/BECore/SDLCaptureTest.cpp` (окно `SDLMainWindow` + `SDLRendererBackend` на dummy/скрытом драйвере, рисует цветной прямоугольник, сверяет пиксель; пропускается, если драйвер недоступен).
- Изменить: `IRenderer.h` (`PixelBuffer`, `RequestCapture`), `SDLRendererBackend` (захват, hint), `SDLMainWindow::Initialize` (правка флагов), `SDL3ApplicationFabric::Create` (подмена рендерера, регистрация `screenshot`), `.claude/rules/rendering-module.md`, `SDL/CLAUDE.md`.
- Проверка: `ctest -R "SDLCapture|ControlSmoke"`; `BagiMatch3 --headless --control --manual-clock`, скриншот через клиент, PNG посмотреть глазами один раз.

### Этап 6. Правила шахмат, чистый код (~4 дня)
- Создать в `games/Chess/src/Chess/Game/`: `ChessPosition.{h,cpp}` (доска + очередь хода + маска рокировки именованными константами + поле взятия на проходе + счётчики), `Fen.{h,cpp}`, `UciMove.{h,cpp}`.
- Изменить: `ChessTypes.h` (`Move{from,to,promotion}`, `GameResult` как `CORE_ENUM`), `MoveGenerator`, `ChessBoard` (`Clear`, `FindKing`).
- Генерация: оставить `Generate(board, from)` (11 существующих тестов зелёные); добавить `GeneratePseudoLegal(const ChessPosition&)` → `eastl::vector<Move>` с рокировкой, взятием на проходе, превращениями; `ChessPosition::GenerateLegalMoves()` (copy-make, фильтр по `IsSquareAttacked`), `Apply(Move) -> MoveEffects`, `IsInCheck`, `GetResult`. Рокировка: пустые клетки, король не под шахом, не проходит битое поле, права теряются при взятии ладьи.
- FEN: `ParseFen -> std::expected<ChessPosition, FenError>` (4 или 6 полей, по одному королю, нет пешек на 1/8 рядах, по 8 клеток в ряду, права согласованы с расстановкой), `ToFen`. UCI: `ParseUci`, `ToUci`, `FindLegal`; рокировка это ход короля на две клетки.
- Perft-тесты (числа проверены по chessprogramming wiki): старт d1-d4 `20, 400, 8902, 197281` (d5 `4865609`); Kiwipete `r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -` d1-d3 `48, 2039, 97862` (d4 `4085603`); позиция 3 `8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -` d1-d4 `14, 191, 2812, 43238` (d5 `674624`); позиция 4 `r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1` d1-d4 `6, 264, 9467, 422333`; позиция 5 `rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8` d1-d4 `44, 1486, 62379, 2103487`. Глубокие (start d5, Kiwipete d4, позиция 5 d4) как `DISABLED_`.
- Остальные тесты: круги FEN и UCI, отказы разбора, детский мат, пат, недостаточный материал, часы 50 ходов, повторение, рокировка через шах и из-под шаха, срок жизни взятия на проходе, превращение со взятием.
- Docs: переписать «Rules scope (MVP)» в `games/Chess/CLAUDE.md`.
- Проверка: `ctest -R "Perft|Fen|Uci|ChessPosition"`.

### Этап 7. Интеграция с моделью и сценой (~2-3 дня)
- `ChessGameModel`: `_board`/`_turn` → `ChessPosition _position`; история ходов и ключи повторений, `_result`, `std::expected<void, MoveError> TryMove(Move)`, `SetPosition`, `GetFen`, `_version`. `OnSquareClicked` остаётся путём для мыши, использует легальные ходы, превращение в ферзя. После конца партии клики и `TryMove` отвергаются.
- Синхронизация сцены: `PieceTrack` хранит текущую фигуру; `RelocatePieces` превращается в применение `MoveEffects` (перенесённый узел, снятая клетка включая взятие на проходе, вторая ладья при рокировке, новый тип при превращении); `SetPosition` раздаёт узлы по цветам в порядке клеток (по 16 на цвет в сцене, любая валидная позиция помещается). `Reset()` возвращает типы и клетки.
- Спрайт при превращении: `ChessPieceComponent::OnUpdate` сравнивает тип модели с типом спрайта, при расхождении задаёт `SpriteRendererComponent::_spriteId` и зовёт `ReloadTexture()` (метод есть: `SpriteRendererComponent.cpp:41`; `TextureLibrary` знает `WhiteQueen` и остальные; ожидаю, что правок движка не нужно, подтвердить на этапе).
- Тесты: `ChessGameModelTest` (`TryMove`, клики, результат, учёт узлов, `Reset`), чистая логика. Превращение, рокировку и взятие на проходе посмотреть вручную в `BagiChess`, автотеста сцены нет.
- Проверка: `ctest -R ChessGameModel`.

### Этап 8. Адаптер Chess (~1-2 дня)
- `games/Chess/src/Chess/ChessGameControl.{h,cpp}`; `ChessGame::AsControllable()`.
- Тесты: состояние в JSON, список действий против `GenerateLegalMoves`, нелегальный ход, `promotion_required`, `game_over`; сценарий для smoke (4 хода + мат в один ход через `set_state`) через `be_add_game_control_tests` в `games/Chess/CMakeLists.txt`.
- Docs: `games/Chess/CLAUDE.md`. Проверка: `ctest -R "ChessGameControl|ControlSmoke"`.

### Этап 9. Мост к Stockfish и MCP (~3 дня)
- В `tools/control/`: `stockfish_bridge.py` (UCI через subprocess, без `python-chess`; флаги `--side white|black|both`, `--movetime`, `--stockfish`/`STOCKFISH`/PATH, `--cross-check`), `bagi_mcp.py` (официальный `mcp` Python SDK, клиент `websockets`), `requirements.txt`, `tests/test_dispatch.py`, `tests/test_stockfish.py`, `README.md`.
- Мост: ждёт состояние, на своём ходу шлёт `position fen …` и `go movetime N`, читает `bestmove`, зовёт `perform_action`. `--cross-check`: `go perft 1` сверяется с `list_actions` в каждой позиции (постоянный монитор расхождения правил).
- MCP-инструменты: `launch_game` (запуск exe с `--control --headless --manual-clock`, свободный порт, ожидание подключения), `connect`, `get_state`, `list_actions`, `perform_action`, `step`, `screenshot` (картинкой), `reset`, `quit`.
- Приёмка: `test_stockfish.py` играет Stockfish на малом уровне против выбора случайного легального хода (или Stockfish за обе стороны) с лимитом полуходов; каждое предложенное движком действие принимает Stockfish, каждый `bestmove` есть в `list_actions`. Код 77, если Stockfish не установлен.
- Проверка: `pytest tools/control/tests`; полная партия `python tools/control/stockfish_bridge.py --side both`.

### Этап 10. Закрытие
- TODO.md: удалить запись или оставить остаток (Vulkan readback, режим без окна, вопрос часов у `ScheduleDelayed`); обновить пункт 2 роадмапа.
- Свод документации: корневой `CLAUDE.md`, `.claude/rules/*`, nested `CLAUDE.md`. Число тестов выросло, обновить «177» в записи про CI.
- `clang-format` на изменённых файлах (скилл `clang-format`), затем `strip-pch-includes` (скилл) по изменённым include. Для большого этапа сначала dry-run на подпапке.

## Риски и что проверить первым

1. **Headless** (S1 закрыт на macOS): работает путь `dummy` + `software`. Осталось проверить на Windows и убедиться, что `SDLMainWindow::Initialize` не вызывает `SDL_Init(VIDEO)` раньше, чем выставлен hint (на этапе 5).
2. **Windows:** сокеты и `WinMain` с этой машины не проверить. Собрать и прогнать loopback-тесты в конце этапа 3.
3. **nlohmann без исключений:** любой запрещённый доступ на данных клиента = abort всей игры. Защита: правила A2 + тест на мусор + обзор кода адаптеров.
4. **Скорость шага на Vulkan:** present по vsync, `step 600` займёт ~10 с. `--headless` это обходит (software SDL). Для окна с ручными часами позже добавить «пропуск present на промежуточных кадрах».
5. **Детерминизм:** `TaskScheduler::ScheduleDelayed` принимает реальное `Duration` (`TaskScheduler.cpp:26`); не проверял, идёт ли оно по настенным часам. Потребителей пока нет, заметка для работы над анимацией.
6. **Локальные процессы:** любой процесс на машине сможет управлять игрой, токена нет. Принято, выключено по умолчанию.
7. **Размер скриншотов:** base64 PNG Full-HD огромный для модели; `maxSize` по умолчанию 1024.
8. **Решение по бэкендам рендера** (TODO «Renderer backends») ещё не принято: если SDL-бэкенд отбросят, реализация `RequestCapture` переезжает; сам API не зависит от бэкенда.
9. **Perft в Debug:** глубокие счёты `DISABLED_`.

## Расхождения TODO-записи с кодом

- `SwitchToMainThread()` как способ передачи команд не годится: очередь не FIFO.
- «Offscreen rendering for screenshots» не нужен: Game mode рисует в бэкбуфер.
- «Readback in both backends» урезан: Vulkan отложен (риск 8).
- «Implemented by Chess and Match3»: модель Match3 принадлежит `Match3BoardComponent`, адаптеру нужен `GetActive()`.
- Smoke-тест нельзя зарегистрировать из `tests/` без имени игры (см. этап 3).

## Прогресс

- [x] Этап 0 спайки S1-S4 (macOS; Windows и MSVC не проверены)
- [x] Этап 1 ядро команд (50 новых тестов, всего 227 зелёных; clang-format и strip-pch-includes применены; не закоммичено)
- [x] Этап 2 интерфейс игры + Match3 (+45 тестов, всего 272 зелёных; отличия от плана: `GetState`/`GetActions` возвращают `std::expected`; ответ методов `{game, version, state|actions|outcome}`, `game` и `version` добавляет `GameControl`, а не адаптер; Match3 получает модель через provider, чтобы адаптер тестировался без сцены; `perform_action` принимает действие как params без обёртки)
- [ ] Этап 3 WebSocket + опции запуска
- [ ] Этап 4 ручные часы, шаг, push
- [ ] Этап 5 headless + скриншот
- [x] Этап 6 правила шахмат (сделано в задаче «мат не ставится», отличия от плана: позиция на битбордах, а не `ChessPosition` с copy-make: `Position` (FEN, `DoMove`/`UndoMove`, `IsLegal`), `Attacks` (магические битборды, Zobrist), `Move` в 16 битах, `MoveGenerator::GenerateLegal` и `ParseUci`, `EvaluateStatus` вместо `GetResult`; perft сверен с chessprogramming wiki, глубокие (старт d5, Kiwipete, позиции 5 и 6 d4) идут в обычных тестах без `DISABLED_`, весь набор около секунды в Debug; автоничьи там пятикратное повторение и 75 ходов (правила FIDE без заявления), а не троекратное и 50 из «Решений»: пороги это две именованные константы в `GameStatus.cpp`)
- [ ] Этап 7 модель и сцена Chess (частично сделано там же: модель на `Position`, `TryMove(Move) -> bool`, статус партии, после конца клики отвергаются, ручки фигур идут за взятием, рокировкой, взятием на проходе и превращением, спрайт меняется, на слое 1 плашка результата; осталось `SetPosition`, `GetFen`, `_version`, `std::expected<void, MoveError>` вместо `bool`, история ходов для адаптера)
- [ ] Этап 8 адаптер Chess
- [ ] Этап 9 Stockfish + MCP
- [ ] Этап 10 закрытие, форматирование, includes
