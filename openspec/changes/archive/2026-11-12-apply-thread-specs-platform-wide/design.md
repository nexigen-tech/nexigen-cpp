# Design

## Context

См. `proposal.md` — Why. После `add-worker-thread` есть `core::WorkerThread<Body>` с самоприменением `ThreadSpec`, readiness/`Required` и остановкой. Оставшиеся платформенные потоки:

- `phm::FaultStorage` и `sm::TransitionAudit` — `std::thread` + `std::atomic<bool> shutdown_`, без `ThreadSpec`;
- `app::SignalWatcher` — `jthread` без спецификации;
- `metricsd` HTTP — `jthread` без спецификации;
- `logd` main — `bg_cpu_affinity` (`CpuSet`) читается, но не применяется;
- `exec` — две одинаковые `std::jthread probe` для preflight Required-спеки.

## Goals / Non-Goals

**Goals:**

- Перевести все перечисленные потоки на `WorkerThread`, сохранив их циклы и наблюдаемость.
- Дать каждому потоку явный `ThreadSpec` в конфиге/манифесте с fallback по варианту A.
- Убрать дублирование probe-потока.

**Non-Goals:**

- Менять `WorkerThread` (кроме `probe_thread_spec`).
- Добавлять новые потоки или переименовывать существующие поля `bg_cpu_affinity`.
- Менять wire-форматы, обработку логов/метрик/спанов.

## Decisions

### D1 — PHM/SM: `WorkerThread` + stop_token + два поля манифеста

`FaultStorage` и `TransitionAudit` получают конструктор `(path, core::ThreadSpec spec)`; тело — существующий `worker_loop`, переписанный на `while (!stop.stop_requested())` с проверкой stop после дренажа. `std::atomic<bool> shutdown_` удаляется.

Манифесты:

```cpp
struct PhmManifest { ...; core::ThreadSpec bg_thread{}; core::ThreadSpec fault_storage_thread{}; };
struct SmManifest  { ...; core::ThreadSpec bg_thread{}; core::ThreadSpec audit_thread{}; };
```

Демоны передают `core::effective_thread_spec(primary, bg_thread)` в конструктор. Пустые поля = наследование (поведение не меняется).

### D2 — `app::SignalWatcher`: `WorkerThread` + `Settings::signal_thread`

`SignalWatcher::start(target, mask, spec)` заменяет `std::jthread` на `WorkerThread<Body>`, где `Body::run` — текущий poll-цикл signalfd. `Settings` получает `core::ThreadSpec signal_thread{}`; `Infrastructure::initialize` передаёт его. Ошибка создания/старта закрывает `fd_` и возвращает статус (как сейчас при ошибке signalfd). Пустой spec = наследование.

### D3 — metricsd HTTP: `WorkerThread` + stop_callback

`MetricDaemonConfig` получает `core::ThreadSpec http_thread{}`. Эффективная спека — `effective_thread_spec(http_thread, bg_cpu_affinity)`.

`HttpFront`, `start_http` и `stop_http` вынесены в тестируемый `include/nexigen/metrics/detail/http_front.hpp` (main остаётся тонким). `HttpFront` хранит `WorkerThread<Body>`; `Body` захватывает `MetricHttpServer*`, `std::atomic<bool>* stop`, `ExpositionBuffer*`. В начале `run` ставится `std::stop_callback` на `stop.store(true)`, затем вызывается существующий `server.serve(stop, exposition)`. `stop_http` вызывает `worker.stop()` (request + join), затем `server.stop_listen()`.

### D4 — logd main: `main_thread` с fallback на legacy CpuSet

`LogDaemonConfig` получает `core::ThreadSpec main_thread{}`; `bg_cpu_affinity` остаётся для совместимости. Эффективная спека:

```cpp
ThreadSpec legacy{};
if (!bg_cpu_affinity.empty()) legacy.set_cpus(bg_cpu_affinity);
const ThreadSpec effective = effective_thread_spec(main_thread, legacy);
```

`run_daemon` вызывает `apply_thread_spec_self(effective, report)` до создания Node/подписчика; `Required`-ошибка → `return 1`. `Info`-строка с отчётом — по желанию.

### D5 — `probe_thread_spec`: один helper вместо двух копий

В `core/worker_thread.hpp`:

