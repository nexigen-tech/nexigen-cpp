# Design

## Context

Мотивация — в proposal.md. Точка расширения уже существует: `detail::AsyncWorker<Producer, Sink>` — шаблон по duck-typed Sink'у (`start/handle/synchronize/stop`), а `FileLogger`/`ConsoleLogger` — лишь два потребителя одного движка `detail::Producer` (hot path ~20 нс) + воркер. Цель — добавить третий Sink, публикующий в iceoryx2, не трогая продюсер и воркер. Числа берём из `bench-log-iceoryx2-latency`: producer ~7 нс, worker loan/send/notify ~278 нс, heap-delta 0.

## Goals / Non-Goals

**Goals:**
- `Iceoryx2Logger` переиспользует `Producer` + `AsyncWorker` без правок.
- Публикация — worker-publish (off-hot-path), форматирование сообщения остаётся на воркере.
- Минимальный конфиг по ответу заказчика: сервис, подписчики, аффинность, idle-потолок.

**Non-Goals:**
- Сам `LogDaemon` (подписка, финальное форматирование, диск/сеть/crash-ring, ротация) — отдельный change.
- Пер-контекстное управление порогами (`LOGC_003`) — вне скоупа; `context_id` резервируется.
- Рефакторинг `FileLogger`/`ConsoleLogger` в общий `Logger<Sink>` шаблон.

## Decisions

**D1 — Layout `LogFrame` (256 Б, `alignas(64)`).** Точная раскладка:

```
offset  field             size
0       timestamp_ns      8   (монотонное время ОС)
8       app_id            4   (char[4] — 4-байтовый код, напр. "LOGD")
12      context_id        4   (uint32, зарезервирован = 0 до появления контекстов)
16      thread_id         4
20      dropped_counter   4   (дельта дропов с прошлого фрейма)
24      payload_size      2
26      log_level         1
27      payload           229 (рендеренный текст сообщения)
        ──────────────────
        total             256 = 4 кэш-линии
```

`static_assert(sizeof(LogFrame) == 256)` и `alignof == 64`. Замечание: в `initial_spec` заголовок (27 Б) + `payload[232]` дают 259 Б — арифметика спеки не сходится с «256 байт»; за первичное ограничение берём **256 Б** (кратно кэш-линии), payload добирает остаток до 229. `app_id` — `char[4]`, как в манифесте (`app_id: "LOGD"`), а не числовой id.

**D2 — Расположение.** `nexigen::logging::LogFrame` в `include/nexigen/logging/log_frame.hpp` (header-only): формат принадлежит клиенту логирования, а будущий LogDaemon и клиент используют один тип через `nexigen::com::Subscriber<LogFrame>`.

**D3 — Sink без новых атомиков.** `Iceoryx2Sink` реализует контракт `start/handle/synchronize/stop`, как `FileSink`. Собственной конкурентности не вводит: продюсерские shard-кольца остаются в `Producer` (per-thread SPSC, владелец трогает только свой слот), а телеметрия — в уже существующем `Publisher` (relaxed-счётчики, один writer на `send`). Новых `std::memory_order`-решений нет.

**D4 — Два сервиса (`REQ_LOGC_005`).** Sink держит `Publisher<LogFrame>` (данные) и `EventNotifier` (пробуждение) — отдельные сервисы: `service_name` и `service_name + "Event"`. Конструируются в `start()` на воркер-потоке (холодный путь); `handle()` делает `loan_uninit → write_payload → send`, затем `notify()`.

**D5 — Отложенный рендер на воркере.** `handle()` вызывает существующий `rec.render_into(buf)` (текст сообщения) и кладёт результат в `payload`. Продюсер не форматирует — инвариант mandate 7 сохранён; финальное форматирование строки (дата/PID/имя контекста) — на демоне.

**D6 — Конфиг.** `Iceoryx2LoggerConfig` (по ответу заказчика + `app_id`):

| Поле | Тип | Маппинг |
|---|---|---|
| `service_name` | `std::string` (≤511) | имя pub/sub-сервиса |
| `app_id` | `std::string` (≤4) | код в `LogFrame.app_id` |
| `max_subscribers` | `uint32` | глубина подписчиков (`REQ_COM_009`) |
| `history_size` | `uint32` | глубина очереди приёмника |
| `worker_thread` | `core::ThreadSpec` | аффинность воркера (как в `FileLoggerConfig`) |
| `worker_poll_interval_us` | `uint32` | потолок idle-сна воркера (ограничивает его idle CPU) |
| `initial_level` | `core::LogLevel` | начальный порог |
| `throttle` | `ThrottleConfig` | rate-limit (drop-семантика) |

Размер shard-ring остаётся **шаблонным параметром** `ShardCapacity` (как в `FileLogger`), а не runtime-полем — «если возможно» по ответу заказчика: compile-time без потерь на hot path.

**D7 — Drop-дельта в `dropped_counter`.** Воркер на каждом фрейме берёт снапшот `producer_.dropped()` (сумма `buffer_full` + `rate_limited` + `slot_exhausted`), кладёт дельту с предыдущего фрейма в `dropped_counter` и запоминает новое значение — `REQ_LOGC_007` («в следующий успешный фрейм, затем сброс»).

**D8 — Гейт.** `Iceoryx2Logger`/`Iceoryx2Sink` зависят от `nexigen::com` и живут в отдельном целевом `nexigen_logging_com` за `NEXIGEN_ENABLE_COM` (паттерн `nexigen_state_client`): ядро `nexigen_logging` остаётся свободным от iceoryx2. Header подключается только при гейте.

**D9 — Что уезжает в демон.** Ротация, размер/имена файлов, многосинковая маршрутизация, финальное JSON-форматирование — не входят в клиент; `synchronize()` = no-op (данные уже в shm), `stop()` = освобождение портов.

## Risks / Trade-offs

- [Фрейм фиксированного размера теряет длинные сообщения] → `render_into` ограничен `payload` (229 Б); переполнение считается `malformed`-скипом, как в `FileSink` (никогда не полстроки).
- [Публикация с нулём подписчиков] → бенч подтвердил: `send`/`notify` без подписчика проходят без ошибки (сэмпл возвращается в пул).
- [Переезд форматирования в демон сдвигает latency] → это осознанный тред-офф модели (a); продюсер и воркер не просядают.
- [Числа бенча — только на этой машине] → позиционируются как относительное сравнение, не абсолютная гарантия.

## Open Questions

- **Маппинг `context_id`** — появится вместе с пер-контекстным управлением порогами (`LOGC_003`), отдельным change.
- **Схема демона** — какие поля LogFrame рендерит демон в итоговую строку, решается в change LogDaemon'а.
