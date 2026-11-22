# `tracing`: спаны и контекст

`nexigen::tracing` отвечает за распределённую трассировку: RAII-спаны, распространение контекста между потоками и процессами, доставку записей в sinks и аварийные снимки trace-демона. Как и логирование, это асинхронный конвейер: горячий путь только фиксирует начало/конец спана, а форматирование и запись делает холодная сторона.

## Ответственность

- `TracerProvider<ShardCapacity = 1024, MaxThreads = 32>` — владеет всем объёмом памяти трассировки: per-thread SPSC-shards, отдельный `StampSource` на слот, счётчики потерь и пул tracer'ов. Объект порядка 10 MiB, поэтому живёт в статической памяти или на куче.
- `Tracer`, `ScopedSpan` — старт/завершение спана; RAII-гард закрывает спан при выходе из области.
- `SpanContext`, `TraceId`, `SpanId`, `TraceHeader` — распространение контекста между потоками и процессами.
- `SpanRecord`, `SpanFrame`, `SpanKind`, `SpanStatus`, `SamplingDecision` — запись, кадр и словари.
- Sinks: `ConsoleTraceSink`, `JsonFileTraceSink`, `ChromeTraceSink` (Perfetto-совместимый), `Iceoryx2TraceSink`, `NullTraceSink`, `CompositeTraceSink`; `RotationPolicy` управляет ротацией файлов.
- `TraceDaemonConfig` и `TraceDaemonSession` — RAM-ring последних секунд и версионированный снимок по аварийному событию PHM.

## Ключевые типы

| Тип | Роль |
|---|---|
| `TracerProvider<...>` | владелец памяти, shard'ов и tracer'ов |
| `Tracer` / `ScopedSpan` | старт/конец спана, RAII |
| `SpanContext` / `TraceId` / `SpanId` | контекст трассировки |
| `TraceHeader` | передача контекста между процессами |
| `SpanRecord` / `SpanFrame` | запись и фиксированный кадр |
| `SpanKind` / `SpanStatus` / `SamplingDecision` | словари |
| sinks | console / JSONL / Chrome / iceoryx2 / null / composite |
| `RotationPolicy` | ротация файлов |
| `TraceDaemonConfig` | конфигурация trace-демона |
| `TraceDaemonSession` | RAM-ring и аварийный снимок |

## Жизненный цикл спана

```text
Tracer::start_span(...) -> ScopedSpan
        |
        +-- SpanContext (trace_id, span_id, parent_span_id)
        +-- SpanRecord (kind, status, timestamps, attributes)
        |
        v
ScopedSpan::~ScopedSpan -> publish record -> per-thread shard -> worker -> sink
```

`ScopedSpan` закрывает спан детерминированно: при выходе из области, а не по сборке мусора. Контекст можно передать в другой поток или процесс через `TraceHeader`; тогда дочерний спан ссылается на родителя, а не начинается «с нуля».

## Горячий и холодный путь

Горячий путь — старт/конец спана: запись в per-thread shard, штамп из `StampSource`, без аллокаций и блокировок. Холодный — рендеринг `SpanRecord`, JSONL/Chrome, публикация `SpanFrame`, запись в файл, ротация. `TracerProvider` выделяет всю память при инициализации; в установившемся режиме новых аллокаций нет.

## Sinks и trace-демон

- `JsonFileTraceSink` — JSONL-поток с `RotationPolicy`.
- `ChromeTraceSink` — Perfetto-совместимый JSON.
- `Iceoryx2TraceSink` — фиксированный `SpanFrame` в `nexigen-traced`; живёт в `nexigen_tracing_com`.
- `ConsoleTraceSink`/`NullTraceSink`/`CompositeTraceSink` — отладка, заглушка и fan-out.

`nexigen-traced` держит RAM-ring последних секунд и по аварийному событию PHM делает версионированный снимок. Это не «логгер на всякий случай»: снимок фиксирует состояние, которое иначе было бы потеряно, и делает это по внешнему триггеру.

## Связи

```text
application
  TracerProvider -> per-thread shards -> worker -> sink
        |                                      |
        |                                      +--> JSONL / Chrome
        |                                      +--> SpanFrame -> nexigen-traced
        +-- TraceHeader -> другой поток/процесс
```

`tracing` зависит от `core` и `chrono`; UTC в JSONL резолвится через time base. `app` владеет `TracerProvider` и вызывает `tracer.quiesce()` при остановке. `phm` даёт событие для аварийного снимка trace-демона. `hub` читает JSONL/снимки как артефакты.

## Контракты и границы

- `TracerProvider` — крупный объект (~10 MiB); статическая память или куча, не стек.
- Горячий путь без аллокаций и блокировок; per-thread shards.
- `ScopedSpan` закрывает спан детерминированно; контекст передаётся через `TraceHeader`.
- `SamplingDecision` фиксирует решение; отбор не переписывает уже начатые спаны.
- `TraceDaemonSession` — RAM-ring; снимок по событию PHM, а не постоянная запись.
- `Iceoryx2TraceSink` — только через `nexigen_tracing_com`.
- Ротация файлов — `RotationPolicy`; JSON/Chrome — холодный путь.
- `tracing` не зависит от `com`; COM-sink — отдельная цель.

## Capabilities

`examples`, `iceoryx2-trace-sink`, `record-format`, `scoped-span`, `span-context`, `span-enums`, `trace-daemon`, `trace-propagation`, `tracer-concepts`, `tracer-provider`, `trace-sinks`, `tracing-settings`.

## Читать дальше

- [`docs/architecture.md`](../architecture.md), раздел 4.6 — карта типов и trace-демона.
- [`platform.md`](platform.md) — место `tracing` в наблюдаемости.
- [`app.md`](app.md) — владение `TracerProvider` и shutdown.
- [`logging.md`](logging.md), [`metrics.md`](metrics.md) — родственные конвейеры.
- Спецификации: `openspec/specs/tracing/*`.
