# Этап 3 внешнего управления: WebSocket-сервер и опции запуска

## Context

Запись TODO.md «No external control» (роадмап, этап 2) и план `.claude/plans/buzzing-tinkering-cat.md`. Этапы 0-2 готовы: ядро команд (`BECore/Control/ControlService`), интерфейс игры `IControllableGame` и адаптер Match3. Команды пока доходят до очереди только из кода внутри процесса: **транспорта нет**. Владелец выбрал в работу следующий этап по плану, «WebSocket и опции запуска».

Цель этапа: запустить `BagiMatch3 --control`, подключиться по `ws://127.0.0.1:8765` и получить ответ на `{"id":1,"method":"get_state"}`. Шаг кадров, headless и скриншот (этапы 4-5), правила шахмат (6-8) и MCP/Stockfish (9) сюда не входят.

Решения владельца, уже принятые в основном плане: свой RFC 6455 без внешней библиотеки, сокеты POSIX/Winsock, localhost, выключено по умолчанию, JSON только через `BECore/Control/Json.h`. Эта часть плана только уточняет их по коду, прочитанному сейчас.

## Что уже есть и на что опирается план (прочитано в коде)

- `ControlService::Post(text, ReplyFn)` потокобезопасен, отвечает `request_too_large` на текст больше `kMaxRequestBytes` (1 MiB); после `Shutdown()` отвечает сразу, на вызывающем потоке. `ReplyFn` вызывается на главном потоке. `SetEventSink(sink)` + `Publish` готовы под push; `kProtocolVersion = 1`.
- `Base64Encode(span<const uint8_t>)` есть в `BECore/Utils/Base64.h` (для `Sec-WebSocket-Accept`); декодер не нужен.
- `Application::Run() const` и `static Cleanup()`; `main.cpp` не принимает `argc/argv` (`RunApplication()` без параметров). `ApplicationConfig` читается через `CoreManager::GetConfigManager().GetConfig("ApplicationConfig"_intern)`, атрибуты через `XmlNode::ParseAttribute`.
- Модуль добавляется так: `be_add_module(<Name> LINK ...)` (раскладка `<dir>/src/<Name>/`), `add_subdirectory` в `cmake/BagiEngine.cmake`. `RuntimeModule` линкует модули через `src/CMakeLists.txt`. Регистраций в статических инициализаторах у нового модуля нет, поэтому `WHOLE_ARCHIVE` не нужен.
- `BE_CLASS` в модуле не будет, значит записи в `CI/meta_generator/reflection_targets.json` не нужно.
- Тесты движка: `tests/BECore/*.cpp` globbing в `tests/CMakeLists.txt`; `be_add_game_tests(... LINK ...)` принимает доп. библиотеки. Чистые тесты не вызывают `EngineEnvironment::EnsureCoreServices()`.
- `.claude/rules/tests-module.md`: gtest-макросы, без `sleep` (опрос условия с таймаутом), JSON клиента не читать throwing-методами даже в тесте.

## Решения этапа (по умолчанию, скажите, если не так)

| Вопрос | Решение |
|--------|---------|
| Поля `ControlSettings` | Только `enabled` и `port` (флаги `--control`, `--control-port=N`; атрибуты `control`, `controlPort` в `ApplicationConfig.xml`). `manualClock` и `headless` добавят этапы 4 и 5, когда у них появится потребитель |
| Приоритет | CLI > атрибут > умолчание (выключено, порт 8765). Порт 0 = эфемерный |
| Ошибки настроек | Неразборчивое значение порта или bool, `--control-port` без включённого API: запуск завершается с сообщением в лог (`Initialize()` возвращает `false`) |
| Порт занят | Запуск завершается с ошибкой: раз API запросили явно, молча жить без него нельзя |
| Безопасность | Bind только `127.0.0.1`; заголовок `Host` должен быть `127.0.0.1`, `localhost` или `[::1]` (с портом или без); `Origin`, если он есть, тоже loopback (защита от страницы в браузере и DNS rebinding), без `Origin` пускаем (обычные клиенты); не более 4 соединений; токена нет |
| Текстовые кадры | UTF-8 на уровне WebSocket не проверяем (отступление от RFC §5.6, записать в `CLAUDE.md` модуля): мусор получит `parse_error` от `ParseJson`. Бинарные кадры закрывают соединение с кодом 1003 |
| Окно в smoke-тесте | До этапа 5 (headless) smoke открывает реальное окно игры. ctest-запись регистрируется сразу, как в основном плане, с таймаутом и `SKIP_RETURN_CODE 77` |
| Python-клиент | Только stdlib (рукопожатие и маскированные кадры вручную): никаких зависимостей для smoke. `websockets` понадобится MCP-мосту на этапе 9 |

