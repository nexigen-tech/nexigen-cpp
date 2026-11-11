# Tasks

## 1. com: generic custom header (H = void by default)

- [x] 1.1 Template `Publisher<T, H = void>`: builder добавляет `.user_header<H>()` при `H ≠ void` (if constexpr). Verify: `Publisher<int>` (H=void) компилируется и работает как раньше; `Publisher<int, TestHeader>` компилируется и создаёт сервис с заголовком.
- [x] 1.2 Template `Subscriber<T, H = void>`: builder аналогично. Verify: `Subscriber<int>` обратно совместим; `Subscriber<int, TestHeader>` принимает заголовок.
- [x] 1.3 Template `Sample<T, H = void>` и `SampleMut<T, H = void>`: `user_header()` / `user_header_mut()` через SFINAE (`!std::is_void_v<H>`). Verify: `Sample<int>.user_header()` не компилируется; `Sample<int, TestHeader>.user_header()` компилируется и возвращает `const TestHeader&`.
- [x] 1.4 Template `SampleMutUninit<T, H = void>`. Verify: обратно совместим с `void`, с `TestHeader` работает.

## 2. tracing: TraceHeader + TraceContext

- [x] 2.1 Create `include/nexigen/tracing/trace_header.hpp`: `TraceHeader` (32 Б, alignas(8)) с `to_string` + formatter. Verify: static_asserts (sizeof==32, is_trivially_copyable).
- [x] 2.2 Create `include/nexigen/tracing/detail/trace_context.hpp`: `TraceContext` = `{TraceId, SpanId}` + `current_trace_context()` thread_local. Verify: читается/пишется из одного потока независимо от других.

## 3. tracing: NEW_ROOT + start_span fallback

- [x] 3.1 `NEXIGEN_TRACE_NEW_ROOT` — макрос, генерирующий свежий `trace_id` в `current_trace_context()`. Verify: после вызова контекст несёт ненулевой trace_id и нулевой source_span_id.
- [x] 3.2 `Tracer::start_span` — fallback: если нет parent и стек пуст → проверить `current_trace_context()` → только потом свежий корень. Verify: спан, открытый после NEW_ROOT без parent, наследует trace_id из контекста.

## 4. Tests

- [x] 4.1 com custom header round-trip: `Publisher<int, TestHeader>` отправляет, `Subscriber<int, TestHeader>` получает, header байты совпадают. Verify: тест проходит.
- [x] 4.2 TraceContext propagation: NEW_ROOT → start_span без parent → span наследует trace_id. Verify: тест проходит.
- [x] 4.3 Cross-process simulation: в одном потоке set_trace_context, start_span → trace_id совпадает с контекстом. Verify: тест проходит.

## 5. Verification matrix

- [x] 5.1 Debug gate-ON: full `ctest` green.
- [x] 5.2 Release `-O3 -march=native -flto` + `NEXIGEN_STRICT_LATENCY_TESTS=ON`: full `ctest` green.
- [x] 5.3 TSAN + ASAN gate-ON: full `ctest` green.

## Workflow follow-up

- Интеграция TraceHeader в Iceoryx2Sink/LogDaemon — отдельным change'ом (приложения сами решают, использовать ли TraceHeader).
- Archive the change after the project's review requirements are satisfied.