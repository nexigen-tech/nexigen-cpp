# Архитектура nexigen

Документ описывает устройство платформы `nexigen-cpp`: состав модулей, их
границы, зависимости и взаимодействие компонентов в рантайме. Он собран по
публичным заголовкам `include/nexigen/`, спецификациям `openspec/specs/` и
целям сборки `CMakeLists.txt`.

Актуально на 18.11.2026: 13 модулей, 379 публичных заголовков, 126
capability, 906 требований и 2317 сценариев в OpenSpec.

## 1. Назначение и принципы

`nexigen-cpp` — библиотека C++23 для Linux для систем жёсткого реального
времени и симуляторов аппаратуры. Платформа даёт приложению готовые
примитивы времени, телеметрии, межпроцессного обмена, супервизии процессов,
управления состояниями и наблюдаемости.

Инварианты, общие для всех модулей:

- **Контракт первичен.** Поведение описывается capability-спецификациями в
  `openspec/specs/<module>/<capability>/spec.md`; код приводится к спецификации,
  а не наоборот.
- **Ошибки без исключений.** Глобальные `-fno-exceptions -fno-rtti`; публичные
  fallible-API возвращают `core::Result<T, ErrorCode>` (или `core::Status`).
  Ветвление — по `has_value()`/`.error()`, вызов `.value()` запрещён.
- **Горячий путь без выделения памяти и блокировок.** Производитель пишет
  только в свой SPSC-shard; форматирование, JSON и файловый ввод-вывод
  вынесены на холодный путь.
- **Один namespace — один модуль.** Внутренности живут в `detail/` и не
  являются публичным контрактом. Зависимости направлены только вниз.
- **Опциональный COM-слой.** Всё, что требует iceoryx2 (Rust), собирается
  только при `NEXIGEN_ENABLE_COM=ON`; остальные модули от него не зависят.
- **Сериализация как мандат.** Публичные типы имеют холодные `to_string()`,
  ADL `to_json`/`from_json` (при `JSON_NOEXCEPTION=1`), а перечисления —
  `constexpr to_string`/`from_string` и `std::formatter`.

## 2. Карта модулей

| Слой | Модуль | CMake-цель | Тип | Заголовки | Назначение |
|---|---|---|---:|---:|---|
| L0 | `nexigen::core` | `nexigen_core` | INTERFACE | 25 | `Result`/`ErrorCode`, сериализация, `LogLevel`, enum-словари, SPSC-очереди, потоки и affinity, sink/settings-инфраструктура, integrity-примитивы |
| L1 | `nexigen::chrono` | `nexigen_chrono` | STATIC | 27 | `Timestamp`, `CivilTime`, калиброванный TSC, `Provider`/`Worker`, UTC-смещение, time-sync монитор, sinks и настройки |
| L1 | `nexigen::com` | `nexigen_com` | STATIC, gate | 54 | SOA поверх iceoryx2: `Node`, pub/sub, request/response, `Event`, event channels, `Field`, `WaitSet`, `ServiceManifest`, E2E |
| L2 | `nexigen::logging` | `nexigen_logging` | STATIC | 37 | Асинхронные логгеры, `LogRecord`, уровни, anti-spam, ротация, кадры и настройки |
| L2 | `nexigen::metrics` | `nexigen_metrics` | STATIC | 49 | Счётчики, gauge, гистограммы, статистика, `MetricProvider`, sinks, Prometheus, кадры |
| L2 | `nexigen::tracing` | `nexigen_tracing` | STATIC | 35 | `TracerProvider`, `ScopedSpan`, контекст трассировки, sinks, кадры и daemon-сессия |
| L2 | `nexigen::diagnostics` | `nexigen_diagnostics` | STATIC | 5 | `Stopwatch`, `ScopedStopwatch`, снимки интервалов |
| L2 | `nexigen::recorder` | `nexigen_recorder` | STATIC | 15 | Raw session storage: `.nxrec`/`.nxidx`/`.nxvid` + `session.json`, writer/reader, CRC-32C, rotation/retention, dataset inventory, video refs |
| L3 | `nexigen::app` | `nexigen_app` | STATIC | 11 | Командная строка, настройки, `Infrastructure`, телеметрическая раскладка, `signalfd` |
| L3 | `nexigen::exec` | `nexigen_exec` | STATIC | 56 | Suite/instance lifecycle, супервизия процессов, Function Groups, манифесты, platform controls |
| L3 | `nexigen::hub` | `nexigen_hub` | STATIC | 15 | Discovery, ограниченные потоки с gap-маркерами, HTTP/SSE-сервер, web UI |
| L3 | `nexigen::phm` | `nexigen_phm` | STATIC, gate | 31 | Platform Health Management: супервизия, checkpoints, claims, recovery, watchdog |
| L3 | `nexigen::sm` | `nexigen_sm` | STATIC, gate | 19 | State Management: арбитраж Function Groups, RootFG, LSM, UCM |

Дополнительные цели, появляющиеся только при `NEXIGEN_ENABLE_COM=ON`:
`nexigen_state_client` (приёмная сторона `SetState` в EM),
`nexigen_exec_control` (COM-реализация `ControlHost`), а также интерфейсные
обёртки `nexigen_logging_com`, `nexigen_metrics_com`, `nexigen_tracing_com`
для iceoryx2-sink'ов.

`nexigen::core` — INTERFACE, потому что состоит из шаблонов и inline-кода.
Остальные модули компилируют холодные пути (калибровка, harvest-циклы,
файловый и сетевой ввод-вывод, разбор манифестов) в статические архивы.

`nexigen::recorder` — единственный модуль хранения. Он зависит только от
`core` и `chrono` и по построению не линкует COM, media, MCAP или FFmpeg.
`nexigen-export` — отдельный on-demand инструмент поверх `nexigen_recorder`
(опционально с MCAP); MP4/MKV остаются follow-up до появления
`nexigen_media`.

## 3. Слои и зависимости

