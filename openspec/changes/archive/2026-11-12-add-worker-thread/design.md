# Design

## Context

См. `proposal.md` — Why. Текущее состояние:

- `core::ThreadSpec` + `apply_thread_spec_self` уже реализуют валидацию, порядок name → affinity → scheduler, rollback и `ThreadApplyReport`. Применение атрибутов дублировать не нужно.
- Четыре провайдера независимо реализуют один и тот же протокол: `jthread`, копия spec, самоприменение, публикация готовности, `Required`-ожидание, `request_stop/join`. `StartCell` в logging и tracing — идентичные 128-байтные структуры; chrono/metrics используют отдельные атомики.
- Семантика `Required` расходится: logging/tracing/chrono откатывают старт и возвращают ошибку, metrics публикует `startup_error_`, но не делает rollback и не возвращает ошибку из конструктора.
- `core/thread-management` фиксирует инвариант: поток применяет spec **сам**, родитель не трогает native handle.

## Goals / Non-Goals

**Goals:**

- Один шаблонный владелец потока, который реализует start/ready/Required/rollback/stop один раз.
- Сохранить публичные API и наблюдаемость провайдеров (`writer_started`, `worker_mask`, `worker_thread_report`, `startup_status`).
- Убрать расхождение `Required`-семантики, не меняя hot path.
- Дать основу варианта A: `has_requests()` + `effective_thread_spec(primary, fallback)`.

**Non-Goals:**

- Платформенные потоки (`phm`/`sm` storage/audit, `SignalWatcher`, HTTP metricsd, `logd` main) и новые поля манифестов — этап 2.
- Универсальный цикл (`PeriodicWorker`/`DrainWorker`): тела циклов остаются у модулей.
- `std::function`/type erasure, изменения `ThreadSpec` JSON или `ThreadApplyReport`.

## Decisions

### D1 — `WorkerThread<Body>`: шаблон, тело inline, без type erasure

```cpp
template <class Body>
class WorkerThread {
public:
    Status start(const ThreadSpec& spec, Body body) noexcept;
    void request_stop() noexcept;
    void join() noexcept;
    void stop() noexcept;            // request_stop + join
    [[nodiscard]] bool joinable() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] core::ErrorCode start_code() const noexcept;
    [[nodiscard]] const core::ThreadApplyReport& report() const noexcept;
    [[nodiscard]] std::uint64_t mask() const noexcept;
private:
    std::optional<Body> body_{};
    std::jthread thread_{};
    StartCell start_{};
};
```

`Body` хранится inline; `start` вызывает `std::jthread` один раз. `std::function` не используется: вызов тела происходит один раз на поток, но шаблон сохраняет стиль проекта (никакого virtual dispatch/RTTI) и исключает возможную аллокацию для больших захватов.

### D2 — Двухфазный контракт тела

```cpp
// on_start опционален; run обязателен.
Status on_start() noexcept;          // sink.start()/provider start
void run(std::stop_token) noexcept;  // модульный цикл
```

Определение — через `requires`:

```cpp
template <class B>
concept WorkerBody = requires(B& body, std::stop_token stop) {
    { body.run(stop) } noexcept;
};

template <class B>
concept HasWorkerStart = WorkerBody<B> && requires(B& body) {
    { body.on_start() } noexcept -> std::same_as<core::Status>;
};
```

Порядок в worker-потоке:

```
apply_thread_spec_self(spec, report)
        |
        +-- Required + error --> publish(code/report/ready) --> return (тело не зовётся)
        |
        v
on_start()  (если есть)
        |
        v   первый error wins: attribute error, иначе on_start error
publish(code/report/mask/ready)
        |
        v
run(stop_token)
```

`on_start`-ошибка не подавляет `run`: это сохраняет текущее поведение logging/tracing (sink может не открыться, но цикл продолжает дренировать и повторять попытки/логировать). `Required`-ошибка атрибутов — единственный случай, когда тело не запускается.

### D3 — StartCell: одна cache-line-ячейка, release/acquire

```cpp
struct alignas(64) StartCell {
    std::atomic<bool> ready{false};            // release store / acquire load
    std::atomic<std::uint8_t> code{0};         // ErrorCode, пишется до ready
    std::atomic<std::uint64_t> mask{0};        // post-pin view, до ready
    core::ThreadApplyReport report{};          // 64 B, до ready
    // padding до 64-байтной границы
};
```

