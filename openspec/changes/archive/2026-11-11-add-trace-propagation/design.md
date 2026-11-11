# Design

## Context

См. proposal.md — мотивация. `initial_spec` TRCC_004 определяет 32-байтный `TraceHeader` как custom header iceoryx2; TRCC_005 — автоматическое наследование контекста в потоке через `thread_local`. Текущий `nexigen::com` жёстко привязан к `void`-заголовку.

## Goals / Non-Goals

**Goals:**
- com: generic `Publisher<T, H = void>`, `Subscriber<T, H = void>`, `Sample<T, H = void>`, `SampleMut<T, H = void>` — второй шаблонный параметр H, дефолт void.
- При H ≠ void: `user_header()` на Sample, `user_header_mut()` на SampleMut, `.user_header<H>()` в билдере Publisher/Subscriber.
- tracing: `TraceHeader` (32 Б), `thread_local TraceContext`, `NEW_ROOT`, `start_span` fallback.
- Всё обратно совместимо — существующий код не меняется.

**Non-Goals:**
- Автоматическое заполнение `sequence_number` (caller инкрементирует сам — решение 1.a).
- One-shot контекст (контекст persistent — решение 2.b).
- Интеграция TraceHeader в `Iceoryx2Sink`/`LogDaemon` (отдельный change — приложения сами решают, использовать ли TraceHeader).

## Decisions

**D1 — com: H = void по умолчанию.** Существующий API `Publisher<T>` эквивалентен `Publisher<T, void>` — полная обратная совместимость. Внутренние `iox2`-типы тоже параметризуются `H`, и при `H=void` SFINAE скрывает `user_header()` из интерфейса.

**D2 — TraceHeader layout (32 Б, `alignas(8)`).** Точная раскладка:

```
offset  field                   size
0       trace_id                8   (tracing::TraceId)
8       source_span_id          8   (tracing::SpanId)
16      publish_timestamp_ns    8   (std::uint64_t)
24      sequence_number         4   (std::uint32_t)
28      (padding)               4
        ────────────────────────
        total                  32
```

`static_assert(sizeof == 32, alignof == 8, is_trivially_copyable)`. Поля `trace_id` и `source_span_id` — строго типизированные `TraceId`/`SpanId` (не сырые uint64), чтобы избежать путаницы и пользоваться `is_unset()`, `to_string()` и `operator==`.

**D3 — `thread_local TraceContext`.** Хранилище: `TraceId trace_id` + `SpanId source_span_id`. Файл: `include/nexigen/tracing/detail/trace_context.hpp`. Доступ через свободную функцию:

```cpp
[[nodiscard]] inline TraceContext& current_trace_context() noexcept {
    thread_local TraceContext ctx{};
    return ctx;
}
```

Заполняется при `receive()` из TraceHeader; читается при `start_span` (fallback) и при заполнении TraceHeader перед `send()`. Persistent — живёт до следующего `receive()` (решение 2.b).

**D4 — `start_span` fallback.** В `tracer.hpp`, после проверки явного parent и стека:

```cpp
// (существующий код):
if (parent != nullptr) { trace_id = parent->trace_id; ... }
else if (!can_push) { trace_id = TraceId::generate(); } // bounded root
else if (stack_top) { trace_id = stack_top->trace_id; }

// НОВОЕ — cross-process fallback:
else if (auto& ctx = current_trace_context(); !ctx.trace_id.is_unset()) {
    trace_id = ctx.trace_id;
    parent_span_id = ctx.source_span_id;
}

// (существующий код):
else { trace_id = TraceId::generate(); } // fresh root
```

Важно: контекст читается, но НЕ сбрасывается (persistent). Следующий корневой спан получит тот же trace_id.

**D5 — `NEW_ROOT`.** Макрос `NEXIGEN_TRACE_NEW_ROOT`:
```cpp
#define NEXIGEN_TRACE_NEW_ROOT(name) \
    do { current_trace_context().trace_id = ::nexigen::tracing::TraceId::generate(); \
         current_trace_context().source_span_id = ::nexigen::tracing::SpanId{}; \
    } while (false)
```

Или сделаем функцией для типобезопасности. Макрос для единообразия с `NEXIGEN_TRACE_SCOPE`.

**D6 — `sequence_number` (caller-managed).** Per-publisher счётчик — не входит в TraceContext (это не thread_local свойство, а per-channel). Caller хранит `uint32_t seq{0}` и инкрементирует перед каждым `send()`. Boilerplate (trace id из контекста + активный span + timestamp) снимает хелпер `make_trace_header(seq)`:

```cpp
sample.user_header_mut() = tracing::make_trace_header(seq++);
```

**D7 — SFINAE для `user_header()`.** В `Sample<T, H>` и `SampleMut<T, H>`:

```cpp
template <typename U = H, typename = std::enable_if_t<!std::is_void_v<U>>>
[[nodiscard]] const U& user_header() const noexcept { return inner_.user_header(); }
```

При `H=void` метод скрыт — код, вызывающий `user_header()`, не компилируется, что и нужно (нет заголовка → нечего читать).

**D8 — Publisher/Subscriber::create с H.** Когда `H ≠ void`, билдер добавляет `.user_header<H>()`:

```cpp
if constexpr (!std::is_void_v<H>) {
    service = service.user_header<H>();
}
```

При `H=void` вызов `user_header<void>()` пропускается (iceoryx2 этого не поддерживает — void-заголовок — это отсутствие заголовка). Логика через `if constexpr` в теле create.

## Risks / Trade-offs

- [Все существующие пользователи `Publisher<T>`/`Subscriber<T>` перекомпилируются] → дефолт `void` сохраняет их поведение — проверяется компиляцией существующего `com_test`.
- [SFINAE на `user_header()` — сложный для отладки error message] → используем `enable_if_t` с понятным условием `!std::is_void_v<H>`; при H=void попытка вызвать `user_header()` даст «no matching member function», что читаемо.
- [TraceContext добавляет скрытое состояние в `start_span`] → строки документации tracer'а явно описывают fallback; existing behaviour при пустом контексте не меняется.