## Новые файлы

### Движок

`src/Modules/RemoteControl/` (пространство имён `BECore`, как у соседних модулей):

| Файл | Назначение |
|------|------------|
| `CMakeLists.txt` | `be_add_module(RemoteControl LINK BECoreModule EventsModule)`; на `WIN32` ещё `ws2_32` |
| `src/RemoteControl/Sha1.{h,cpp}` | `Sha1(span<const uint8_t>) -> std::array<uint8_t, 20>` |
| `src/RemoteControl/WebSocketHandshake.{h,cpp}` | `ComputeAcceptKey(key)`, `ParseHandshakeRequest(headerText) -> std::expected<key, HandshakeError{httpStatus, message}>` (GET, HTTP/1.1, `Upgrade`, список токенов `Connection`, версия 13, форма ключа 24 символа base64, `Host`, `Origin`; имена заголовков без учёта регистра), `BuildAcceptResponse(key)`, `BuildRejectResponse(status, message)` (426 с `Sec-WebSocket-Version: 13`, 403, 400, 503) |
| `src/RemoteControl/WebSocketFrame.{h,cpp}` | `EncodeFrame(opcode, payload, optional mask)` (маска нужна клиентам и тестам), `WebSocketFrameParser` (инкрементальный: `Feed(bytes)`, `Pop()` отдаёт `Message{kind, payload, closeCode}` или `ParseError{closeCode, reason}`). Правила: клиентские кадры обязаны быть маскированы, RSV = 0, управляющие кадры до 125 байт и без фрагментации, фрагментация данных, ping внутри фрагментов допустим, размер сообщения проверяется по длине в заголовке до накопления (лимит `ControlService::kMaxRequestBytes`, код 1009), нарушения протокола 1002. Опкоды константами (`kOpcodeText` и т.д.), не `enum class` |
| `src/RemoteControl/Socket.{h,cpp}` | Движимый RAII-класс: `Listen(port)` (только `127.0.0.1`), `Accept()`, `Connect(port)` (для тестов), `Send` (цикл до конца), `Receive`, `SetReceiveTimeout`, `ShutdownBoth`, `Close`, `GetLocalPort`. `#ifdef PLATFORM_WINDOWS` Winsock (`WSAStartup` один раз, `SO_EXCLUSIVEADDRUSE`), иначе POSIX (`SO_REUSEADDR`; `SO_NOSIGPIPE` на macOS, `MSG_NOSIGNAL` на Linux) |
| `src/RemoteControl/WebSocketServer.{h,cpp}` | `WebSocketServer(ControlService&)`, `Start(port) -> std::expected<uint16_t, ControlError>` (возвращает выбранный порт), `Stop()`, `GetPort()`. Поток accept, на соединение поток чтения и поток записи с очередью (outbox, `std::condition_variable`, лимит байт: медленный клиент отключается, игру не блокирует). Рукопожатие читается до `\r\n\r\n` не больше 8 KiB с таймаутом 5 с. Текстовый кадр идёт в `service.Post(text, reply)`, где `reply` держит `std::shared_ptr<Connection>` и кладёт текстовый кадр в outbox (безопасно и после закрытия соединения). Ping отвечает pong с тем же телом, close зеркалится. `SetEventSink` рассылает push всем открытым соединениям. `Stop()`: снять sink, закрыть слушающий сокет, `ShutdownBoth` у соединений, `join` |
| `CLAUDE.md` | Протокол кадров, потоки, лимиты, безопасность, отступление от RFC по UTF-8, как проверить вручную |

`src/Modules/BECore/Control/ControlSettings.{h,cpp}`:

```cpp
struct ControlSettings { bool enabled = false; uint16_t port = kDefaultControlPort; };
// args: аргументы командной строки без argv[0]; appConfig: корень ApplicationConfig (может быть пустым XmlNode)
std::expected<ControlSettings, ControlError> ResolveControlSettings(eastl::span<const eastl::string> args, const XmlNode& appConfig);
```

Чистая функция без обращений к `CoreManager`, поэтому тестируется без движка. `kDefaultControlPort = 8765` именованной константой.

### Python и ctest

