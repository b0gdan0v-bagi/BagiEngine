# TaskSystem: нормальное ожидание в TaskHandle + связанные CancellationToken

## Context

В `src/Modules/TaskSystem` остались два TODO, которые мешают по-настоящему:

1. `TaskHandle::Wait()` ([TaskHandle.h:113](../../src/Modules/TaskSystem/src/TaskSystem/TaskHandle.h)) крутится в цикле
   `yield()`. Его вызывает `TaskGroup::WaitAll()` на главном потоке при старте ([ConfigManager.cpp:49](../../src/Modules/BECore/Config/ConfigManager.cpp)),
   так что пока грузятся конфиги, главный поток сжигает ядро. Кроме CPU там есть настоящая гонка: `IsDone()` читает
   `coroutine_handle::done()`, то есть поле во фрейме корутины, пока воркер его пишет.
2. `CancellationToken::CreateLinked()` ([CancellationToken.h:67](../../src/Modules/TaskSystem/src/TaskSystem/CancellationToken.h))
   возвращает независимый токен, хотя в комментарии обещано "отмена родителя отменяет дочерний".

Цель: заменить спин на блокирующее ожидание без гонок и реализовать связывание токенов. Поведение API не меняется.

## Что делаем и как

### 1. Сигнал завершения в promise ([Task.h](../../src/Modules/TaskSystem/src/TaskSystem/Task.h))

Новый файл не нужен (иначе придётся делать re-configure из-за `GLOB_RECURSE`), всё живёт в `Detail` рядом с `FinalAwaiter`.

- `Detail::CompletionEvent`: `std::mutex` + `std::condition_variable` + `std::atomic<bool> _signaled`.
  - `Signal()`: под мьютексом ставит флаг и делает `notify_all` (уведомление под замком, чтобы ждущий не успел уничтожить
    событие раньше, чем сигналящий закончит с ним работать).
  - `Wake()`: берёт мьютекс и делает `notify_all`. Нужен для `Cancel()`, чтобы ждущий перепроверил предикат.
  - `IsSignaled()`: `load(acquire)`.
  - `WaitUntil(extraPredicate)`: `cv.wait(lock, [] { return _signaled || extraPredicate(); })`.
- `TaskPromiseBase` получает `CompletionEvent* _completion = nullptr`.
- `FinalAwaiter::await_suspend`: сначала забрать `_continuation` в локальную переменную, потом `_completion->Signal()`,
  потом вернуть сохранённое. После `Signal()` ждущий поток вправе уничтожить фрейм, поэтому из promise после этого
  ничего читать нельзя. Заодно закрывается существующее окно, когда ещё не вернувшийся `await_suspend` читал уже
  освобождённый фрейм.
- Добавить `#include <condition_variable>` (в PCH его нет, `<mutex>` и `<atomic>` там есть).

### 2. `TaskHandle` ([TaskHandle.h](../../src/Modules/TaskSystem/src/TaskSystem/TaskHandle.h))

- Член `CompletionEvent _completion;` объявить ПЕРЕД `_task`, тогда `_task` (и фрейм) разрушится раньше, чем событие.
  В конструкторе `TaskHandle(Task<T>&&)` зарегистрировать `_task.GetHandle().promise()._completion = &_completion`.
  Объект живёт в куче через `MakeIntrusive` и не перемещается (пользовательский деструктор отключает move, копирование
  запрещено атомиками), так что адрес стабилен.
- `IsDone()`: `!_task.IsValid() || _completion.IsSignaled()`. Это убирает гонку на чтении фрейма. Для невалидной задачи
  результат тот же, что и сейчас.
- `Wait()`: `_completion.WaitUntil([this] { return IsCancelled(); })`, дальше прежняя подстановка статуса
  `Running → Completed`. Ранний выход при отмене сохраняем: отменённая до запуска задача никогда не стартует
  (`Start()` проверяет `Pending`), и без этого `Wait()` блокировался бы навсегда.
- `Cancel()`: после выставления `_cancelled` вызвать `_completion.Wake()`.
- Комментарий к `Wait()`: нельзя звать на главном потоке для задачи, запланированной на `ThreadType::MainThread`
  (очередь главного потока разбирается в `TaskManager::Update`, и ожидание заблокирует сам себя). Это правило уже есть в
  `tasksystem-module.md`, поведение такое же, как у старого спина.

### 3. Связанные токены ([CancellationToken.h](../../src/Modules/TaskSystem/src/TaskSystem/CancellationToken.h))

Опрос вместо подписок: без колбэков, без блокировок и без проблем со временем жизни.

- Приватное `struct State { std::atomic<bool> cancelled{false}; eastl::shared_ptr<State> parent; };`
  вместо `shared_ptr<atomic<bool>>`.
- `Create()` создаёт `State`. `CreateLinked()` создаёт `State` с `parent = _state` (у невалидного родителя `parent`
  остаётся пустым, токен всё равно валиден, как сейчас).
- `IsCancelled()`: идёт вверх по цепочке `parent` и возвращает `true` при первом взведённом флаге.
- `Cancel()` взводит только свой флаг: отмена ребёнка родителя не трогает. `CheckCancellation()` и `TaskGroup` менять не нужно.

## Файлы

- Меняю: `TaskSystem/Task.h`, `TaskSystem/TaskHandle.h`, `TaskSystem/CancellationToken.h`.
- Обновляю доку: `src/Modules/TaskSystem/CLAUDE.md` (одна строка про `Wait()` и `CreateLinked()`; файл переехал из `.claude/rules/tasksystem-module.md` при ревизии правил, старого пути больше нет и пересоздавать его не нужно).
- НЕ трогаю: `BECore/Tests/**`, `config/TestsConfig.xml`, корневой `CmakeLists.txt`, `.gitmodules`, `external/`
  (это территория агента, который выносит тесты в gtest, и у него в рабочем дереве незакоммиченные правки).

