# `synchro`: фазовая сетка поверх событий

`nexigen::synchro` — прикладной слой над `com/event-system`. Он публикует события по абсолютной фазовой сетке и превращает поток этих событий в проверяемую временную опору для потребителей. Модуль решает ровно одну задачу — «какая фаза сейчас и насколько мы от неё отклонились», не подменяя транспорт и не добавляя собственный брокер.

Собирается только при `NEXIGEN_ENABLE_COM=ON`. Зависит от `com`, `chrono`, `logging`, `metrics` и `core`; сам `com` о телеметрии не знает.

## Ответственность

- `SyncPayload` — 32-байтовый trivially-copyable payload: номер цикла, индекс и общее число фаз, исходный дедлайн и период цикла. Передаётся zero-copy.
- `SyncSchedule` — арифметика сетки: `T(c, p) = T_start + c·Tc + ⌊p·Tc/N⌋`, `classify_overrun` для пропуска устаревших слотов и `resolve_start` для политик `NextCycle`/`AtTarget` (включая `Reject` и `RealignToNextCycle`).
- `SyncGenerator<Logger>` — один `EventProducer<SyncPayload>` и один worker: гибридное ожидание `ppoll` + финальный спин до дедлайна (`CLOCK_MONOTONIC_RAW`), публикация с исходным `scheduled_time_ns` в payload и фактическим временем в заголовке, счётчики перегрузок и джиттера.
- `SyncConsumer<Logger>` — реализует `com::IEventConsumer`, владеет `com::EventConsumer<SyncPayload>` и `SyncTracker`. Считает IPC- и end-to-end-задержки, классифицирует опоздания (`DropAndWarn`/`DeliverWithFlag`), ведёт watchdog (`initial_sync_timeout_ns`, `sync_loss_timeout_ns`) и вызывает обработчики фаз.
- `SyncRunner<Logger>` — ровно один consumer и один `EventConsumerSet<16>`: `run_once()` возвращает `com::WaitOutcome`, `run()` крутится до `stop_token`, `wake_up()` будит ожидание при остановке.
- `SyncError`/`SyncResult` — собственная модель ошибок с `SyncErrorKind`, транспортом `com::Error` и `core::ErrorCode`.

## Ключевые типы

| Тип | Роль |
|---|---|
| `SyncPayload` | 32-байтовый payload фазы |
| `SyncSchedule` | арифметика сетки и политики старта/overrun |
| `SyncGenerator<Logger>` | публикация фазы по дедлайну |
| `SyncConsumer<Logger>` | приём, задержки, watchdog |
| `SyncRunner<Logger>` | consumer + `EventConsumerSet<16>` |
| `SyncTracker` | непрерывность и классификация скачков |
| `SyncError` / `SyncResult` | модель ошибок |
| `StartPolicy` / `PastTargetPolicy` / `LateEventPolicy` | политики |
| `SyncConsumerStats` / `SyncGeneratorStats` | счётчики |

## Сетка и непрерывность

Абсолютная позиция фазы — `cycle_number * phases_total + phase_index`. `SyncSchedule` вычисляет момент фазы от `T_start` и периода цикла; `classify_overrun` определяет, что слот уже пропущен; `resolve_start` выбирает, с какого цикла начать (`NextCycle`, `AtTarget`, `Reject`, `RealignToNextCycle`).

`SyncTracker` различает два класса отказов:

- **transport gap** — часть скачка объясняется потерянными событиями транспорта;
- **generator skip** — остаток относится к перегрузке генератора.

Смена epoch, изменение `phases_total`, обратный ход цикла и неожиданный reset дают `resync`, а не ложный пропуск. Именно это позволяет отличить «мы потеряли сообщение» от «мы не успели выдать фазу», не смешивая два разных класса.

## Горячий и холодный путь

`SyncGenerator` — worker: `ppoll` ждёт дедлайн, затем короткий финальный спин; публикация идёт через `EventProducer` и `EventHeader` (фактическое время), а `scheduled_time_ns` остаётся в payload. `SyncConsumer` обрабатывает события в `SyncRunner`; задержки, watchdog и обработчики фаз — на этом же потоке. Горячий путь не аллоцирует и не берёт блокировок; инструменты метрик — `MultiThreaded`, имена фиксированы, дубликат регистрации — ошибка создания канала.

## Связи

```text
SyncGenerator (worker)
   |  SyncPayload + EventHeader (scheduled/actual)
   v
EventChannel<SyncPayload> (com, iceoryx2)
   |
   v
EventConsumerSet<16> -> SyncRunner -> SyncConsumer -> handlers
                                      |
                                      +-> SyncTracker (skips/resync)
                                      +-> metrics (latency, skips, jitter)
                                      +-> watchdog
```

`synchro` использует `chrono` для `CLOCK_MONOTONIC_RAW`, `logging` для диагностики и `metrics` для инструментов. `nexigen-sm`/`phm`/`exec` не участвуют в фазовой сетке; это прикладной слой для задач, которым нужна общая фаза.

## Контракты и границы

- Модуль доступен только при `NEXIGEN_ENABLE_COM=ON`.
- Каждый канал требует собственного `metrics::Meter`; имена инструментов фиксированы, дубликат — ошибка.
- Все инструменты `MultiThreaded`; горячий путь без выделений и блокировок.
- `EventConsumerSet` — до 16 consumer'ов в одном наборе; больше — несколько runner'ов/каналов.
- Минимальный период фазы в v1 — 1 мс.
- `SyncPayload` — 32 байта, trivially copyable; публикуется zero-copy.
- Один consumer и один `EventConsumerSet` на `SyncRunner`; несколько каналов — несколько runner'ов.
- `LateEventPolicy` (`DropAndWarn`/`DeliverWithFlag`) и watchdog-таймауты задаются конфигурацией.
- Ошибки не смешиваются: `SyncErrorKind` отделяет транспорт от нарушения расписания.

## Capabilities

`consumer`, `generator`, `protocol`.

## Читать дальше

- [`docs/architecture.md`](../architecture.md), раздел 4.3b — полная карта типов и диаграмма.
- [`platform.md`](platform.md) — место `synchro` в плоскости данных.
- [`com.md`](com.md) — событийные каналы и `WaitSet`.
- [`chrono.md`](chrono.md) — монотонная шкала для дедлайнов.
- [`metrics.md`](metrics.md) — инструменты задержек и пропусков.
- Спецификации: `openspec/specs/synchro/*`.
