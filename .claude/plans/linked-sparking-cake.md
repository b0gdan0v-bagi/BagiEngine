# Scene-scoped events: компоненты неактивных сцен не должны реагировать на события

## Статус: выполнено

Все шаги сделаны, `ctest --preset macos-debug` даёт 152 из 152 (было 145, плюс 7 в `SceneEventsTest`). Отличия от плана:

- В тесте `LoadScenes` не нужен `BeginObject("SceneManager")`: `XmlDeserializer::LoadFromString` уже ставит корневой элемент текущим узлом, как `MakeFromConfig`. Первый прогон упал из-за этого, а не из-за бага; настоящее воспроизведение получено после правки теста.
- Кроме перечисленных в плане документов поправлен список тестовых данных в `.claude/rules/tests-module.md`; `Games/CLAUDE.md` пришлось поправить (старая формулировка противоречила новому поведению), `games/Chess` и `games/Match3` менять не пришлось.
- `format_sources.py` заодно свернул два уже существовавших выражения в `ChangeToRandomColorDebugComponent.cpp` и `ClickableComponent.cpp`; `strip-pch-includes` дублей не нашёл.
- Игры руками не запускались.

## Context

Первая задача из Roadmap (`TODO.md`, этап 1 «гигиена»), запись «Scene events are global». Проблема найдена чтением кода, этот план начинается с её воспроизведения тестом.

Как это работает сейчас:

- `SceneUpdateEvent`, `SceneDrawEvent` (`Events/SceneEvents.h`), `SDLEvents::MouseButtonDownEvent` и `InputEvents::ActionEvent` — это `EventBase<T>` с одним статическим dispatcher на тип, к сцене они не привязаны.
- Все сцены из `SceneConfig` десериализуются сразу, `Scene::Initialize()` → `SceneNode::WireToScene()` вызывает `OnAttached()` у компонентов **каждой** сцены, и каждый подписывается на глобальные события.
- `SceneManager::SetActiveScene()` / `Initialize()` только меняют указатель `_activeScene` (`SceneManager.cpp:9,39`). Поэтому при двух сценах квады и спрайты неактивной сцены рисуются поверх активной, `ClickableComponent` неактивной сцены эмитит `ActionEvent` по клику, `ChangeToRandomColorDebugComponent` реагирует на чужие действия.
- Сейчас симптом скрыт: `SceneConfig.xml` игры заменяет движковый целиком, и игровой exe не грузит `MainScene`. Chess сам проверяет `GameManager::GetActive()`, Match3 не проверяет ничего.