```mermaid
flowchart TB
    subgraph L4["L4 · инструменты"]
        TOOLS["nexigen-exec · nexigen-em · nexigen-hub<br/>nexigen-logd · nexigen-traced · nexigen-metricsd<br/>nexigen-phmd · nexigen-sm · nexigen-export"]
        EXP["nexigen-export<br/>on-demand converter"]
    end

    subgraph L3["L3 · платформенные сервисы"]
        APP["app<br/>CLI · settings · telemetry layout · Infrastructure"]
        EXEC["exec<br/>suite · instances · supervision · Function Groups"]
        HUB["hub<br/>discovery · streaming · HTTP/SSE"]
        PHM["phm<br/>supervision · recovery · watchdog"]
        SM["sm<br/>arbitration · RootFG · UCM · LSM"]
    end

    subgraph L2["L2 · телеметрия и хранение"]
        LOG["logging<br/>loggers · records · daemon transport"]
        MET["metrics<br/>instruments · provider · sinks"]
        TRC["tracing<br/>provider · spans · sinks"]
        DIA["diagnostics<br/>stopwatch"]
        REC["recorder<br/>session writer/reader · CRC · retention"]
    end

    subgraph L1["L1 · примитивы"]
        CHR["chrono<br/>timestamp · stamp source · provider"]
        COM["com<br/>iceoryx2 SOA"]
    end

    CORE["core · L0<br/>Result · serialization · SPSC · threading · integrity"]

    TOOLS --> APP
    TOOLS --> EXEC
    TOOLS --> HUB
    TOOLS --> PHM
    TOOLS --> SM
    TOOLS --> REC
    EXP --> REC

    APP --> LOG
    APP --> MET
    APP --> TRC
    APP --> CHR
    EXEC --> APP
    HUB --> EXEC
    PHM --> COM
    SM --> COM

    LOG --> CHR
    MET --> CHR
    TRC --> CHR
    DIA --> CHR
    REC --> CHR

    APP --> CORE
    EXEC --> CORE
    HUB --> CORE
    PHM --> CORE
    SM --> CORE
    CHR --> CORE
    COM --> CORE
    LOG --> CORE
    MET --> CORE
    TRC --> CORE
    DIA --> CORE
    REC --> CORE

    LOG -.->|"logging_com"| COM
    MET -.->|"metrics_com"| COM
    TRC -.->|"tracing_com"| COM
    EXEC -.->|"state_client (COM)"| COM
    PHM -.->|"state / recovery vocabulary"| SM

    classDef optional fill:#fff4d6,stroke:#b8860b,stroke-dasharray: 5 4,color:#333
    class COM,PHM,SM,TOOLS optional
```

Правила, которые проверяет граф включений:

- `core` зависит только от стандартной библиотеки и `nlohmann/json`;
  `chrono` и `com` — только от `core`; телеметрия — от `core`/`chrono`.
- `recorder` зависит только от `core`/`chrono` и не линкует COM, media, MCAP
  или FFmpeg; `nexigen-export` — отдельная цель поверх `recorder`.
- `app` — единственный владелец подсистем телеметрии; `exec` использует
  `app`, но не тянет `com` в незагеймленное ядро.
- `hub` зависит от `exec` только ради типов раскладки запусков; он ничего не
  запускает и не изменяет.
- `phm` и `sm` работают поверх `com`; их связь с `exec` идёт через
  сообщения, а не через линковку.

Осознанные исключения, все — внутри `detail/` и не часть публичного
контракта:

- `metrics/detail/metric_http_server.hpp` использует
  `hub/detail/http_parse.hpp`, чтобы не дублировать HTTP-парсер;
- `tracing/detail/trace_daemon_session.hpp` включает типы `exec` и `sm` для
  состояния trace-демона;
- заголовки `phm` включают словари `sm` (`group_state_payload`,
  `recovery_notification`), потому что PHM и SM обмениваются этими
  сообщениями.

## 4. Модули

### 4.1 `core` — базовые примитивы

**Ответственность.** Фундамент, на котором стоят все остальные модули:
модель ошибок, сериализация, словари перечислений, примитивы конкурентности,
управление потоками, общие cold-path утилиты и криптографические хеши.

**Ключевые типы.**

- `Result<T, E = ErrorCode>` и `Status` над `std::expected`; девять кодов:
  `Ok`, `BufferFull`, `BufferEmpty`, `InvalidArgument`, `SystemError`,
  `NotInitialized`, `RateLimited`, `Conflict`, `ResourceExhausted`.
- `serialization_mandate`, `EnumTraits`, `FixedWriter` — единый контракт
  `to_string`/`to_json`/`from_json`/`std::formatter`.
- `SpscRingBuffer<T, Capacity>` — wait-free очередь с одной записью и одним
  чтением, индексы на отдельных cache line.
- `ShardPool` (`shard_pool`) — пул per-thread shard'ов для многопоточных
  производителей.
- `thread_management`: `CpuSet` до 1024 CPU, `ThreadSpec` (имя, политика
  Other/FIFO/RR, приоритет, required/best-effort), `ThreadApplyReport`,
  `apply_thread_spec_self`; `worker_thread` и `worker_tuning` задают
  поведение фоновых потребителей.
- `sink_pack::Fanout`, `sink_io`, `settings_io`/`settings_paths`/
  `settings_source`/`settings_diagnostic` — общая инфраструктура sinks и
  настроек.
- `integrity`: CRC-32C, CRC-64/XZ, SHA-256, сравнение дайджестов за
  постоянное время, `read_verified(path, sha256)`.

**Горячий/холодный путь.** SPSC-очередь и форматирование enum'ов — hot path;
парсинг настроек, JSON и файловые операции — cold path.

**Зависимости.** Только стандартная библиотека и `nlohmann/json`.

**Capabilities (11):** `affinity`, `integrity`, `log-level`, `result`,
`serialization`, `settings-vocabulary`, `sink-plumbing`, `spsc-ring-buffer`,
`thread-management`, `worker-thread`, `worker-tuning`.

### 4.2 `chrono` — время и калиброванный TSC

**Ответственность.** Монотонное время, гражданское время и UTC, калибровка
TSC, эталонные часы процесса, периодические снимки смещения UTC и контроль
пересинхронизации системных часов.

**Ключевые типы.**

- `Timestamp` — наносекунды монотонного времени; `CivilTime`, `ClockId` —
  форматирование и домены часов; `utc_offset` — преобразование монотонного
  штампа в UTC по снятому смещению.
- `StampSource` — hot-path источник штампа: `__rdtsc()` плюс
  зафиксированная при калибровке пара `(tsc_anchor, mono_anchor)` и
  fixed-point множитель; периодическая перекалибровка, монотонный clamp и
  безопасный откат на `Timestamp::now()` при ошибке или на не-x86.
- `Provider<MaxSources>` — «беспоточный» авторитет времени: выдаёт stamper'ы
  производителям, снимает UTC-смещение на холодном пути и доставляет
  снимки в sink по триггеру. Банк источников фиксирован; исчерпание —
  `ResourceExhausted`.
- `Worker` — опциональная демон-обёртка для периодических снимков.
- Sinks: `ConsoleSink`, `JsonFileSink`, `NullSink`, `CompositeSink`;
  `settings` и `snapshot` описывают загрузку конфигурации и сохранение
  смещения.
- `TimeSyncMonitor` сравнивает `CLOCK_REALTIME − CLOCK_MONOTONIC_RAW` и
  классифицирует переходы как `Ok`, `Stepped`, `GoingBackwards`,
  `RateExceeded`; по умолчанию порог шага 1 мс, скорость 500 ppm, окно
  100 мс. Монитор не создаёт поток и не выделяет память.

**Зависимости.** `core`.

