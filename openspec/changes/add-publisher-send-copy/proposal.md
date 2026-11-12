# Proposal

## Why

Горячий путь публикации кадров тратит основное время не на наш маппинг, а на C++-обёртку над iceoryx2. Замеры на одной Release-сборке: чистый транспорт `loan_uninit` → `write_payload` → `send` стоит **187.6 нс** для 256-байтного кадра, а полный `Iceoryx2TraceSink::on_span` — **210.3–220.6 нс**; разница (~25–33 нс) — это маппинг, хэши и `Timestamp::now()`. Дизассемблер `on_span` показывает до **восьми** FFI-переходов `iox2_sample_mut_move` (по 256 Б каждый), две полные копии 384-байтного кадра и до шести `iox2_sample_mut_drop` на один спан.

У iceoryx2 уже есть операция, делающая ровно то, что нужно синкам: `Publisher::send_copy(const Payload&)` — один FFI-вызов, внутри которого транспорт сам берёт слот, копирует payload в разделяемую память и отправляет. Мы не используем её только потому, что `nexigen::com::Publisher` её не пробрасывает. Проброс убирает обёрточную возню без изменения wire-формата, семантики доставки и бюджета 400 нс.

## What Changes

- Новый метод `nexigen::com::Publisher<T, H>::send_copy(const T&)`: один вызов `iox2::Publisher::send_copy`, возвращает `core::Status`. Телеметрия повторяет `send()` (`send_count`, `lost_samples`, `peak_recipients`), поэтому снимки `lost_samples()` до/после публикации продолжают работать, а `to_string()` показывает те же счётчики.
- Три hot-path синка переводятся на `send_copy`: `tracing::Iceoryx2TraceSink::on_span`, `logging::detail::Iceoryx2Sink::handle`, `metrics::Iceoryx2MetricSink::on_batch`. Семантика сохраняется: кадр формируется в scratch-буфере, `sequence` инкрементится до попытки, ошибка loan/send одинаково трактуется как `++dropped_` с успешным `Status`.
- `loan_uninit()` / `write_payload()` / `send()` остаются без изменений для управляющих портов (PHM, SM, request-response), где важна запись в слот до/после дополнительных проверок.
- Транспортный бенчмарк `tests/com/latency_test.cpp` получает ветку `(a2) send_copy`, чтобы сравнивать оба пути на одном стенде; latency-гейт трассировки остаётся тем же (≤400 нс) и продолжает измерять полный `on_span`.
- **BREAKING**: нет. Wire-форматы `SpanFrame`/`LogFrame`/`MetricFrame` не меняются, конфигурация не меняется, существующие вызовы `send()` компилируются и работают как раньше.

**Вне скоупа:** батчинг кадров (`SpanFrame[N]`), изменение `publish_ts_ns`, переход на `ServiceType::Local`, правки upstream iceoryx2.

## Capabilities

### New Capabilities

Нет.

### Modified Capabilities

- `com/publish-subscribe`: добавляется требование к публикации caller-owned payload одним транспортным вызовом (`send_copy`) с сохранением существующей телеметрии потерь и запрета на аллокации. Поведение подписчика, wire-контракт и политика переполнения не меняются.

Поведение `tracing/iceoryx2-trace-sink`, `logging/iceoryx2-logger` и `metrics/iceoryx2-metric-sink` не меняется: они по-прежнему публикуют по одному кадру на событие, не аллоцируют и соблюдают прежние latency-гейты. Это реализационная оптимизация внутри существующих требований.

## Impact

- **Производительность**: с пути публикации уходят до восьми перемещений 256-байтного `iox2_sample_mut_t`, одна лишняя копия 384 Б (параметр `write_payload` по значению) и ~девять пересечений FFI-границы на событие. Ожидаемый `on_span` — **170–190 нс** вместо 210–220 нс; ожидаемое время `on_batch`/`handle` снижается пропорционально. Цифра проверяется замером, а не принимается на веру; Rust-часть транспорта (mempool, `update_connections`, очередь) остаётся и задаёт нижнюю границу ~120–130 нс.
- **Latency-гейты**: `tracing` — медиана `on_span` ≤400 нс (strict Release x86-64) без изменения порога; `logging`/`metrics` — существующие гейты. Замеры выполняются только в strict-конфигурации, в Debug/TSan/ASan печатаются без ассерта.
- **Память и аллокации**: новых аллокаций нет. `send_copy` делает один loan и один `memcpy` в разделяемую память — ровно столько же, сколько текущая пара `write_payload` + `send`. Обёртка не добавляет полей в `Publisher`. Тест отсутствия аллокаций (`arena_used()`/`mallinfo2`) остаётся обязательным.
- **Код**: `include/nexigen/com/publisher.hpp`, `include/nexigen/tracing/iceoryx2_trace_sink.hpp`, `include/nexigen/logging/detail/iceoryx2_sink.hpp`, `include/nexigen/metrics/iceoryx2_metric_sink.hpp`, `tests/com/latency_test.cpp`, `tests/com/com_test.cpp`, `tests/tracing/iceoryx2_trace_sink_test.cpp`.
- **Совместимость и зависимости**: аддитивный метод; новых third-party зависимостей нет, iceoryx2 уже в дереве за `NEXIGEN_ENABLE_COM`.
