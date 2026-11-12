# Proposal

## Why

`nexigen::tracing` умеет собирать спаны только внутри одного процесса: `TracerProvider` дренирует shard-кольца в in-process sinks (console, JSONL, Chrome). Центрального `TraceDaemon` (`initial_spec/tracing/trace_daemon_srs.md`, TRCD_001–007) нет, и появиться он не может, пока спаны не пересекают границу процесса — у демона нет источника данных.

Первый шаг — транспорт: клиентский sink, публикующий спаны в iceoryx2 по той же схеме, что уже работают `Iceoryx2Logger` → `LogDaemon` и `Iceoryx2MetricSink` → `MetricsDaemon`. Это отдельный change, а не сразу демон, по прецеденту `add-iceoryx2-logger` → `add-log-daemon`: транспорт можно вмержить и протестировать независимо, а демон (RAM-ring, Perfetto, ftrace) строить поверх стабильного wire-контракта.

## What Changes

- Новый `SpanFrame` — фиксированный 384-байтный (`alignas(64)`, 6 кэш-линий) `trivially_copyable` wire-тип, повторяющий раскладку `SpanRecord`; два сырых указателя (`file_name`, `function_name`) заменяются стабильными 64-битными FNV-1a хэшами, а идентичность публикатора (`app_id`, `pid`, `sequence`, `publish_ts_ns`) и `version` лежат внутри кадра — как `app_id`/`pid` у `LogFrame` и `MetricFrame`.
- Новый `Iceoryx2TraceSink`, удовлетворяющий `concepts::TraceSink`: `start` / `on_span` / `flush` / `stop`. Публикует один `SpanFrame` на спан, только с worker-потока провайдера, без аллокаций, с `DiscardData` при переполнении и счётчиком отброшенных кадров.
- Memo-кэш source-хэшей в sink: 64 записи `pointer → hash` (1 КБ), worker-confined, без атомиков. Повторные call-site не перехэшируются; hit/miss видны в диагностике. Без кэша FNV-1a по 70–140 байтам source-локаций дороже копии всего кадра.
- Новый `Iceoryx2TraceSinkConfig` (`service_name`, `app_id`, `subscriber_max_buffer_depth`) с обязательным trio `to_string` / `std::formatter` / JSON round-trip.
- Latency-гейт на формирование кадра: медиана `on_span` (маппинг + хэш с учётом промахов + `loan/send`) ≤ **400 нс** в strict Release x86-64, с печатью медианы/p99/p99.9 и с компиляцией гейта только под `NEXIGEN_STRICT_LATENCY_TESTS`.
- Контракт дренажа для будущего демона: sink не отправляет Event на каждый спан; потребитель вычитывает очередь по интервалу (20–50 мс, как в SRS), а `sequence` в кадре позволяет обнаруживать разрывы.
- Новая CMake-цель `nexigen_tracing_com` (INTERFACE, по образцу `nexigen_logging_com` и `nexigen_metrics_com`), собирается только при `NEXIGEN_ENABLE_COM=ON`; ядро `nexigen_tracing` остаётся свободным от iceoryx2.
- **BREAKING**: нет. `SpanRecord` не меняется, in-process sinks не меняются, hot path не затрагивается.

**Вне скоупа:** сам `TraceDaemon`, RAM flight recorder, on-demand сессии и управление категориями, Perfetto `.pftrace`, ftrace, словарь `hash → строка` для source-локаций. Это последующие change'и `add-trace-daemon`, `add-trace-session-control`, `add-perfetto-export`, `add-ftrace-capture`.

## Capabilities

### New Capabilities

- `tracing/iceoryx2-trace-sink`: межпроцессный транспорт спанов — wire-тип `SpanFrame`, клиентский sink поверх iceoryx2 pub-sub, memo-кэш source-хэшей, политика переполнения, latency-бюджет и диагностика.

### Modified Capabilities

Нет: поведение существующих capabilities не меняется. `com/publish-subscribe` уже поддерживает нужные порты, `tracing/trace-sinks` описывает in-process стоки и остаётся как есть.

## Impact

- **Код**: новые заголовки в `include/nexigen/tracing/` (`span_frame.hpp`, `iceoryx2_trace_sink_config.hpp`, `iceoryx2_trace_sink.hpp`, `detail/fnv1a_64.hpp`); сериализация — `src/tracing/`; тесты — `tests/tracing/iceoryx2_trace_sink_test.cpp` (+ latency-блок); CMake-цель `nexigen_tracing_com`.
- **Производительность**: hot path (`ScopedSpan`, `Tracer::start_span`, shard push) не меняется — гейт ≤20 нс остаётся. Формирование кадра (копия 384 Б, memo-хэши, `Timestamp::now()`, `loan/send`) выполняется только на background worker провайдера. Ориентир: измеренный транспорт `loan/send` ~190 нс для 256 Б; с маппингом и кэшем ожидаемая медиана ~215–240 нс на спан, гейт 400 нс. При 10 000 спанов/с это ~3.84 МБ/с и доли процента одного ядра.
- **Память**: `SpanFrame` 384 Б + memo-кэш 1 КБ в объекте sink; после `start()` публикация не аллоцирует. Очередь iceoryx2 ограничена `subscriber_max_buffer_depth`; переполнение не блокирует worker, а инкрементирует счётчик потерь.
- **Совместимость**: аддитивно. `SpanRecord` остаётся 320 Б; отсутствие потребителя (демона) безопасно — кадры перезаписываются по `DiscardData`.
- **Зависимости**: `iceoryx2` уже в дереве за `NEXIGEN_ENABLE_COM`; новых third-party зависимостей нет.
- **Провенанс**: адаптация TRCC_007 (384 Б inline вместо 64 Б + словаря) фиксируется в `design.md`; триггер возврата к lean-формату — устойчивые `>20–50k` спанов/с или бюджет flight-recorder ring `≤32 МБ`.