**Capabilities (10):** `chrono-settings`, `chrono-sink`, `chrono-snapshot`,
`clock-id`, `provider`, `stamp-source`, `time-format`, `timestamp`,
`time-sync`, `worker`.

### 4.3 `com` — межпроцессный обмен (gate)

**Ответственность.** Тонкая объектная обёртка над iceoryx2, которая скрывает
`iox2::` от прикладного кода и даёт zero-copy паттерны AUTOSAR-подобной SOA.

**Ключевые типы.**

- `Node` — участие процесса в коммуникационной фабрике.
- `Publisher<T>`/`Subscriber<T>` — публикация и подписка без копирования;
  `Sample<T>`/`SampleMut<T>` — read-only и loaned-представления слота.
- `Client<Req, Resp>`/`Server<Req, Resp>` — request/response;
  `PendingResponse` — отложенный ответ.
- `Event`/`EventId` — уведомления; `Field<T>` — последнее опубликованное
  значение; `WaitSet` — мультиплексирование ожиданий в ядре с callback
  progression и RAII-гардами вложений.
- `ServiceManifest`, `ServiceName` — архитектурные параметры сервисов
  (глубина истории, число подписчиков, QoS); размеры payload выводятся из
  типа на этапе компиляции.
- `com/e2e` — E2E-защита: `E2eHeader` (16 байт), профили `None`, `P04`,
  `P05`, `P06`, `P07`, обёртки `E2ePublisher`, `E2eSubscriber`, `E2eClient`,
  `E2eServer`, счётчики и `max_delta_counter`.
- `com::Error` — собственная модель ошибок без утечки типов iceoryx2.

**Особенности.** Модуль собирается только при `NEXIGEN_ENABLE_COM=ON`.
Ради cold-path механизма имён типов iceoryx2 модуль локально включает RTTI,
хотя остальной проект собирается с `-fno-rtti`.

**Зависимости.** `core`, `iceoryx2-cxx`.

**Capabilities (7):** `e2e`, `event-system`, `field`, `publish-subscribe`,
`request-response`, `service-management`, `waitset`.

### 4.3a `com/event-system` — событийные каналы

`EventChannel<T>` — прикладная фабрика `producer()`/`consumer()` поверх
`com::Publisher`/`Subscriber`, `EventNotifier`/`EventListener` и `WaitSet`.
Создание идёт только через `EventChannel<T>::create(node, name, config)`,
который валидирует `ChannelConfig` до открытия сервисов.

`EventHeader` (64 B) несёт transport sequence, каноническое время события и
его домен, producer epoch, host ingest time и опциональные FPGA-поля
(`hardware_sequence`, `hardware_timestamp_ns`, `hardware_event_code`).
`EventProducer<T>` назначает sequence только после успешной отправки,
возвращает `Notified`/`NotifyFailed` и умеет повторить wake без повторной
публикации. `EventConsumer<T>` отслеживает потери по transport sequence:
первый sample задаёт baseline, gap даёт точный `lost_count`, duplicate
отбрасывается, sequence reset и смена epoch классифицируются отдельно.
`EventConsumerSet<MaxConsumers>` мультиплексирует до 16 consumer'ов в одном
потоке, поддерживает timeout и `wake_up()` для штатной остановки.

```mermaid
flowchart LR
    P[producer / gateway] --> EP[EventProducer]
    EP --> PUB[Publisher + EventHeader]
    EP --> N[EventNotifier]
    PUB --> SM[(iceoryx2 shared memory)]
    N --> K[event wake-up]
    SM --> SUB[Subscriber + EventHeader]
    K --> L[EventListener]
    SUB --> EC[EventConsumer]
    L --> CS[EventConsumerSet / WaitSet]
    EC --> CB[event callback]
    EC --> LC[loss callback]
```

Подробности: [com-event-system.md](com-event-system.md).

### 4.4 `logging` — асинхронные логи

**Ответственность.** Запись логов без блокировок на горячем пути:
производитель кладёт отложенный формат в свой shard, фоновый потребитель
форматирует и отдаёт запись в sink.

**Ключевые типы.**

- Концепт логгера и семейство: `ConsoleLogger`, `FileLogger`,
  `CompositeLogger`, `NullLogger`, `Iceoryx2Logger`.
- `LogRecord` — payload между производителем и потребителем с отложенным
  набором аргументов; `format_with_location` добавляет место вызова.
- `LoggerOptions` — единая конфигурация инициализации (ёмкости, пороги,
  `ThreadSpec`, overflow policy); после старта читаются только relaxed
  atomics.
- Многопроизводительный режим: до `MaxThreads` (по умолчанию 32) потоков,
  у каждого свой SPSC-shard, единственный потребитель — внутренний worker.
- Anti-spam: квоты, окна throttle, `SpamProtectionConfig`,
  `ThrottleConfig`; статистика потерь — `DroppedStats`.
- Ротация: `RotationConfig`, `RotationSchedule`, размерные сегменты
  `app.jsonl → app.1.jsonl → …` и почасовые директории.
- `LogFrame`/`LogDaemonConfig` — фиксированный кадр и конфигурация
  центрального демона `nexigen-logd`.
- `logging_settings` — загрузка global/local JSON и `NEXIGEN_LOG_*`
  окружения с best-effort диагностикой.

**Зависимости.** `core`, `chrono`; `Iceoryx2Logger` доступен только через
интерфейсную цель `nexigen_logging_com`.

**Capabilities (14):** `composite-logger`, `console-logger`, `file-logger`,
`format-api`, `iceoryx2-logger`, `log-daemon`, `logger-concept`,
`logger-options`, `logging-settings`, `log-level`, `log-record`,
`multi-producer`, `null-logger`, `spam-protection`.

### 4.5 `metrics` — метрики

**Ответственность.** Инструменты измерения, фоновый сбор и доставка
снимков в sinks без выделения памяти и блокировок на пути записи.

**Ключевые типы.**

- `Counter`, `Gauge`, `StateGauge`.
- `ExplicitHistogram`, `ExponentialHistogram`, `DescriptiveStatistics`,
  снимки `HistogramSnapshot`, `StatisticsSnapshot`.
- `Meter` — фабрика инструментов и владелец идентичности метрики.
- `MetricProvider` — реестр, фабричный корень и фоновый harvester: один
  выровненный блок памяти на арены, таблицы слотов и буфер сбора; поток
  применяет `ThreadSpec` до первой партии.
- Sinks: `ConsoleMetricSink`, `JsonFileMetricSink`, `Iceoryx2MetricSink`,
  `NullMetricSink`, `CompositeMetricSink`; тип-стёртая привязка через
  указатели на функции, без виртуальности и RTTI.
- `MetricFrame` — фиксированный кросс-процессный кадр; `MetricDaemonConfig`
  и `metrics_settings` — конфигурация демона и загрузчик настроек.
- Cold-path рендеринг Prometheus и запись JSONL-истории.