- `tools/control/bagi_control_client.py`: минимальный клиент (рукопожатие, маскированные кадры, `call(method, **params)`, очередь push-событий, `ControlError`), stdlib only.
- `tools/control/tests/smoke.py`: `--exe PATH [--port N]`: берёт свободный порт, запускает игру с `--control --control-port=N`, ждёт соединения (опрос с таймаутом), сценарий Match3: `ping`, `list_games`, `reset {"seed": 1}`, `get_state`, `list_actions`, `perform_action` первым действием, `get_state` (версия выросла), мусорный текст даёт `parse_error` и соединение живо, неизвестный метод, `quit`; код выхода процесса 0. Код 77, если файла игры нет.
- `tools/control/tests/test_frames.py` (pytest): кадры клиента на векторах RFC 6455 §5.7.
- `be_add_game_control_tests(<ExeTarget> [SCRIPT ...])` в `cmake/BagiEngine.cmake` (внутри блока `ENGINE_BUILD_TESTS`): `add_test(NAME <Exe>ControlSmoke COMMAND ${Python3_EXECUTABLE} <engine>/tools/control/tests/smoke.py --exe $<TARGET_FILE:<Exe>>)`, `SKIP_RETURN_CODE 77`, `TIMEOUT 60`. Вызывается из `games/Match3/CMakeLists.txt`, не из `tests/`: движковый CMake не называет игру.

### Тесты движка (`tests/BECore/`, цель `BagiEngineTests`)

- `Sha1Test.cpp`: векторы FIPS 180 (`abc` → `a9993e36...`, пустая строка → `da39a3ee...`, 56 байт `abcdbcdecdef...` → `84983e44...`, миллион `a` → `34aa973c...`), длины на границах блока 55/56/63/64/65 (ожидаемые значения сверить `hashlib` при написании).
- `WebSocketHandshakeTest.cpp`: ключ из RFC (`dGhlIHNhbXBsZSBub25jZQ==` → `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`), корректный запрос, регистр заголовков, `Connection: keep-alive, Upgrade`, нет `Upgrade`/ключа, версия не 13 (426), кривой ключ, чужие `Host` и `Origin` (403), loopback `Origin` с портом и без, слишком длинный заголовок.
- `WebSocketFrameTest.cpp`: маскированный «Hello» `81 85 37 fa 21 3d 7f 9f 4d 51 58`, серверный `81 05 48 65 6c 6c 6f`, длины 125/126/65535/65536, подача по одному байту, фрагментация `Hel`+`lo` и ping между фрагментами, немаскированный клиентский кадр (1002), RSV (1002), управляющий кадр больше 125 или фрагментированный (1002), продолжение без начала (1002), новое сообщение посреди фрагментов (1002), бинарный кадр (1003), размер сверх лимита (1009).
- `WebSocketServerTest.cpp` (loopback, порт 0, свой `ControlService`; тестовый поток сам вызывает `Drain()` в цикле ожидания с таймаутом): рукопожатие 101, `ping` через сокет, конвейер из нескольких запросов сохраняет порядок, мусорный текст даёт `parse_error` и соединение живо, ping-кадр даёт pong, сообщение больше 1 MiB закрывает с 1009, `Origin: http://evil.example` даёт 403 и ничего не попадает в очередь, пятое соединение получает 503, `Publish` доходит до клиента, `Stop()` с открытыми соединениями завершается (join), второй сервер на занятом порту: `Start` возвращает ошибку, после `service.Shutdown()` ответ `shutting_down`.
- `ControlSettingsTest.cpp`: умолчание выключено, атрибуты, CLI поверх атрибутов (`--control` при `control="false"`), порт 0, неверный порт (`abc`, `70000`, `-1`, пустой), неверный bool в атрибуте, `--control-port` без включённого API, неизвестные аргументы игнорируются, пустой `XmlNode`.

## Изменения существующих файлов

