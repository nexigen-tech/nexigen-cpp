# Proposal

## Why

Трассировка (`nexigen::tracing`) сейчас связывает спаны только внутри одного процесса: `start_span` наследует `trace_id` от родителя через `active_span_stack`, но границу процесса пересечь не может. `initial_spec` (TRCC_004–006) требует, чтобы каждый iceoryx2-сэмпл сопровождался мета-заголовком `TraceHeader`, несущим сквозной `trace_id`, а клиентская библиотека автоматически пробрасывала контекст между полученным и отправляемым сообщениями — это даёт сквозную трассировку конвейеров (`Camera → Perception → Planner`) и замер очереди IPC (`publish_timestamp_ns`). Нужен generic custom-header в `nexigen::com` и propagation-логика в `nexigen::tracing`.

## What Changes

- `nexigen::com` — `Publisher<T, H = void>`, `Subscriber<T, H = void>`, `Sample<T, H = void>`, `SampleMut<T, H = void>`: второй шаблонный параметр `H` (дефолт `void` — без заголовка). При `H ≠ void` `Sample::user_header()` даёт `const H&`, `SampleMut::user_header_mut()` — `H&`; `Publisher<, H>::create()` добавляет `.user_header<H>()` в цепочку билдера.
- `nexigen::tracing::TraceHeader` (32 Б, trivially copyable, `alignas(8)`): `TraceId trace_id`, `SpanId source_span_id`, `uint64_t publish_timestamp_ns`, `uint32_t sequence_number`.
- `nexigen::tracing::detail::current_trace_context()` — `thread_local`-хранилище `{trace_id, source_span_id}`. Заполняется подписчиком из `sample.user_header()`; читается при `start_span` (fallback перед fresh-root) и при заполнении `TraceHeader` перед `send()`.
- `start_span` — новый fallback: если явного родителя нет и стек пуст → проверить `current_trace_context()` (это пришедший из другого процесса `trace_id`) → только потом fresh root.
- `NEXIGEN_TRACE_NEW_ROOT` — макрос, генерирующий свежий `trace_id` в контексте текущего потока (для источника данных — драйверов, камер, лидаров).
- Последовательный номер (`sequence_number`) — caller сам инкрементирует per-publisher счётчик; `com` остаётся generic и в `TraceHeader` не завязывается.

## Capabilities

### New Capabilities

- `tracing/trace-propagation`: TraceHeader, thread-local TraceContext, NEW_ROOT, и cross-process fallback в `start_span`.

### Modified Capabilities

- `com/publish-subscribe`: Publisher, Subscriber, Sample и SampleMut получают второй шаблонный параметр `H` (дефолт `void`); при `H ≠ void` открывается `user_header()`/`user_header_mut()`.

## Impact

- **Код**: модификация `com/publisher.hpp`, `com/subscriber.hpp`, `com/sample.hpp`, `com/sample_mut.hpp` (шаблонный параметр `H`). Новые `tracing/trace_header.hpp`, `tracing/detail/trace_context.hpp`. Модификация `tracing/tracer.hpp` (`start_span` fallback). Тесты в `tests/com/` и `tests/tracing/`.
- **Производительность**: generic `H` — нулевой оверхед при `H=void` (SFINAE убирает `user_header()` из интерфейса); при `H≠void` — одна дополнительная операция копирования 32 Б перед `send` и чтения после `receive`.
- **API**: обратно совместим — весь существующий код с `Publisher<T>` и `Subscriber<T>` компилируется без изменений (дефолт `void`). TraceHeader и контекст опциональны — приложения, не использующие трассировку, не меняются.
- **Зависимости**: `nexigen::com` остаётся независимым от `nexigen::tracing` (H — generic). `nexigen::tracing` использует `com`-типы с `H=TraceHeader` без обратной зависимости.