**Зависимости.** `core`, `chrono`; iceoryx2-sink — через
`nexigen_metrics_com`.

**Capabilities (14):** `counter-gauge`, `descriptive-statistics`,
`examples`, `explicit-histogram`, `exponential-histogram`,
`iceoryx2-metric-sink`, `metric-concepts`, `metric-provider`,
`metrics-daemon`, `metric-sinks`, `metrics-settings`, `policy-enums`,
`snapshot-format`, `state-gauge`.

### 4.6 `tracing` — трассировка

**Ответственность.** RAII-спаны, распространение контекста между потоками и
процессами, доставка записей в sinks и аварийные снимки trace-демона.

**Ключевые типы.**

- `TracerProvider<ShardCapacity = 1024, MaxThreads = 32>` — владеет всем
  объёмом памяти трассировки: per-thread SPSC-shards, отдельный `StampSource`
  на слот, счётчики потерь и пул tracer'ов; объект порядка 10 MiB, поэтому
  живёт в статической памяти или на куче.
- `Tracer`, `ScopedSpan` — старт/завершение спана; `SpanContext`, `TraceId`,
  `SpanId`, `TraceHeader` — распространение контекста.
- `SpanRecord`, `SpanFrame`, `SpanKind`, `SpanStatus`, `SamplingDecision`.
- Sinks: `ConsoleTraceSink`, `JsonFileTraceSink`, `ChromeTraceSink`
  (Perfetto-совместимый), `Iceoryx2TraceSink`, `NullTraceSink`,
  `CompositeTraceSink`; `RotationPolicy` управляет ротацией файлов.
- `TraceDaemonConfig` и `trace_daemon_session` — RAM-ring последних секунд и
  версионированный снимок по аварийному событию PHM.

**Зависимости.** `core`, `chrono`; iceoryx2-sink — через
`nexigen_tracing_com`.

**Capabilities (12):** `examples`, `iceoryx2-trace-sink`, `record-format`,
`scoped-span`, `span-context`, `span-enums`, `trace-daemon`,
`trace-propagation`, `tracer-concepts`, `tracer-provider`, `trace-sinks`,
`tracing-settings`.

### 4.7 `diagnostics` — измерения интервалов

**Ответственность.** Дешёвое измерение длительности операций и бюджетов
реального времени.

**Ключевые типы.** `Stopwatch` — один cache line, trivially copyable, без
атомиков, блокировок и кучи; источник штампа — `Timestamp::now()` или
внедрённый `StampSource`. `ScopedStopwatch` — RAII-гард с `OutRefSink` для
записи результата. `StopwatchState`, `LapSnapshot` — состояние и снимки.

**Зависимости.** `core`, `chrono`.

**Capabilities (4):** `examples`, `scoped-stopwatch`, `stopwatch`,
`stopwatch-state`.

### 4.8 `app` — прикладной слой

**Ответственность.** Запуск приложения: разбор командной строки, слияние
настроек, телеметрическая раскладка и владение подсистемами телеметрии.

**Ключевые типы.**

- `parse_command_line` (`command_line`), `CommandLineOptions`,
  `CommandLineError`, `CommandLineDiagnostic` — холодный парсер аргументов
  (`-h/--help` короткозамыкает разбор).
- `app::Settings` (`app_settings`), `AppSettingsPaths` — слияние
  global/local JSON.
- `RunIdentity`, `telemetry_layout` (load/validate/resolve), `TelemetryPaths` —
  идентичность запуска из `NEXIGEN_*` окружения и пути трёх JSONL-потоков
  (metrics, traces, chrono).
- `Infrastructure<Policy>` — heap-only сервис, создаваемый только через
  `make_infrastructure<Policy>()`. Владеет логгером, метриками,
  трассировкой и chrono в безопасном порядке, монопольно владеет
  обработкой `SIGINT`/`SIGTERM` через `signalfd`, выполняет
  `run(tick)`, фиксирует причину остановки и детерминированно освобождает
  ресурсы в обратном порядке. Второй инициализированный экземпляр получает
  `ErrorCode::Conflict`.

**Зависимости.** `core`, `chrono`, `logging`, `metrics`, `tracing`.

**Capabilities (4):** `application`, `command-line`, `settings`,
`telemetry-layout`.

### 4.9 `exec` — исполнение и супервизия

**Ответственность.** Управление жизненным циклом suite и приложений:
манифесты, запуск, readiness, остановка, рестарты, Function Groups и
артефакты запуска.

**Ключевые типы.**

- `ExecutionManager` — stateless-контроль одного instance через его
  директорию: `start` создаёт layout, генерирует настройки, делает
  `fork`/`setsid`/`execvp` и пишет `resource.json`; `stop`/`status`
  работают только с диском.
- `SuiteManifest`, `AppSpec`, `FunctionGroupConfig` — suite-манифест,
  приложения, Function Groups и `initial_state`.
- `start_suite`/`stop_suite` (`suite_control`), `ChildProcess`,
  `ExecutionStateTracker` — detached-супервизор, состояние каждого ребёнка и
  подтверждение готовности.
- Модель процессов: `ProcessType` (`daemon`/`oneshot`), `ProcessState`,
  `AppState`, `ApplicationStateReporter`, `SuiteState`, `SuiteStatus`,
  `UnrecoverableState`.
- Раскладка и артефакты: `suite_paths`, `instance_layout`,
  `instance_settings`, `instance_file_io`, `SuiteMetadata`,
  `SuiteAppMetadata`, `SuiteExit`, `ResourceMetadata`, `ExitMetadata`,
  `run`-лог.
- `ManifestAccessor` — определение JSON/ARXML и единый путь
  `from_json` + `validate_suite_manifest`; `MachineManifest` —
  кросс-проверка ссылок на процессы машины (`nexigen-exec --machine`).
- `ControlHost` — COM-свободная граница супервизора; конкретная
  COM-реализация живёт в `nexigen_exec_control`.
- `StateClient` — приёмная сторона `SetState`; `FunctionGroupRegistry`,
  `StateCommand`/`StateAck`/`StateResult`, `state_dependent_startup_config`,
  `request_id` и access control по `function_group_port_mapping`.
- Platform controls: `ResourceGroupConfig` (RLIMIT_AS, cgroup v2
  CPU/memory, CPU set, working folder), `cgroup_budget`,
  `sha256_file`/`executable_digest`, `SettingsDigest`,
  `TrustedPlatformConfig`, `uid`/`gid` и сброс `CAP_SYS_NICE`.
- `DeterministicClient` — фиксированный такт, seeded splitmix64 и работа
  без выделений для одного execution-потока.
- `root_function_group`/`root_state_query` — словарь имён и состояний RootFG
  и payload результата начального перехода.