- Worker пишет `code/report/mask` обычными store'ами, затем `ready.store(true, release)` и `ready.notify_one()`.
- Читатель (стартующий поток) после `ready.load(acquire)` (или `ready.wait(false, acquire)`) видит согласованный снимок.
- `std::atomic::wait/notify_one` (C++20) заменяет текущие `sleep(200us)`/`yield()` в `Required`-ожидании. Если сборочная среда потребует `-latomic`, откатываемся на 200 µs poll без изменения API.
- Атомики только в стартовой ячейке; цикл тела не содержит атомиков обёртки.
- После того как стартующий поток прочитал `Required`-ошибку, `start()` сбрасывает `ready` (report/code/mask остаются читаемыми). Это сохраняет прежний контракт провайдеров: `writer_started()`/`worker_started()` — false после неудачного старта, но `start_code`/`worker_thread_report` доступны.

### D4 — `start()`: Required ждёт, BestEffort — нет

- Валидация spec до создания потока; `start` на joinable → `Conflict`.
- `Required`: после запуска ждать `ready`; если `report.first_error() != Ok`, `request_stop + join`, сбросить `body_`, вернуть `SystemError` (или validation error). Это повторяет текущее поведение logging/tracing/chrono.
- `BestEffort`: вернуться сразу после создания потока; `ready()/start_code()/report()` доступны для опроса. Это сохраняет неблокирующий старт, на который опираются существующие тесты.
- Ошибка `on_start` попадает в `start_code()`/`report()`, но не меняет возврат `start()` (кроме Required-атрибутной ошибки). Так сохраняется текущий контракт `writer_start_code`/`startup_status`.

### D5 — Lifecycle

- `request_stop()` — `thread_.request_stop()`; `join()` — `thread_.join()`, затем сброс `body_`; `stop()` — оба.
- Деструктор вызывает `stop()`.
- После успешного старта повторный `start` — `Conflict`. После `Required`-провала поток join-ится и `body_` сбрасывается, поэтому повторный `start` возможен (текущие провайдеры могут retry).
- Тело, ожидающее на CV, должно использовать `condition_variable_any::wait_until(lock, stop, ...)` — `request_stop` будит его без внешнего notify. Модули, у которых есть собственный флаг (HTTP и т.п. — этап 2), используют `std::stop_callback`.
- Владелец обязан объявлять `WorkerThread` **последним** членом: деструктор join-ит раньше, чем умирают члены, которые захватывает тело. Это правило документируется в заголовке и `docs/thread-management.md`.
- Создание `std::jthread` под `-fno-exceptions` по-прежнему `std::terminate` при отказе ОС; контракт не меняется.

### D6 — Диагностика

`WorkerThread` — публичный live-класс, поэтому по mandate 9–11:

- `to_string()`: spec, ready, start_code, report, mask;
- free `to_string`, `std::formatter`;
- `to_json`: `spec` (через `ThreadSpec` JSON), `ready`, `start_code`, `report` (поля `requested/applied/failed/rollback_failed/first_error` — у `ThreadApplyReport` нет ADL `to_json`, собираем объект вручную), `mask`;
- `from_json` отсутствует намеренно (live-класс), отсутствие пинится compile-time тестом.

### D7 — `ThreadSpec::has_requests` и `effective_thread_spec`

```cpp
[[nodiscard]] constexpr bool has_requests() const noexcept {
    return !cpus_.empty() || !name_.empty() || scheduler_configured_;
}

[[nodiscard]] constexpr ThreadSpec
effective_thread_spec(const ThreadSpec& primary, const ThreadSpec& fallback) noexcept {
    return primary.has_requests() ? primary : fallback;
}
```

- `has_requests` не меняет `validate()` и JSON.
- `effective_thread_spec` не мутирует аргументы; используется в этапе 2, но тестируется уже здесь.
- Явный `Other,0` считается запросом (`scheduler_configured_ == true`) и не подменяется fallback'ом.

### D8 — Миграция провайдеров