| Файл | Что |
|------|-----|
| `src/main.cpp` | `RunApplication(argc, argv)`; `main` передаёт свои, `WinMain` берёт `__argc` / `__argv`; вызов `Application::SetArguments` до `Initialize()` |
| `src/Application/Application.{h,cpp}` | `SetArguments(int, const char* const*)` (в `eastl::vector<eastl::string>`, без `argv[0]`); после `GameManager::Initialize()`: `ResolveControlSettings`, при `enabled` создать `WebSocketServer` и `Start`; ошибка настроек или `Start` = `false` из `Initialize()`. `Run()` и `Cleanup()` перестают быть `const` / `static`; в `Cleanup()` сервер останавливается первым (требование `Control/CLAUDE.md`: транспорт останавливается до `ControlService::Shutdown`) |
| `src/CMakeLists.txt` | `RuntimeModule` линкует `RemoteControlModule` |
| `cmake/BagiEngine.cmake` | `add_subdirectory(.../src/Modules/RemoteControl)` после TaskSystem, `be_add_game_control_tests`, шапка с перечнем публичных функций |
| `tests/CMakeLists.txt` | `LINK RemoteControlModule` |
| `games/Match3/CMakeLists.txt` | `be_add_game_control_tests(BagiMatch3)` |
| `config/ApplicationConfig.xml` | без изменений (по умолчанию выключено); атрибуты описаны в документации |

## Документация

- `src/Modules/RemoteControl/CLAUDE.md` (новый).
- `src/Modules/BECore/Control/CLAUDE.md`: строка `ControlSettings` в таблице, раздел про запуск и флаги.
- Корневой `CLAUDE.md`: строка порядка модулей (`BECore → Events → TaskSystem → RemoteControl → SDL / Vulkan → ...`), раскладка `src/Modules/`, список nested `CLAUDE.md`, строка про `tools/control`.
- `.claude/rules/cmake-rules.md`: порядок модулей и таблица публичных функций; `.claude/rules/tests-module.md`: упоминание новых тестов и loopback.
- `TODO.md`: «Next:» в записи «No external control» (этап 3 готов), без смены стадии записи.
- `.claude/plans/buzzing-tinkering-cat.md`: отметить этап 3 в «Прогрессе», записать отличия от плана.

## Порядок работы

1. `ControlSettings` и `ControlSettingsTest`.
2. `Sha1`, `WebSocketHandshake`, `WebSocketFrame` с тестами (чистый код, без сокетов).
3. `Socket`, `WebSocketServer`, CMake модуля, loopback-тесты.
4. Подключение в `Application` и `main.cpp`.
5. Python-клиент, `smoke.py`, `be_add_game_control_tests`; ручной запуск.
6. Документация, TODO и план, затем `clang-format` (скилл) и `strip-pch-includes` (скилл) по изменённым и новым файлам; `todo-sync check`.

## Проверка

```bash
cmake --preset macos-debug                                        # новый модуль и тесты подхватываются glob-ом
cmake --build --preset macos-debug --target BagiEngineTests ChessTests Match3Tests
ctest --preset macos-debug -R "Sha1|WebSocket|ControlSettings"    # новые тесты
ctest --preset macos-debug                                        # все тесты, число не должно упасть (сейчас 272)
cmake --build --preset macos-debug                                # BagiChess и BagiMatch3 собираются с модулем
build/ClangmacOS/BagiMatch3 --control --control-port=8765         # вручную, затем клиент:
.venv/bin/python -c "from tools.control.bagi_control_client import BagiControlClient as C; c=C('127.0.0.1',8765); print(c.call('get_state'))"
.venv/bin/python -m pytest tools/control/tests                    # кадры клиента
```

Проверить руками: без `--control` порт не слушается (`lsof -i :8765` пусто); `--control-port=99999` и занятый порт завершают запуск с понятной строкой в логе; закрытие окна при подключённом клиенте не зависает.

## Риски

1. **Windows не проверить с этой машины:** Winsock, `WinMain` с `__argc/__argv`, `ws2_32`. Loopback-тесты собрать и прогнать на Windows сразу после этапа, не в конце проекта (риск 2 основного плана). До этого этап помечается «macOS».
2. **Остановка с живыми соединениями:** потоки чтения заблокированы в `recv`. Решает `ShutdownBoth` перед `join`; тест на `Stop()` с открытыми соединениями обязателен.
3. **Гонка при завершении:** `reply` вызывается на главном потоке после того, как соединение закрыто. Защита: `shared_ptr<Connection>` с флагом закрытия под мьютексом; ответы в закрытое соединение отбрасываются.
4. **Окно в ctest:** smoke открывает окно игры до этапа 5. Если мешает, запись можно отключить из набора по умолчанию до этапа 5 (одна строка в `games/Match3/CMakeLists.txt`).
5. **Порт в тестах:** loopback-тесты и smoke берут эфемерный порт (0 или найденный свободный), чтобы параллельные `ctest -j` не конфликтовали.
6. **Любой локальный процесс управляет игрой:** токена нет, API выключено по умолчанию (принято в основном плане).
