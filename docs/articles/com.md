# `com`: межпроцессный обмен без утечек iceoryx2

`nexigen::com` — тонкая объектная обёртка над iceoryx2. Её задача — дать приложению zero-copy pub/sub, request/response, события и `WaitSet`, не выпуская наружу типы `iox2::` и не заставляя прикладной код знать, как устроена разделяемая память.

Модуль собирается только при `NEXIGEN_ENABLE_COM=ON`. Это не «опция для удобства»: iceoryx2 — Rust-компонент, и вся платформа построена так, чтобы без него можно было собрать `chrono`, логирование, метрики, трассировку, рекордер и `chrono`-демон. `com` — отдельный слой L1, который появляется, когда нужен обмен между процессами.

## Ответственность

- `Node` — участие процесса в коммуникационной фабрике.
- `Publisher<T>`/`Subscriber<T>` — публикация и подписка без копирования; `Sample<T>`/`SampleMut<T>` — read-only и loaned-представления слота.
- `Client<Req, Resp>`/`Server<Req, Resp>` — request/response; `PendingResponse` — отложенный ответ.
- `Event`/`EventId` — уведомления; `Field<T>` — последнее опубликованное значение; `WaitSet` — мультиплексирование ожиданий в ядре с callback progression и RAII-гардами вложений.
- `ServiceManifest`/`ServiceName` — архитектурные параметры сервисов: глубина истории, число подписчиков, QoS. Размеры payload выводятся из типа на этапе компиляции.
- `com/e2e` — E2E-защита: `E2eHeader` (16 байт), профили `None`, `P04`, `P05`, `P06`, `P07`, обёртки `E2ePublisher`/`E2eSubscriber`/`E2eClient`/`E2eServer`, счётчики и `max_delta_counter`.
- `EventChannel<T>` — прикладная фабрика `producer()`/`consumer()` поверх `Publisher`/`Subscriber`, `EventNotifier`/`EventListener` и `WaitSet`.

## Ключевые типы

| Тип | Роль |
|---|---|
| `Node` | процесс в iceoryx2 |
| `Publisher<T>` / `Subscriber<T>` | zero-copy pub/sub |
| `Sample<T>` / `SampleMut<T>` | read-only / loaned слот |
| `Client<Req, Resp>` / `Server<Req, Resp>` | request/response |
| `PendingResponse` | отложенный ответ сервера |
| `Event` / `EventId` | уведомление |
| `Field<T>` | последнее значение |
| `WaitSet` / `WaitSetGuard` | мультиплексирование ожиданий |
| `ServiceManifest` / `ServiceName` | параметры сервиса |
| `E2eHeader` / `E2ePublisher` / ... | E2E-защита |
| `EventChannel<T>` | событийный канал |
| `EventHeader` (72 Б) | метаданные события |
| `EventProducer<T>` / `EventConsumer<T>` | публикация/приём событий |
| `EventConsumerSet<MaxConsumers>` | до 16 consumer'ов в одном потоке |
| `com::Error` | ошибки без типов iceoryx2 |

## Событийные каналы

`EventChannel<T>` создаётся только через `EventChannel<T>::create(node, name, config)`; `ChannelConfig` валидируется до открытия сервисов. `EventProducer<T>` назначает sequence только после успешной отправки, возвращает `Notified`/`NotifyFailed` и умеет повторить wake без повторной публикации. `EventConsumer<T>` отслеживает потери по transport sequence: первый sample задаёт baseline, gap даёт точный `lost_count`, duplicate отбрасывается, sequence reset и смена epoch классифицируются отдельно. `EventConsumerSet<MaxConsumers>` мультиплексирует до 16 consumer'ов в одном потоке, поддерживает timeout и `wake_up()` для штатной остановки.

`EventHeader` — 72 байта:

| Смещение | Размер | Поле |
|---:|---:|---|
| 0 | 8 | `sequence_id` |
| 8 | 8 | `timestamp_ns` — каноническая проекция, опционально |
| 16 | 8 | `producer_epoch` |
| 24 | 8 | `ingest_timestamp_ns` — host `CLOCK_MONOTONIC_RAW` |
| 32 | 8 | `domain_epoch` |
| 40 | 8 | `native_ticks` — instant в native-домене |
| 48 | 4 | `event_code` |
| 52 | 4 | `hardware_event_code` |
| 56 | 4 | `hardware_sequence` |
| 60 | 2 | `domain_id` |
| 62 | 2 | `flags` |
| 64 | 4 | `app_id` |
| 68 | 4 | padding |

Время события хранится в том домене, который его произвёл: `native_ticks` + `domain_id` + `domain_epoch` авторитетны, а `timestamp_ns` — только каноническая проекция и имеет смысл при установленном `has_canonical_projection_bit`. Host-события публикуются в каноническом домене и всегда несут проекцию.

## Горячий и холодный путь

Горячий путь — публикация/приём sample и событий: zero-copy, без аллокаций и без сериализации. `WaitSet` спит в ядре, а не крутится в busy-poll. Холодный путь — создание node, сервисов, манифестов, `ServiceName`, настройка E2E и разбор ошибок; здесь возможны аллокации iceoryx2.

## Связи

```text
application / daemon
        |
        +-- Publisher/Subscriber <T> --+
        +-- Client/Server <Req,Resp> ---+--> iceoryx2 shared memory
        +-- EventChannel <T> -----------+
        +-- Field <T> ------------------+
        +-- WaitSet --------------------+--> kernel wait
```

`com` зависит от `core` и `iceoryx2-cxx`. На нём стоят `phm`, `sm`, `synchro`, `nexigen-logd`/`nexigen-metricsd`/`nexigen-traced`, а также COM-части `exec`. Телеметрические модули остаются COM-свободными: их iceoryx2-sink'и живут в интерфейсных целях `nexigen_logging_com`/`nexigen_metrics_com`/`nexigen_tracing_com`.

## Контракты и границы

- Типы `iox2::` не протекают в прикладной код; ошибки — `com::Error`.
- `ServiceManifest`/`ServiceName` задают глубину истории, число подписчиков и QoS; размеры payload выводятся из типа.
- `WaitSet` — единственный штатный способ ждать несколько источников; callback progression и RAII-гарды не дают вложению пережить listener.
- E2E-профили защищают от искажения, повторов, потерь, переупорядочения и ошибочной доставки; `max_delta_counter` ограничивает допустимый разрыв.
- `EventConsumerSet` — до 16 consumer'ов в одном наборе; больше — несколько наборов/потоков.
- `EventHeader` фиксирован (72 байта); native domain/epoch — авторитетны, canonical projection — опциональна.
- Модуль требует `NEXIGEN_ENABLE_COM=ON`; ради cold-path механизма имён типов iceoryx2 он локально включает RTTI, хотя остальной проект собирается с `-fno-rtti`.

## Capabilities

`e2e`, `event-system`, `field`, `publish-subscribe`, `request-response`, `service-management`, `waitset`.

## Читать дальше

- [`docs/architecture.md`](../architecture.md), разделы 4.3/4.3a — карта типов и событийных каналов.
- [`platform.md`](platform.md) — место `com` в плоскости данных.
- [`synchro.md`](synchro.md) — прикладной слой поверх событийных каналов.
- [`phm.md`](phm.md), [`sm.md`](sm.md) — потребители `com` в управляющей плоскости.
- Спецификации: `openspec/specs/com/*`.