**Границы.** `nexigen_exec` не содержит COM-символов и собирается при
выключенном gate. `state_client.cpp`, `control_host_com.cpp` и
`recovery_handler.cpp` вынесены в отдельные цели, подключаемые только к
инструментам при `NEXIGEN_ENABLE_COM=ON`.

**Зависимости.** `core`, `app`; COM-части — `com`.

**Capabilities (18):** `application-state`, `app-suite`,
`deterministic-execution`, `function-group-state`, `instance-lifecycle`,
`logging`, `machine-manifest`, `manifest-accessor`,
`process-lifecycle-events`, `process-supervision`, `process-types`,
`resource-groups`, `root-function-group`, `state-client`,
`state-client-access-control`, `state-request-ids`, `trusted-executable`,
`update-state`.

### 4.10 `hub` — наблюдаемость

**Ответственность.** Read-only обзор завершённых и текущих запусков:
обнаружение директорий, потоковая доставка телеметрии и HTTP/SSE API.

**Ключевые типы.**

- `Discovery` — явные run-root'ы, канонизация путей, inotify и
  согласование дерева `run → suite → app → instance/attempt`.
- `StreamManager` — ограниченные tailer'ы, replay-ring по числу записей и
  байтам, `GapMarker` при переполнении, подписчики.
- `HttpServer` — один epoll-цикл (listener, eventfd, inotify, клиенты,
  таймеры, polling потоков), HTTP/1.1 + SSE, встроенный web UI, только
  `GET`; лимиты запроса и клиентов, `400`/`404`/`405`.
- DTO: `HubTree`, `HubRun`, `HubSuite`, `HubApp`, `HubInstance`,
  `HubAttempt`, `HubStream`, `StreamRecord`, `StreamKind`.

**Зависимости.** `core`, `exec` (типы раскладки).

**Capabilities (4):** `discovery`, `http-api`, `streaming`, `web-ui`.

### 4.11 `phm` — здоровье и восстановление (gate)

**Ответственность.** Замкнутый контур «обнаружение → реакция»: супервизия
процессов, проверка identity, запись отказов, watchdog и восстановление
через EM.

**Ключевые типы.**

- `PhmManifest` — декларативное описание наблюдаемых сущностей.
- `Supervisor` — по одной headless-машине на процесс: `Alive` (счётчики за
  окно и допуски), `Logical` (граф разрешённых переходов и fork/join),
  `Deadline` (пары source→target), толерантность `Ok → Failed → Expired`.
  Все горячие пути O(1) по размеру манифеста, без выделений и блокировок.
- `CheckpointEmitter`, `HealthChannel`, `SupervisedEntity` — клиентская
  сторона: checkpoints, отчёты о здоровье, чтение состояния Function Group,
  периодическая публикация claim.
- `ClaimPublisher`, `ClaimRegistry`, `ClaimVerdict`,
  `SecurityEvent` — проверка `{name, pid, uid}` через `/proc` и события
  безопасности при чужом или неизвестном claim.
- `RecoveryClient`, `RecoveryRequest`, `RecoveryResponse`, `RecoveryAction` —
  `RestartProcess` (через EM, без расхода retry-бюджета) и локальный
  `TriggerWatchdogReset`.
- `FaultStorage`, `FaultKind`, `verify_fault_log`/`FaultLogReport` —
  неблокирующая запись отказов с монотонным номером и CRC-32C; проверка
  целостности журнала.
- `Watchdog`, `WatchdogDevice`, `PosixWatchdogDevice`, `WatchdogState`,
  `WatchdogConfig` — `/dev/watchdog`, период кормления не более половины
  таймаута, отключён по умолчанию, magic close при остановке.
- `LogicalFork`, `EmLifecycleState`, `SupervisionStatus` — логические
  fork/join-регионы и приостановка супервизии на время штатной остановки.

**Зависимости.** `com`; словари `sm` — только на уровне заголовков.

**Capabilities (7):** `health-channel`, `identity`, `manifest`, `recovery`,
`supervised-entity`, `supervision`, `watchdog`.

### 4.12 `sm` — управление состояниями (gate)

**Ответственность.** Центральный арбитр Function Groups: проверка переходов,
приоритеты запросов, публикация состояний, аудит, RootFG, лёгкие
декларативные машины и координация обновлений UCM.

**Ключевые типы.**

- `SmManifest`, `StateManager` — по одной headless-машине на группу,
  проверка перехода по графу и арбитраж по приоритету текущего держателя;
  startup/shutdown — явные шаги, а не рёбра графа. Прикладная группа не
  покидает off-like состояние, пока `RootFG` не достиг `Run`.
- `GroupStatePayload`, `StatePublication` — публикация состояния как
  `Field`; `TriggerRequest`/`TriggerResponse` — входящие запросы;
  `TransitionPreparation` — handshake подготовки.
- `TransitionResult`, `TransitionRecord`, `TransitionAudit` — результат,
  запись и неблокирующий журнал аудита; `SmMetrics` — метрики арбитража.
- `LightweightStateMachine`, `LsmRuntime` — декларативные таблицы
  переходов, действий и recovery для группы.
- `RootFG` (enum), `RootLifecycleClient` — словарь `Off`/`Startup`/`Verify`/
  `Run`/`Shutdown`/`Restart` и клиент начального перехода EM.
- `UpdateRequest`/`UpdateResponse`, `UpdateCoordinator` — `Start`/`Complete`/
  `Cancel` обновления: блокировка группы, состояние `Update`, отказ
  `RejectedByUpdate`, единственный активный update.
- `RecoveryHandler`, `RecoveryNotification` — обработка восстановления от
  PHM.

**Зависимости.** `com`.

**Capabilities (9):** `ipc`, `lifecycle`, `lightweight-state-machine`,
`lsm-runtime`, `manifest`, `recovery-handler`, `state-machine`,
`telemetry`, `ucm-coordination`.

### 4.13 `recorder` — сырые сессии и конвертер

`nexigen_recorder` — self-contained хранилище сессий: `session.json` плюс
триплеты `.nxrec` (append-only записи), `.nxidx` (разреженный индекс) и
`.nxvid` (кадры с выравниванием 4 KiB). Модуль не зависит от COM, media,
MCAP или FFmpeg; всё, что нужно для чтения, лежит внутри сессии.

- **Горячий путь.** Один writer владеет порядком и индексом; все буферы
  выделены в `create()`, `append()` не аллоцирует и не блокируется.
  Производители передают записи через SPSC `RecordShard`, переполнение
  превращается в `GapMarker` (`IoBackpressure`).
- **Формат.** Каждая запись покрыта CRC-32C; метаданные — `RecordHeader`
  (48 B), `VideoFrameRef` (64 B), `VideoStreamConfig` (1056 B),
  `SegmentFooter` (64 B). Видео хранится как непрозрачные байты с
  необязательным per-frame CRC; запись не декодирует и не перекодирует
  битстрим.