**logging `AsyncWorker`**
- `StartCell`/`jthread` удаляются; `Body` захватывает `AsyncWorker*`.
- `on_start` → `sink_->start()`; `run` → существующий drain + flush barrier + backoff; shutdown-drain остаётся в теле после цикла.
- `start()` валидирует spec, создаёт sink, вызывает `worker_.start(spec, Body{this})`; при ошибке сбрасывает sink.
- `writer_started/code/mask/thread_report` делегируют обёртке.

**tracing `TracerProvider`**
- `StartCell`/`start_worker`/`worker_` удаляются; `initialize` проверяет `worker_.joinable()`, калибрует stamps, запускает `worker_.start(config_.worker_thread, Body{this})`.
- `on_start` → `binding_.start`; `run` → `consume_loop(stop)` (без вызова `start_worker`).
- Деструктор: если `worker_.joinable()` — stop; иначе inline-drain (как сейчас) только если sink не стартовал (никогда не инициализировались или Required-провал). Признак — `worker_.ready()` и `report().first_error()`.
- `worker_mask/started/start_code/thread_report` делегируют.

**chrono `Worker`**
- `worker_`, `startup_*`, `worker_thread_report_` заменяются обёрткой; `start()` идемпотентен по `joinable()`.
- `run` — текущий fixed-rate CV-цикл; `on_start` отсутствует.
- `startup_status()` → `start_code()`; `worker_mask/report` делегируют.

**metrics `MetricProvider`**
- `harvester_`/`startup_*` заменяются обёрткой; конструктор запускает её, если `collect_interval > 0`.
- `run` — fixed-rate CV harvest; `on_start` отсутствует.
- `startup_status()` сохраняет контракт: если воркер запускался — `start_code()`, иначе ошибка валидации/`Ok`.
- `Required`-провал теперь делает rollback через `apply_thread_spec_self` и не запускает цикл — расхождение с остальными устраняется.
- Деструктор: `worker_.stop()`, затем финальный `collect_now()`/`flush()`.

### D9 — Тесты и сборка

- Новый `tests/core/worker_thread_test.cpp` + executable `nexigen_worker_thread_tests` (линкует `nexigen_core`, `Threads::Threads`, использует fault injection для детерминированного `Required`-провала, как `tests/core/core_test.cpp`).
- Проверки: тело/поток, порядок `on_start→run`, `Conflict`, stop/join, `Required`-провал без тела, BestEffort с телом, `on_start`-ошибка с телом, mask, диагностика/JSON, `has_requests`/`effective_thread_spec`.
- Существующие suite'ы провайдеров должны остаться зелёными; latency-гейты не меняются.
- Тестовый target добавляется в `tests/CMakeLists.txt`; core остаётся header-only.

## Risks / Trade-offs

- **[`atomic::wait` требует поддержки/линковки]** → fallback на 200 µs poll; API и семантика не меняются.
- **[Расхождение поведения провайдеров]** → accessor'ы делегируются, а BestEffort/Required-контракт зафиксирован тестами; отдельно проверяется tracing destructor inline-drain и metrics constructor start.
- **[Тело захватывает `this`, обёртка должна быть последним членом]** → документируется в заголовке и `docs/thread-management.md`; code review + пример.
- **[`on_start`-ошибка не возвращается из `start()`]** → сохраняем текущий контракт; ошибка видна в `start_code()`/`report()`; тест фиксирует.
- **[Required-провал и повторный старт]** → wrapper join-ит и сбрасывает тело; провайдеры, которые не поддерживали retry, не меняют своего поведения.
- **[Fault injection выключен в некоторых конфигурациях]** → тест `Required`-провала помечается как условный (как в core suite), остальные проверки безусловны.

## Migration Plan

- Аддитивно: новый заголовок + helper'ы; затем по одному провайдеру (logging → tracing → chrono → metrics), каждый со своими тестами.
- Откат: вернуть прежние StartCell/jthread в конкретном провайдере; обёртка и helper'ы остаются.
- Публичные API/конфиги не меняются; wire/JSON форматы не затрагиваются.

## Open Questions

- Нужен ли `probe_thread_spec(spec)` в `core` уже в этом change'е или в этапе 2 вместе с exec? Сейчас он не нужен четырём провайдерам; вынесен в этап 2.