```cpp
[[nodiscard]] inline Status probe_thread_spec(const ThreadSpec& spec) noexcept {
    struct Body { void run(std::stop_token) noexcept {} };
    WorkerThread<Body> worker{};
    const Status started = worker.start(spec, Body{});
    if (!started) return started;
    worker.join();
    return {};
}
```

`BestEffort` `start` возвращает сразу; `join` ждёт короткого тела. Caller (`exec` preflight) проверяет только `Required`, поэтому статуса достаточно; отчёт остаётся доступен только внутри helper'а и не нужен наружу.

`src/exec/suite_control.cpp` и `src/exec/app_suite.cpp` заменяют свои лямбды на `core::probe_thread_spec(spec)`.

### D6 — Политика fallback (вариант A)

| Поток | Per-thread поле | Fallback |
|---|---|---|
| PHM fault storage | `fault_storage_thread` | `bg_thread` |
| SM audit | `audit_thread` | `bg_thread` |
| app signal watcher | `signal_thread` | нет (пусто = inherit) |
| metricsd HTTP | `http_thread` | `bg_cpu_affinity` |
| logd main | `main_thread` | `bg_cpu_affinity` (CpuSet→spec) |

Пустой effective spec не делает syscall. Платформенные демоны задают fallback в своих манифестах; библиотека `app` не навязывает политику.

### D7 — Совместимость

- Новые поля JSON опциональны; отсутствие = пустой spec = прежнее поведение (кроме `logd`, который теперь применяет уже существующий `bg_cpu_affinity` — это исправление).
- Новые поля агрегатов добавляются **в конец** (`Settings::signal_thread`, `LogDaemonConfig::main_thread`), чтобы позиционная aggregate-инициализация старых полей оставалась валидной; `Settings::create(name)` — именованный путь конструирования.
- `phmd`/`sm` завершают старт с кодом 1 при `Required`-провале storage/audit-спеки (не работают в деградированном режиме молча); `logd` возвращает 1; `metricsd` не стартует HTTP.
- `LogDaemonConfig::bg_cpu_affinity` и `MetricDaemonConfig::bg_cpu_affinity` не переименовываются.
- `PhmManifest`/`SmManifest` JSON расширяются; старые манифесты парсятся без изменений.
- `FaultStorage`/`TransitionAudit` конструкторы получают новый параметр со значением по умолчанию (`ThreadSpec{}`), чтобы существующие тесты/вызовы не ломались; демоны передают effective spec.

### D8 — Тесты

- `nexigen_core_worker_thread_tests`: `probe_thread_spec` — Required failure, BestEffort success, caller unchanged, probe joined.
- `nexigen_phm_tests`/`nexigen_sm_tests`: manifest JSON round-trip новых полей; placement-тесты через `effective_thread_spec` + `thread_report()/thread_mask()`; существующие supervision/audit тесты проходят; FaultStorage/TransitionAudit пишут записи после миграции.
- `nexigen_app_tests`: `Settings` JSON с `signal_thread`; существующий watcher-тест.
- `nexigen_metrics_tests`/daemon tests: `http_thread` JSON и fallback; smoke metricsd.
- `nexigen_logging_com_daemon_tests`/logd smoke: `main_thread` JSON; smoke с `bg_cpu_affinity`.
- exec tests: preflight с Required-спекой по-прежнему возвращает ошибку при отказе.

## Risks / Trade-offs

- **[stop_token меняет момент выхода циклов]** → проверка stop после каждого дренажа; shutdown-поведение тестов фиксирует.
- **[HTTP server использует atomic-флаг]** → `std::stop_callback` внутри тела; внешний API сервера не меняется.
- **[Новый Required-провал может остановить демон]** → это ожидаемый контракт; пустые spec по умолчанию не меняют поведение.
- **[logd теперь пиннится по legacy-полю]** → документируется как исправление; поле существовало и обещало это.
- **[probe helper в BestEffort не возвращает отчёт]** → caller (exec) проверяет только Required; расширение API — отдельно.
- **[Порядок членов]** → `WorkerThread` остаётся последним членом в каждом владельце; документируется.

## Migration Plan

- Аддитивно: новые поля/конструкторы с дефолтами; затем по одному потоку (phm → sm → app → metricsd → logd → exec probes).
- Откат: вернуть `std::thread`/`jthread` в конкретном владельце; поля остаются неиспользованными.

## Open Questions

Нет: fallback-политика и семантика `probe_thread_spec` зафиксированы выше; вопрос о возврате отчёта из probe отложен до реальной потребности.