- **Ротация и восстановление.** Сегмент закрывается по байтам (`.nxrec` +
  `.nxvid`), длительности или числу записей; `circular` удаляет только
  закрытые триплеты, `stop` финализирует и останавливает приём,
  `min_free_bytes` не даёт открыть новый сегмент. Повреждённый хвост
  читается как префикс, отсутствующий `.nxidx` перестраивается.
- **Чтение и конвертация.** `SessionReader` строит инвентарь датасетов,
  фильтрует по каналу и event-time, проверяет CRC и читает видео по
  `VideoFrameRef`. `nexigen-export` — on-demand CLI: `inspect`, `list`,
  `info`, `extract`, `verify`; выходы JSONL, Perfetto, MCAP (опционально)
  и elementary video. MP4/MKV отложены до `nexigen_media`.

**Зависимости.** `core`, `chrono`; MCAP — только в `nexigen-export`.

**Capabilities (3):** `recorder/raw-storage`, `recorder/video-storage`,
`recorder/export`.

Подробности формата и CLI: [recorder.md](recorder.md), раздел 10.

## 5. Взаимодействие компонентов

### 5.1 Карта процессов и каналов

```mermaid
flowchart TB
    subgraph PROC["Процессы приложений"]
        APP["Application<br/>app::Infrastructure::run(tick)"]
        ENT["phm::SupervisedEntity<br/>checkpoints · claims · health"]
        APP --- ENT
    end

    subgraph PLANE["Плоскость управления"]
        EM["nexigen-exec / nexigen-em<br/>detached suite supervisor + ControlHost"]
        SM["nexigen-sm<br/>StateManager"]
        PHM["nexigen-phmd<br/>Supervisor + Watchdog"]
        WD["/dev/watchdog"]
    end

    subgraph OBS["Плоскость наблюдаемости"]
        SINKS["sinks: console · JSONL · iceoryx2"]
        BUS(("iceoryx2"))
        LOGD["nexigen-logd"]
        MD["nexigen-metricsd"]
        TD["nexigen-traced"]
        DISK[("run directory<br/>JSONL · suite.json · fault log")]
        HUB["nexigen-hub"]
        UI["HTTP/SSE · Web UI · Prometheus"]
    end

    EM -->|"fork/exec · setsid<br/>SIGTERM → SIGKILL"| APP
    SM -->|"SetState"| EM
    EM -->|"StateAck · readiness"| SM
    SM -->|"state fields"| APP
    APP -->|"checkpoints · health · claims"| PHM
    PHM -->|"RestartProcess"| EM
    PHM -->|"SafeState"| SM
    PHM --- WD

    APP --> SINKS
    SINKS -->|"JSONL"| DISK
    SINKS -->|"fixed frames"| BUS
    BUS --> LOGD
    BUS --> MD
    BUS --> TD
    LOGD --> DISK
    MD --> DISK
    TD -.->|"dump on PHM event"| DISK
    EM -->|"suite metadata"| DISK
    DISK --> HUB
    HUB --> UI
    MD -->|"Prometheus /metrics"| UI
```

Ключевые потоки:

- **EM → приложение.** `nexigen-exec`/`nexigen-em` поднимают detached
  супервизор, который запускает каждое приложение в новой сессии и группе
  процессов и применяет остановку по таймауту (`SIGTERM` → `SIGKILL`).
- **SM → EM → приложение.** `nexigen-sm` отправляет `SetState` в
  `StateClient`; EM разрешает состояние в набор процессов через
  `FunctionGroupRegistry` и выполняет stop/restart/start через
  `ChildProcess`.
- **Приложение → EM.** Readiness каждого процесса и результаты переходов
  собирает трекер готовности EM через state-каналы
  (`Nexigen/EM/State/*`); SM получает итог перехода.
- **Приложение → PHM.** Checkpoints, отчёты о здоровье и identity claims
  публикуются в `Nexigen/PHM/*`; PHM принимает checkpoints только после
  проверки claim.
- **PHM → EM/SM.** При `Expired` PHM выполняет настроенное правило
  восстановления: `RestartProcess` через `Nexigen/PHM/Recovery` или
  SafeState-команду в SM; watchdog останавливает кормление, если есть
  просроченные сущности.
- **Телеметрия → диск → hub.** Sinks пишут JSONL в run-директорию или
  отправляют кадры в iceoryx2; демоны агрегируют их; `nexigen-hub` читает
  файлы и отдаёт клиентам.

### 5.2 Телеметрический конвейер

```mermaid
flowchart LR
    CALL["Hot path<br/>log · span · metric"] --> SHARD["Per-thread SPSC shard"]
    SHARD --> WORKER["Cold worker / harvester<br/>deferred formatting · serialization"]
    WORKER --> CONSOLE["Console sink"]
    WORKER --> JSONL["JSONL file sink"]
    WORKER --> FRAME["Iceoryx2 sink<br/>fixed frame"]

    FRAME --> LOGD["nexigen-logd"]
    FRAME --> MD["nexigen-metricsd"]
    FRAME --> TD["nexigen-traced"]

    LOGD --> FILES[("hourly JSONL")]
    MD --> HIST[("quota JSONL history")]
    MD --> PROM["Prometheus /metrics"]
    TD --> RING[("RAM ring → dump on PHM event")]

    FILES --> HUB["nexigen-hub"]
    HIST --> HUB
    RING -.-> HUB
    HUB --> UI["HTTP/SSE · Web UI"]
```

Инварианты конвейера:

- производитель не выделяет память, не форматирует и не блокируется;
- при переполнении ring'а публикуется `GapMarker` с диапазоном потерянных
  последовательностей — потеря видима, а не маскируется;
- каждый модуль телеметрии имеет собственный shard и не блокирует
  соседние;
- JSON-сериализация, разбор имён и файловый ввод-вывод выполняются только
  на холодном пути.

### 5.3 Жизненный цикл suite и Function Groups

```mermaid
flowchart TD
    MAN["Suite manifest<br/>JSON или ARXML"] --> ACCESSOR["ManifestAccessor<br/>определение формата · разрешение ссылок"]
    ACCESSOR --> VALID["SuiteManifest<br/>from_json + validate"]
    VALID --> PRE["preflight потоков<br/>CPU-маски · приоритеты"]
    PRE --> SUP["Detached suite supervisor"]
    SUP --> HOST["ControlHost<br/>COM-граница"]
    HOST --> SC["StateClient<br/>+ readiness tracker"]
    SUP --> CHILD["ChildProcess на приложение<br/>setsid · fork · exec"]
    CHILD --> STATE["suite-state.json · suite.json<br/>suite-exit.json · run-лог"]
    STATE --> HUB["hub::Discovery"]
    SC --> FG["FunctionGroupRegistry<br/>state → процессы"]
    FG --> CHILD
    SM["nexigen-sm"] -->|"SetState"| SC
    PHM["nexigen-phmd"] -->|"recovery"| SC
```

