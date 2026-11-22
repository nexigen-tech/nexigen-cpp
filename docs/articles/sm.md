# `sm`: арбитраж состояний

`nexigen::sm` — State Manager, центральный арбитр Function Groups. Он не запускает процессы и не останавливает их; он решает, **какой переход допустим**, кто имеет право его запросить и какое состояние после этого публикуется. Исполнение перехода остаётся за `exec`.

Модуль собирается только при `NEXIGEN_ENABLE_COM=ON` и работает поверх `com`. Его состояние — это не «поле в памяти менеджера», а публикуемое `Field`, которое читают приложения, PHM и EM.

## Ответственность

- Проверка перехода по графу `SmManifest`: допустимые рёбра, приоритеты текущего держателя, startup/shutdown как явные шаги, а не рёбра.
- Публикация состояния группы: `GroupStatePayload`, `StatePublication`.
- Приём запросов: `TriggerRequest`/`TriggerResponse`, `TransitionPreparation` (handshake подготовки).
- Результат и аудит: `TransitionResult`, `TransitionRecord`, `TransitionAudit`; `SmMetrics` — метрики арбитража.
- `LightweightStateMachine`/`LsmRuntime` — декларативные таблицы переходов, действий и recovery для группы.
- `RootFG` — корневая Function Group: `Off`/`Startup`/`Verify`/`Run`/`Shutdown`/`Restart`; `RootLifecycleClient` — клиент начального перехода EM.
- `UpdateCoordinator` — UCM: `Start`/`Complete`/`Cancel`, блокировка группы, состояние `Update`, отказ `RejectedByUpdate`.
- `RecoveryHandler`/`RecoveryNotification` — обработка восстановления от PHM.

## Ключевые типы

| Тип | Роль |
|---|---|
| `SmManifest` | граф переходов, приоритеты, начальные состояния |
| `StateManager` | headless-машина на группу |
| `GroupStatePayload` / `StatePublication` | публикуемое состояние |
| `TriggerRequest` / `TriggerResponse` | входящий запрос перехода |
| `TransitionPreparation` | handshake подготовки |
| `TransitionResult` / `TransitionRecord` / `TransitionAudit` | результат и аудит |
| `SmMetrics` | метрики арбитража |
| `LightweightStateMachine` / `LsmRuntime` | декларативные таблицы и runtime |
| `RootFG` / `RootLifecycleClient` | корневая группа и клиент EM |
| `UpdateRequest` / `UpdateResponse` / `UpdateCoordinator` | UCM-обновление |
| `RecoveryHandler` / `RecoveryNotification` | восстановление от PHM |

## Правила арбитража

1. Переход проверяется по графу. Если ребра нет — отказ, независимо от того, кто просит.
2. Приоритет текущего держателя группы учитывается при конфликте запросов; более приоритетный запрос может вытеснить менее приоритетный.
3. `startup`/`shutdown` — явные шаги жизненного цикла, а не обычные рёбра графа.
4. Прикладная группа не покидает off-like состояние, пока `RootFG` не достиг `Run`.
5. `RootFG::Off` не имеет привязанных процессов; `Run` — целевое состояние; `Restart` — быстрый перезапуск без прохода через `Off`.
6. Во время UCM-обновления группа блокируется; второй update отклоняется, запросы получают `RejectedByUpdate`.
7. Recovery от PHM входит в state machine как запрос, а не как обход правил.

## Как проходит `SetState`

```text
nexigen-sm (StateManager)
   |  SetState(group, state, requester, request_id)
   v
nexigen-exec / EM  -- access policy + request arbitration
   |  FunctionGroupRegistry: state -> processes
   v
application: stop / restart / start
   |  readiness (Run или exit 0)
   v
EM -> StateAck / StateResult -> SM
   |  publish State field + audit record
   v
application / PHM / hub
```

SM не вызывает `ChildProcess` сам. Он публикует намерение и получает результат; EM разрешает состояние в набор процессов и выполняет stop/restart/start. Это разделение не даёт State Manager превратиться во второй супервизор.

## Горячий и холодный путь

SM — control plane. Переходы, аудит, UCM и подготовка — холодные операции; они могут аллоцировать и писать. Публикация состояния идёт через `Field`, а не через файл. Горячего пути в смысле наносекунд здесь нет; есть требование не блокировать `SetState` дольше, чем нужно для принятия решения.

## Связи

- `com` — публикация состояний, приём `TriggerRequest`, `Field`.
- `exec` — исполнение переходов, `StateClient`, `FunctionGroupRegistry`.
- `phm` — recovery-запросы и `RecoveryNotification`; PHM использует словари `sm` на уровне заголовков.
- `hub` — читает состояния через артефакты/телеметрию, но не управляет SM.
- `app` — приложения читают состояние группы и публикуют health/checkpoints.

## Контракты и границы

- Модуль доступен только при `NEXIGEN_ENABLE_COM=ON`.
- `StateManager` — headless-машина на группу; состояние живёт в публикации, а не в глобальном синглтоне.
- Недопустимый переход отклоняется; приоритеты и `request_id` не дают устаревшему запросу выиграть.
- `RootFG` — обязательный контекст: прикладные группы не стартуют, пока корень не в `Run`.
- UCM: один активный update; остальные — `RejectedByUpdate`.
- Аудит неблокирующий; запись перехода не должна задерживать решение.
- SM не управляет процессами напрямую и не дублирует EM.

## Capabilities

`ipc`, `lifecycle`, `lightweight-state-machine`, `lsm-runtime`, `manifest`, `recovery-handler`, `state-machine`, `telemetry`, `ucm-coordination`.

## Читать дальше

- [`docs/architecture.md`](../architecture.md), раздел 4.12 — полная карта типов и сценариев.
- [`platform.md`](platform.md) — место `sm` в плоскости управления.
- [`exec.md`](exec.md) — исполнение переходов.
- [`phm.md`](phm.md) — recovery и SafeState.
- Спецификации: `openspec/specs/sm/*`.
