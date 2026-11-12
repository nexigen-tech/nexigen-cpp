# Proposal

## Why

Проект владеет шестью долгоживущими фоновыми потоками, и четыре из них (`logging::detail::AsyncWorker`, `tracing::TracerProvider`, `chrono::Worker`, `metrics::MetricProvider`) повторяют один и тот же протокол: `std::jthread` + копия `ThreadSpec` + самоприменение атрибутов + публикация готовности + `Required`-ожидание + rollback + `request_stop/join`. `StartCell` в logging и tracing — буквально одинаковые 128 байт, а `metrics` при `Required`-сбое не делает rollback и не возвращает ошибку из старта, в отличие от остальных.

При этом `ThreadSpec` и `apply_thread_spec_self` уже централизованы, так что дублируется не применение атрибутов, а **жизненный цикл потока**. Нужен один шаблонный владелец потока, который реализует этот протокол один раз и оставляет модулю только тело цикла.

Это этап 1. Платформенные потоки без `ThreadSpec` (`phm::FaultStorage`, `sm::TransitionAudit`, `SignalWatcher`, HTTP-поток metricsd) и новые поля манифестов выносятся в отдельный change `apply-thread-specs-platform-wide`.

## What Changes

- Новый `nexigen::core::WorkerThread<Body>` — header-only владелец `std::jthread`:
  - `start(const ThreadSpec&, Body)` валидирует spec, создаёт поток, а атрибуты применяет **сам поток** (никакого parent-side pinning);
  - тело — шаблонный `Body` без `std::function`: опциональный `on_start() -> core::Status` и обязательный `run(std::stop_token)`;
  - порядок: `apply_thread_spec_self` → при `Required`-ошибке опубликовать report/code/ready и выйти → `on_start()` → первый error wins → снимок affinity → публикация ready → `run()`;
  - `request_stop` / `join` / `stop` (request + join), деструктор останавливает; повторный `start` — `Conflict`;
  - диагностика: `ready()`, `start_code()`, `report()`, `mask()`, `joinable()`, `to_string`/formatter/JSON (live-класс: без `from_json`).
- `ThreadSpec` получает `has_requests()`; в `core` появляется `effective_thread_spec(primary, fallback)` — основа варианта A, где пустой per-thread spec наследует `bg_thread` демона. В этом change helper только добавляется и тестируется; платформенные вызовы — в этапе 2.
- Четыре провайдера переводятся на `WorkerThread`:
  - `logging::detail::AsyncWorker` — StartCell и Required-rollback уходят; `writer_started()` / `writer_code()` / `writer_mask()` / `writer_thread_report()` делегируются обёртке;
  - `tracing::TracerProvider` — то же; сохраняется inline-drain в деструкторе, если воркер не стартовал;
  - `chrono::Worker` — startup-атомики уходят; CV-цикл остаётся телом;
  - `metrics::MetricProvider` — harvester переезжает на обёртку; `startup_status()` сохраняет текущий контракт, но `Required`-сбой теперь даёт единый rollback через `apply_thread_spec_self`.
- **BREAKING**: нет. Публичные API провайдеров, `ThreadSpec`, `ThreadApplyReport` и формат конфигов не меняются.

**Вне скоупа:** `phm::FaultStorage`, `sm::TransitionAudit`, `app::SignalWatcher`, HTTP-поток metricsd, `logd` main, probe-хелпер exec и новые поля манифестов — этап 2 (`apply-thread-specs-platform-wide`).

## Capabilities

### New Capabilities

- `core/worker-thread`: шаблонный владелец фонового потока — самоприменение `ThreadSpec`, публикация готовности/отчёта, `Required`-семантика, остановка и join, контракт тела.

### Modified Capabilities

- `core/thread-management`: добавляются `ThreadSpec::has_requests()` и `effective_thread_spec(primary, fallback)` — проверка «spec что-то запрашивает» и выбор fallback для варианта A.

## Impact

- **Код**: новый `include/nexigen/core/worker_thread.hpp`; правки `include/nexigen/core/thread_management.hpp`; миграция `include/nexigen/logging/detail/async_worker.hpp`, `include/nexigen/tracing/tracer_provider.hpp`, `include/nexigen/chrono/worker.hpp`, `include/nexigen/metrics/metric_provider.hpp` / `src/metrics/provider.cpp`; тесты `tests/core/worker_thread_test.cpp` (новый executable) и существующие suite'ы провайдеров.
- **Производительность**: hot path не меняется — обёртка работает только на старте/остановке и не входит в producer/harvest/drain циклы. Один `std::jthread` на воркер, как и раньше; `Body` хранится inline, без `std::function` и без дополнительной аллокации. Применение атрибутов — те же 1–3 syscall'а один раз при старте.
- **Память**: StartCell (128 B) в logging/tracing заменяется на одну `alignas(64)` ячейку готовности внутри обёртки; `ThreadApplyReport` (64 B) остаётся. `Body` — обычно лямбда размером с указатель.
- **Поведение**: `Required`-сбой теперь единообразно откатывает уже применённые атрибуты и не запускает тело; `metrics` догоняет остальных. `BestEffort` по-прежнему не блокирует `start()` и публикует частичный отчёт.
- **Сборка**: `core` остаётся INTERFACE/header-only; новый тестовый executable линкует `nexigen_core` и `Threads::Threads`.