Порядок выполнения:

1. `nexigen-exec start --config` или `nexigen-em --config` загружают
   манифест, проверяют его и при необходимости сверяют с Machine Manifest.
2. Запускается detached-супервизор; при COM-сборке он устанавливает
   `ControlHost` и создаёт `StateClient` с трекером готовности.
3. Супервизор входит в `initial_state` через тот же движок переходов, что
   и любой `SetState`, а не прямым циклом запуска.
4. `daemon`-процессы обязаны сообщить `Run`, `oneshot` — завершиться с
   кодом 0; только после этого переход подтверждается.
5. `stop` останавливает приложения с grace-периодом, затем супервизор
   фиксирует `suite-exit.json` и причину остановки.

### 5.4 Последовательность `SetState`

```mermaid
sequenceDiagram
    autonumber
    participant SM as nexigen-sm (StateManager)
    participant EM as Suite supervisor (EM)
    participant REG as FunctionGroupRegistry
    participant APP as Application
    participant PHM as nexigen-phmd

    SM->>EM: SetState(group, state, requester_id, request_id)
    EM->>EM: access policy + request id arbitration
    EM->>REG: state → набор процессов
    REG-->>EM: процессы + startup config
    EM->>APP: stop / restart / start (ChildProcess)
    APP-->>EM: readiness (Run или exit 0)
    EM-->>SM: StateAck / StateResult
    SM->>SM: publish State field + audit record

    APP->>PHM: checkpoints · health · claims
    PHM->>EM: RestartProcess (recovery)
    PHM->>SM: SafeState
```

### 5.5 RootFG

`RootFG` — зарезервированная корневая Function Group: брокер сообщений, SM,
PHM, EM и будущий DataRecorder. Манифест может объявить её с фиксированным
набором состояний; `Off` не имеет привязанных процессов, `Run` — целевое
состояние, `Restart` — быстрый перезапуск без прохода через `Off`.

```mermaid
stateDiagram-v2
    [*] --> Off
    Off --> Startup: начальный переход EM
    Startup --> Verify: SM SetState(Verify)
    Verify --> Run: SM SetState(Run)
    Run --> Shutdown: SM SetState(Shutdown)
    Shutdown --> Off: путь остановки EM
    Run --> Restart: рестарт SM
    Restart --> Startup: fast reboot

    note right of Off
        Владелец — EM.
        Внешний SetState(Off) отклоняется.
    end note
    note right of Run
        Целевое состояние.
        Прикладные группы могут стартовать.
    end note
    note right of Restart
        Быстрый перезапуск.
        Состояние Off пропускается.
    end note
```

Начальный переход `Off → Startup` выполняет EM через общий движок
переходов и публикует результат в
`Nexigen/EM/InitialRootStateTransitionResult`. SM читает результат, затем
командует `Verify` и `Run`. Пока RootFG не в `Run`, прикладные группы не
покидают off-like состояния. EM также публикует
`Running`/`Terminating`/`Terminated` в `Nexigen/PHM/ExecutionState`, чтобы
PHM приостанавливал супервизию на время штатной остановки.

### 5.6 Запись и конвертация raw-сессии

```mermaid
flowchart LR
    subgraph RT["Процесс приложения / encoder"]
        PROD["producer threads<br/>и encoder'ы"]
        SHARD["RecordShard<br/>SPSC на производителя"]
        W["SessionWriter<br/>единственный writer"]
        PROD --> SHARD
        SHARD --> W
    end

    W --> REC[(".nxrec<br/>записи + декларации")]
    W --> IDX[(".nxidx<br/>разреженный индекс")]
    W --> VID[(".nxvid<br/>кадры 4 KiB")]
    W --> MAN[("session.json<br/>манифест")]

    REC --> READER["SessionReader<br/>инвентарь · фильтры · verify"]
    IDX --> READER
    VID --> READER
    MAN --> READER

    READER --> EXP["nexigen-export<br/>один запуск, без daemon/follow"]
    EXP --> JSONL["JSONL"]
    EXP --> PERF["Perfetto"]
    EXP --> MCAP["MCAP<br/>(опционально)"]
    EXP --> ELEM["elementary video<br/>+ timestamps sidecar"]
```

- **Запись.** Несколько producer-потоков пишут каждый в свой `RecordShard`;
  только `SessionWriter` владеет порядком, индексом, ротацией и retention.
  При переполнении shard'а теряется диапазон, но появляется `GapMarker`
  (`IoBackpressure`), поэтому потеря наблюдаема.
- **Файлы.** Writer добавляет декларации схем/каналов до первых записей,
  считает CRC-32C, пишет разреженный `.nxidx`, выравнивает кадры в
  `.nxvid` и периодически атомарно заменяет `session.json`.
- **Чтение.** `SessionReader` открывает либо каталог сессии, либо один
  `.nxrec`; отсутствующий индекс перестраивается, повреждённый хвост
  читается как префикс, `verify` проверяет записи, footer'ы и CRC кадров.
- **Конвертация.** `nexigen-export` — on-demand процесс: он не является
  демоном, не поддерживает `--follow` и завершается после записи выхода.
  MCAP включается отдельной опцией сборки; MP4/MKV отложены до
  `nexigen_media`.

## 6. Инструменты командной строки

| Инструмент | Сборка | Роль |
|---|---|---|
| `nexigen-exec` | всегда | CLI жизненного цикла suite и instance: `start`, `stop`, `status`; при COM — с `ControlHost` |
| `nexigen-hub` | всегда | HTTP/SSE-обсерватория над run-root'ами, встроенный web UI |
| `nexigen-export` | всегда (MCAP опционально) | On-demand конвертер raw-сессий: `inspect`, `list`, `info`, `extract`, `verify`; JSONL, Perfetto, MCAP, elementary video |
| `nexigen-em` | COM | фронтенд Execution Manager: запускает тот же detached-супервизор, что и `nexigen-exec` |
| `nexigen-sm` | COM | State Manager: арбитраж, публикация состояний, TriggerIn, UCM, аудит |
| `nexigen-phmd` | COM | супервизия, recovery, faults, watchdog; `--verify-fault-log` |
| `nexigen-logd` | COM | приём `LogFrame`, форматирование и почасовая JSONL-запись |
| `nexigen-metricsd` | COM | агрегация метрик, Prometheus HTTP, квотируемая JSONL-история |
| `nexigen-traced` | COM | RAM-ring спанов и аварийный снимок по событию PHM |

Инструменты при `NEXIGEN_BUILD_TOOLS=ON` собираются вместе с библиотекой;
COM-зависимые инструменты появляются только при включённом gate.

## 7. Сквозные контракты

### Ошибки и сериализация

- Публичные fallible-функции возвращают `Result`/`Status`; исключения и
  RTTI запрещены глобально (кроме cold-path исключения в `com` для
  iceoryx2).