Подписки в компонентах, которые надо исправить (все найдены grep'ом по `Subscribe<`):

| Файл | Событие |
|------|---------|
| `BECore/Scene/Components/QuadRendererComponent.cpp:12` | `SceneDrawEvent` |
| `BECore/Scene/Components/SpriteRendererComponent.cpp:38,57` | `SceneDrawEvent` (в `OnAttached` и `ReloadTexture`) |
| `BECore/Scene/Components/ClickableComponent.cpp:16` | `MouseButtonDownEvent` |
| `BECore/Scene/Components/ChangeToRandomColorDebugComponent.cpp:13` | `ActionEvent` |
| `games/Chess/.../ChessBoardComponent.cpp:46,47` | `SceneDrawEvent`, `MouseButtonDownEvent` |
| `games/Chess/.../ChessPieceComponent.cpp:39` | `SceneUpdateEvent` |
| `games/Match3/.../Match3BoardComponent.cpp:53,54` | `SceneDrawEvent`, `MouseButtonDownEvent` |

## Решение

Из двух вариантов записи (подписка только пока сцена активна, либо фильтр по сцене в обработчике) выбран **фильтр, собранный в одном месте в `IComponent`**:

- `Scene` хранит флаг `_active`, владелец флага — `SceneManager` (`PassKey<SceneManager>`).
- `IComponent::IsSceneActive()` и защищённый шаблон `SubscribeInScene<Event, &Handler>(this)`: подписка с обёрткой, которая вызывает обработчик, только если сцена компонента активна. Обёртка — свободная статическая функция `InvokeInScene<Event, Handler, T>(T* self, const Event&)`, `entt` умеет `connect<&free_function>(instance)`, а `SubscriptionHolder::Subscribe` уже принимает любой `auto Handler`, так что `EventBase` и `SubscriptionHolder` не меняются.
- Обычный `Subscribe` остаётся для подписок, которые должны работать всегда.

Почему так, а не подписка/отписка при `SetActiveScene`:

- `Events/CLAUDE.md`: «Connect or disconnect inside an event handler — undefined behavior». `GameManager::SwitchTo` → `SetActiveScene` сегодня вызывается только при старте, но ничто не мешает вызвать его из обработчика `ActionEvent`; фильтр не трогает соединения вообще.
- Работает и для входных событий (`MouseButtonDownEvent`, `ActionEvent`), которые сцена не эмитит. Вариант «событие несёт сцену» их не покрывает.
- Тестируется без `CoreManager::GetSceneManager()`: в тестовом окружении (`OnTestEnvironmentInit`) `_sceneManager` не создаётся, а флаг лежит на самой `Scene`.

Минус, о котором стоит знать: диспетчеризация остаётся глобальной, лишние компоненты получают вызов и тут же возвращаются. Для текущих размеров сцен это ничего не стоит. Забыть `SubscribeInScene` в новом компоненте всё ещё можно; это записывается в `Scene/CLAUDE.md` как правило.

Вне объёма: статические указатели `ChessBoardComponent::GetActive()` / `Match3BoardComponent::s_active` (выигрывает последний присоединённый, при двух сценах с шахматной доской они перепутаются), проверки `GameManager::GetActive()` в Chess (это проверка игры, а не сцены, остаются), `SceneEditorWidget` (читает только активную сцену).

## Шаги

1. **Тест, воспроизводящий баг** (на текущем коде должен упасть). `tests/BECore/SceneEventsTest.h` + `.cpp`:
   - в `.h` reflected-компонент `CountingComponent : IComponent` с `BE_CLASS`, счётчики update / draw; подписывается через `SubscribeInScene` (каталог `tests/BECore` уже в `reflection_targets.json`, `.gen.hpp` включается только из `.cpp`);
   - `SceneManager` собирается из XML-строки с двумя сценами `A` и `B` (`XmlDeserializer::LoadFromString` → `BeginObject("SceneManager")` → `Deserialize`, затем `Initialize()`, как в `ReflectionTest.cpp:LoadPlayer`). Две раскладки: **счётная** (в каждой сцене нода `TransformComponent` + `CountingComponent` + `ClickableComponent` с действиями `ActionA` / `ActionB`) и **цветная** (нода `TransformComponent` + `QuadRendererComponent` + `ChangeToRandomColorDebugComponent`);
   - рендер-double не нужен: в цветной раскладке `SceneDrawEvent` не эмитится (`QuadRendererComponent::OnDraw` обратился бы к `CoreManager::GetRenderer()`, которого в тестовом окружении нет), в счётной квадов нет, `IRenderer` не вызывается нигде;
   - кейсы, счётная раскладка: (1) `UpdateAll()` / `DrawAll()` дёргают счётчики только активной сцены; (2) после `SetActiveScene("B")` роли меняются; (3) `SceneUpdateEvent::Emit()` напрямую (не через менеджер) тоже доходит только до активной сцены; (4) клик (`MouseButtonDownEvent::Emit` с `SDL_MouseButtonEvent` внутри прямоугольника обеих нод) даёт ровно один `ActionEvent`, с действием активной сцены (счётчик через подписчика на `SubscriptionHolder`, как в `EventSubscriptionTest.cpp`); (5) неизвестное имя в `SetActiveScene` оставляет активной прежнюю; (6) компонент, добавленный в неактивную сцену через `AddComponent`, не реагирует;
   - кейс, цветная раскладка: (7) `ActionEvent::Emit` меняет `_color` квада только в активной сцене.
2. **Движок:**
   - `Scene.h/.cpp`: `bool IsActive() const`, `void SetActive(bool, PassKey<SceneManager>)`, поле `bool _active = false;` без `BE_REFLECT_FIELD`.
   - `SceneManager.cpp`: общий приватный метод активации, который снимает флаг со старой сцены и ставит на новую; его используют `Initialize()` и `SetActiveScene()`.
   - `IComponent.h/.cpp`: `IsSceneActive() const` (`_scene && _scene->IsActive()`), `SubscribeInScene`, `InvokeInScene` (см. выше), doc-комментарии Doxygen.
3. **Миграция** подписок из таблицы выше на `SubscribeInScene`. `SpriteRendererComponent::ReloadTexture` тоже (иначе после перезагрузки текстуры неактивная сцена снова начнёт рисоваться).
4. **Документация:**
   - `Scene/CLAUDE.md`: переписать пункт «Gotchas» про глобальные события, строку про `SceneManager`/`Scene` в таблице, шаг 3 в «Component lifecycle» и «Adding a component» (подписка из `OnAttached` через `SubscribeInScene`, обычный `Subscribe` только для всегда-активных);
   - `Events/CLAUDE.md`: одна строка рядом с `SceneEvents.h`, что события сцены глобальны, а компоненты фильтруют через `SubscribeInScene`;
   - `TODO.md`: удалить запись «Scene events are global»; в Roadmap п. 1 оставить только CI; в записях «Input has no mapping layer» и «Test gaps and platforms» поправить ссылки на «scene events entry» (двухсценный тест уже есть, диспетчеризация `ClickableComponent` решена);
   - `Games/CLAUDE.md`, `games/Chess/CLAUDE.md`, `games/Match3/CLAUDE.md`: проверить упоминания и поправить, только если они противоречат новому поведению.
5. **Форматирование:** скилл `clang-format` на изменённых файлах, затем `strip-pch-includes` (добавились include'ы в тест и `IComponent`).

## Проверка

- Сначала собрать с одним тестом (шаг 1) и убедиться, что кейсы 1-5 **падают** на текущем коде: это и есть воспроизведение из пункта «Check first» записи. Потом шаги 2-3 и зелёный прогон.
- `cmake --preset macos-debug` (заново: новые файлы и reflected-тип), `cmake --build --preset macos-debug` (BagiChess, BagiMatch3 собираются с миграцией), `cmake --build --preset macos-debug --target BagiEngineTests ChessTests Match3Tests`, `ctest --preset macos-debug`. Число тестов выросло на новые кейсы (было 145), старые не упали.
- Окна в этой среде не открыть, поэтому `BagiChess` / `BagiMatch3` визуально не проверяются: каждая игра держит одну сцену, и миграция для неё сводится к тому, что единственная сцена остаётся активной после `SceneManager::Initialize()` (покрыто тестом, кейс 1). Запуск обеих игр руками остаётся за пользователем.
- Коммит не делается, пока пользователь не попросит. Ветка сейчас `todo-roadmap`.