## Верификация

Тесты в этой сессии не пишу (вы сказали, что тестами занят другой агент). Проверяю тем, что есть:

1. Перед сборкой проверяю `pgrep ninja|cmake`: если другой агент собирает в `build/macos-debug`, жду, чтобы не перетирать
   его сборку. Если сборка падает из-за его недоделанных правок (googletest в CMake), останавливаюсь и сообщаю, а не обхожу.
2. Скилл `build`, пресет `macos-debug`, только сборка (новых файлов нет, configure не нужен). Жду exit code 0.
3. Скилл `run`: движок стартует, `ConfigManager` грузит конфиги через `TaskGroup::WaitAll()` (это и есть новый путь
   `Wait()`), в логе `[ConfigManager] Loaded N configs` и `TaskSystemTest ... All tests PASSED`, `Results: 10 passed, 0 failed`,
   без зависания на старте.
4. Скилл `clang-format --changed`, затем `strip-pch-includes` (dry-run на `src/Modules/TaskSystem`). Повторная сборка.
5. `git status`: изменены только перечисленные файлы. Коммит только по вашей просьбе.

Чего я не смогу проверить: гонки и нагрузку (нет TSan в пресетах, нет стресс-теста). Поэтому в план для вашего
gtest-агента кладу готовые кейсы, которые стоит добавить после миграции:
- `Wait()` на задаче с `Delay(50ms)` возвращается после неё, а не сразу и не по спину;
- `Wait()` на отменённой до запуска задаче возвращается, `GetResult()` даёт `Cancelled`;
- `Cancel()` из другого потока будит заблокированный `Wait()`;
- `CreateLinked()`: отмена родителя видна в ребёнке (в том числе через два уровня), отмена ребёнка родителя не трогает.

## Вне scope (и почему)

- **`EventAwaiterWithTimeout` (таймаут):** `EventAwaiter` подписывается на собственный локальный `entt::dispatcher`, а не на
  dispatcher из `EventBase<T>` (у него свой на каждый тип события), поэтому реальные события до него не доходят.
  `WaitForEvent` нигде не используется. Таймаут поверх неработающей основы бессмысленен, нужен отдельный разговор о том,
  как подключить awaiter к `EventBase`.
- **Приоритеты в `TaskScheduler::DispatchToThreadPool`:** `ThreadPool::Submit` раскладывает задачи по FIFO-очередям
  round-robin. Приоритеты требуют приоритетной очереди в `TaskQueue` и решения про work stealing. Это отдельная задача.
- **Сырой `handlePtr` в лямбде `TaskManager::Run`:** если выбросить `IntrusivePtr` до запуска, лямбда обратится к
  освобождённому handle. Похоже на реальную проблему, но не входит в "спин-цикл". Предлагаю отдельным шагом.
- **`MarkCompleted()` никто не вызывает:** `GetStatus()` у завершённой задачи остаётся `Running`, пока кто-то не вызовет `Wait()`.
  Тоже отдельный шаг.

## Статус: реализовано, не закоммичено

Пока шла работа, другой агент закоммитил вынос тестов в GoogleTest (`tests/`, цель `BagiEngineTests`) и сам
`BinarySerializer`/`BinaryDeserializer` (`07624b8b`). Поэтому пункты "Верификация" 1-3 выполнены в таком виде:

- Сборка `macos-debug`, цели `MyGame` и `BagiEngineTests`: exit code 0, 0 предупреждений. Configure перезапускал, потому что
  дерево ушло вперёд (новые файлы в `Reflection/` и `tests/`).
- `BagiEngineTests`: 110 из 110 тестов зелёные, включая 9 из `TaskSystemTest`. Строки `Results: 10 passed` больше нет,
  тесты вынесены из движка.
- Запуск `MyGame`: `ConfigManager` загрузил 9 конфигов через `TaskGroup::WaitAll()` (новый путь `Wait()`),
  `EngineSmokeTest` прошёл, зависаний нет. Процесс остановлен вручную.
- `clang-format --changed` применён (потребовался `PATH` с `/opt/homebrew/opt/llvm/bin`), `strip-pch-includes` в dry-run
  ничего не нашёл, `<condition_variable>` в PCH нет и остаётся явным.
- Изменены только `Task.h`, `TaskHandle.h`, `CancellationToken.h` и `src/Modules/TaskSystem/CLAUDE.md`.

Тесты добавлены в `tests/BECore/TaskSystemTest.cpp` (11 новых кейсов, всего 121 в наборе, `ctest` зелёный):
блокирующий `Wait()` и пробуждение нескольких ждущих, `Wait()` после завершения и на пустом handle, отмена до запуска,
отмена из другого потока, `CreateLinked()` (два уровня, ребёнок не отменяет родителя, ребёнок переживает родителя,
невалидный родитель, связь с токеном `TaskGroup`). Ожидание идёт в отдельном потоке (`AsyncWaiter`), поэтому
сломанный `Wait()` даёт упавший тест, а не зависший. Проверено мутацией: без `Wake()` в `Cancel()` и со старым
`CancellationToken.h` падают 4 теста, 300 повторов на исправленном коде без сбоев.

Не покрыто: сам факт отсутствия спина (поведенчески спин неотличим от блокировки) и гонки под TSan.