- Каждый публичный тип имеет холодный `to_string()` (член и свободную
  функцию), `to_json`/`from_json` и `std::formatter`; перечисления —
  `constexpr to_string`/`from_string`.
- Тела сериализации живут в скомпилированных единицах `src/*/*_format.cpp`,
  чтобы не раздувать TU потребителей.

### Горячий и холодный путь

| | Горячий путь | Холодный путь |
|---|---|---|
| Память | без выделений | `std::string`, `nlohmann::json`, файлы |
| Синхронизация | wait-free atomics, SPSC | мьютексы control plane |
| Ошибки | предпроверенные `Status` | `Result` с диагностикой |
| Бюджет | наносекунды, latency-гейты | не ограничен |

### Потоки и affinity

`CpuSet` адресует до 1024 CPU; `ThreadSpec` задаёт имя, политику и
приоритет, а также режим применения — required (ошибка откатывает
инициализацию) или best-effort (ошибка видна в `ThreadApplyReport`). Каждый
фоновый worker применяет копию спецификации к себе до старта sink'а.

### Идентичность запуска и телеметрия

`app::load_run_identity` читает `NEXIGEN_RUN_ID`, `NEXIGEN_EXEC_DIR`,
`NEXIGEN_INSTANCE_ID`, `NEXIGEN_INSTANCE_DIR`; `resolve_telemetry_paths`
отображает их в пути metrics/traces/chrono. Контракт не зависит от языка и
используется реализациями на C#, Python и C++.

### Безопасность и целостность

- Дайджесты SHA-256 для манифестов, исполняемых файлов и файлов настроек;
  проверка до `fork`/запуска, ошибка — `SecurityError`.
- `trusted_platform` требует абсолютных команд; `require_non_root` и
  `uid`/`gid` выполняют сброс привилегий до `exec`; `CAP_SYS_NICE`
  сбрасывается, если не разрешён явно.
- Identity claims (`{name, pid, uid}`) проверяются через `/proc`; чужой
  claim порождает `SecurityEvent`.
- Access control для `SetState` по парам `(requester, Function Group)`;
  `request_id` отменяет устаревшие переходы.
- E2E-профили защищают обмен от искажения, повторов, потерь, переупорядочения
  и ошибочной доставки.
- `TimeSyncMonitor` обнаруживает шаг, обратный ход и превышение скорости
  системных часов.
- Fault-журнал PHM содержит монотонный номер и CRC-32C; режим
  `--verify-fault-log` различает целые, повреждённые, усечённые и legacy
  записи.

## 8. Спецификации и трассируемость

Спецификации лежат в `openspec/specs/<module>/<capability>/spec.md`; путь
повторяет структуру `include/nexigen/`. Каждая capability содержит
требования (`Requirement`) и проверяемые сценарии (`Scenario`).

- Всего: 126 capability, 906 требований, 2317 сценариев.
- Межмодульные возможности: `packaging`, `examples`, `quality/*`.
- Проверка корпуса: `openspec validate --specs`.
- Покрытие требований AUTOSAR `RS_Safety` отслеживается в
  [`safety/traceability.md`](safety/traceability.md) и проверяется
  `cmake/check_safety_traceability.py`.

Если код и спецификация расходятся, побеждает спецификация: код
исправляется либо открывается change, который меняет спецификацию. Тихая
рассинхронизация не допускается.

## 9. Сборка, тесты, установка

```bash
cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug -j 64
ctest --test-dir build/debug --output-on-failure
```

Основные опции:

- `NEXIGEN_BUILD_TESTS`, `NEXIGEN_BUILD_EXAMPLES`, `NEXIGEN_BUILD_TOOLS` —
  по умолчанию `ON`;
- `NEXIGEN_ENABLE_COM` — по умолчанию `OFF`, требует Rust-тулчейн и
  собирает `com`, `phm`, `sm` и COM-инструменты;
- `NEXIGEN_ENABLE_MCAP` — по умолчанию `OFF`; добавляет MCAP-выход только
  в `nexigen-export` и не попадает в `nexigen_recorder`;
- `NEXIGEN_BUILD_STORAGE_BENCHMARKS` — по умолчанию `OFF`; собирает
  opt-in бенчмарки форматов хранения (MCAP против `.nxrec`);
- `NEXIGEN_ENABLE_ASAN`, `NEXIGEN_ENABLE_TSAN` — взаимоисключающие
  санитайзеры; coverage с ними несовместим;
- `NEXIGEN_ENABLE_COVERAGE` — цель `coverage` и порог покрытия;
- `NEXIGEN_NATIVE_ARCH` — `-march=native`/`-mtune=native`;
- `NEXIGEN_STRICT_LATENCY_TESTS` — наносекундные latency-гейты в
  Release/RelWithDebInfo;
- `NEXIGEN_ENABLE_FAULT_INJECTION`, `NEXIGEN_BUILD_FUZZERS` — расширенная
  проверка холодных путей и HTTP-парсера.

Установка и потребление как CMake-пакета:

```bash
cmake --install build/release --prefix /opt/nexigen
find_package(nexigen CONFIG REQUIRED)
target_link_libraries(app PRIVATE nexigen::app nexigen::exec)
```

Пакет экспортирует `nexigen::core`, `chrono`, `logging`, `metrics`,
`tracing`, `diagnostics`, `app`, `exec`, `hub`, `recorder`, а при включённом gate —
`com`, `phm`, `sm`, и сам разрешает `Threads` и `nlohmann_json` через
`find_dependency`.

## 10. Recorder: формат и конвертация

`nexigen::recorder` — self-contained raw session storage (`.nxrec/.nxidx/.nxvid`
+ `session.json`), COM-free and media-free. `nexigen-export` is the on-demand
converter; MCAP/FFmpeg are converter-only dependencies. Модульный обзор — в
разделе 4.13.

```mermaid
flowchart LR
    P[producers / encoders] --> W[nexigen_recorder single writer]
    W --> R[.nxrec records + schemas + CRC-32C]
    W --> I[.nxidx sparse index]
    W --> V[.nxvid aligned frames]
    W --> M[session.json]
    R --> C[nexigen-export on demand]
    I --> C
    V --> C
    M --> C
    C --> J[JSONL]
    C --> PF[Perfetto]
    C --> MC[MCAP optional]
    C --> E[elementary video]
```

Подробности: [recorder.md](recorder.md).

## 11. Источники

- Публичные заголовки: `include/nexigen/`.
- Контракты: `openspec/specs/`.
- Цели и зависимости сборки: `CMakeLists.txt`, `tools/CMakeLists.txt`.
- [Руководство по метрикам](metrics-guide.md)
- [Управление потоками и CPU](thread-management.md)
- [Trace Daemon](trace-daemon.md)
- [nexigen-hub](nexigen-hub.md)
- [Трассировка требований безопасности](safety/traceability.md)
