# Proposal

## Why

Этап 1 дал `core::WorkerThread<Body>`, но платформенные потоки всё ещё живут вне него: `phm::FaultStorage` и `sm::TransitionAudit` используют голый `std::thread` и вообще не принимают `ThreadSpec`; `app::SignalWatcher` и HTTP-поток metricsd не пиннятся; `logd` читает `bg_cpu_affinity`, но никогда его не применяет. В результате «все потоки» настраиваются только у четырёх провайдеров, а инфраструктурные потоки могут делить ядро с RT-задачами.

Этот change закрывает этап 2: переводит оставшиеся долгоживущие потоки на `WorkerThread`, добавляет per-thread `ThreadSpec` в манифесты/конфиги с fallback на общий `bg_thread`, чинит `logd` main и убирает две копии probe-потока в `exec`.

## What Changes

- `phm::FaultStorage` и `sm::TransitionAudit` переводятся с `std::thread` на `WorkerThread`; их циклы получают `std::stop_token` вместо `std::atomic<bool> shutdown_`.
- `app::SignalWatcher` переводится на `WorkerThread`; в `app::Settings` появляется `signal_thread` (пустой = наследование).
- `metricsd`: HTTP-поток переводится на `WorkerThread`; в `MetricDaemonConfig` появляется `http_thread`, пустой — fallback на `bg_cpu_affinity`.
- `logd`: main-поток применяет `main_thread` (новое поле `ThreadSpec`), пустой — fallback на существующий `bg_cpu_affinity`; при `Required`-сбое демон завершается с ошибкой.
- `core::probe_thread_spec(spec)` — one-shot проверка spec на одноразовом потоке; две копии `std::jthread probe` в `src/exec/suite_control.cpp` и `src/exec/app_suite.cpp` заменяются одним вызовом.
- Манифесты PHM/SM получают `bg_thread` + `fault_storage_thread` / `audit_thread`; effective spec = `effective_thread_spec(primary, bg_thread)`.
- **BREAKING**: нет. Новые поля опциональны и по умолчанию пусты; пустой spec сохраняет текущее поведение (кроме `logd`, который начнёт применять уже существующий `bg_cpu_affinity` — это исправление заявленного контракта).

**Вне скоупа:** изменение `WorkerThread` (кроме `probe_thread_spec`), новые потоки вне перечисленных, переименование `bg_cpu_affinity` в `bg_thread` у metricsd/logd (совместимость сохраняется).

## Capabilities

### New Capabilities

Нет: поведение добавляется в существующие capability.

### Modified Capabilities

- `core/worker-thread`: добавляется `probe_thread_spec(spec)` — одноразовый запуск spec на отдельном потоке с возвратом статуса применения.
- `phm/manifest`: `bg_thread` и `fault_storage_thread`; storage-поток применяет effective spec.
- `sm/manifest`: `bg_thread` и `audit_thread`; audit-поток применяет effective spec.
- `app/settings`: `signal_thread` в `app::Settings`; `SignalWatcher` применяет его.
- `metrics/metrics-daemon`: `http_thread`; пустой наследует `bg_cpu_affinity`; HTTP-поток на `WorkerThread`.
- `logging/log-daemon`: `main_thread`; пустой наследует `bg_cpu_affinity`; main применяет effective spec.

## Impact

- **Код**: `include/nexigen/phm/fault_storage.hpp` + `src/phm/fault_storage.cpp`, `include/nexigen/sm/transition_audit.hpp` + `src/sm/transition_audit.cpp`, `include/nexigen/app/infrastructure.hpp` + `app_settings*`, `include/nexigen/metrics/metric_daemon_config.hpp` + `tools/nexigen-metricsd/main.cpp`, `include/nexigen/logging/log_daemon_config.hpp` + `tools/nexigen-logd/main.cpp`, `include/nexigen/core/worker_thread.hpp`, `src/exec/{suite_control,app_suite}.cpp`, JSON-сериализация манифестов/конфигов, тесты.
- **Производительность**: hot path не меняется. Потоки выполняют те же циклы (drain→файл, signalfd poll, HTTP epoll); добавляется только один self-apply spec при старте и `stop_token`-проверки вместо atomic-флага. `probe_thread_spec` создаёт поток один раз на старте EM.
- **Память**: `WorkerThread` (StartCell + jthread) вместо `std::thread`/atomic-флага; для каждого потока хранится `ThreadSpec` (192 B) в конфиге/манифесте. Аллокаций в циклах нет.
- **Поведение**: потоки без явного spec наследуют атрибуты (как раньше); `logd` main теперь применяет `bg_cpu_affinity`; `Required`-сбой нового потока останавливает соответствующий старт (FaultStorage/audit/HTTP/сигнальный watcher) по контракту `WorkerThread`.
- **Сборка**: без новых зависимостей; затронуты существующие module targets и тесты.
