# nexigen-cpp — контекст проекта

Сжатый брифинг для загрузки в контекст ИИ-модели перед проработкой новых функций.
Снимок: **27.11.2026**, база `4aafb55` + волна P2-24…26 в рабочем дереве.
Источники: `README.md`, `docs/project-overview.md`, `docs/architecture.md`,
`openspec/specs/`, `GLOSSARY.md`, `docs/adr/`, `CMakeLists.txt`.

## 1. Что это

`nexigen-cpp` — ядро экосистемы **nexigen** (рядом: `nexigen-csharp`,
`nexigen-python`): платформа **C++23** для Linux под жёсткий real-time, HIL-стенды
и симуляторы аппаратуры, **плюс** инженерная инфраструктура — демоны, утилиты и
GUI-инструменты. Ubuntu 26.04 LTS, x86-64/ARM64. Объём — ~151 k строк C++,
~1 060 файлов, 16 модулей, 19 бинарников инструментов.

Ключевые принципы:

- **Zero-lock на горячем пути** — SPSC wait-free кольца с per-thread шардами,
  `std::atomic` с явными `memory_order`, `alignas(64)` против false sharing.
  Никаких мьютексов, аллокаций, форматирования и I/O у производителя.
- **`-fno-exceptions -fno-rtti`** глобально (единственное исключение — `com`,
  см. §4). Fallible API возвращает `core::Result<T, ErrorCode>` / `core::Status`
  поверх `std::expected`; `.value()` запрещён — ветвление по
  `has_value()`/`.error()` (есть страж `cmake/check_result_value.py`).
- **Горячий/холодный путь** — producer только стампит и кладёт в свой shard;
  форматирование, JSON, файловый/сетевой I/O, разбор манифестов — на worker'ах.
- **Время** — `CLOCK_MONOTONIC_RAW` как каноническая шкала, калиброванный TSC,
  UTC только из опубликованной time base; политика зафиксирована в
  `docs/adr/0001-time-source-policy.md`.
- **Идентичность** — app id: 16-байтовый ASCII-код из манифеста
  (`docs/adr/0004-app-id-identity.md`), штампуется во все кадры телеметрии.
- **Спецификация первична**: поведение описано в
  `openspec/specs/<module>/<capability>/spec.md` (160 спек); код приводится к
  спеке, а не наоборот. Изменения — через `openspec/changes/` (185 в архиве).
- **Один namespace = один модуль**, внутренности в `detail/`, зависимости вниз.
- **Мандат сериализации**: у публичных типов холодные `to_string()`, ADL
  `to_json`/`from_json` (при `JSON_NOEXCEPTION=1`), у enum'ов — `constexpr
  to_string`/`from_string` + `std::formatter`.
- **Мандат 12**: nlohmann/json — единственная разрешённая JSON-библиотека,
  **ровно v3.12.0** (`find_package(... 3.12.0 EXACT)`, pinned FetchContent fallback).

## 2. Модули (16) и CMake-цели

| Слой | Модуль | Цель | Тип | Назначение |
|---|---|---|---|---|
| L0 | `core` | `nexigen_core` | INTERFACE | `Result`/`ErrorCode`, сериализация, enum-словари, `SpscRingBuffer`, `ShardPool`, `thread_management`, `worker_thread`, `sink_pack::Fanout`, `sink_io`, `settings_io`, `integrity` (CRC-32C/64, SHA-256), журнал диагностики |
| L1 | `chrono` | `nexigen_chrono` | STATIC | `Timestamp`, калиброванный `StampSource`, `CivilTime`, `Provider`/`Worker`, time-sync монитор, **time base** (домены, seqlock-блок `/nexigen-timebase`, история, `nexigen-chronod`) |
| L1 | `com` | `nexigen_com` | STATIC, **gate** | SOA поверх iceoryx2: `Node`, pub/sub, request/response, `Event`/`EventChannel`, `Field`, `WaitSet`, `ServiceManifest`, E2E |
| L1 | `hw::can` | `nexigen_hw_can` | STATIC | CAN ISO-11898: `CanMessage`, каналы, `CanDriver`, provenance; COM-мосты в `nexigen_hw_can_com` |
| L1 | `canopen` | `nexigen_canopen` | STATIC | Транспортно-независимые CANopen-примитивы: CiA 306 EDS/DCF, PDO unpack, SDO decode/segmented, COB-ID routing, NMT/EMCY словари. Зависит от `core`, `chrono`, `hw::can`; **без COM/телеметрии** |
| L2 | `logging` | `nexigen_logging` | STATIC | Асинхронные логгеры (Console/File/Composite/Null), `LogRecord` с отложенным форматом, anti-spam, ротация, `LogFrame`/`nexigen-logd` |
| L2 | `metrics` | `nexigen_metrics` | STATIC | Counter/Gauge/StateGauge, explicit+exponential histogram, descriptive stats, `MetricProvider`, sinks, Prometheus, `MetricFrame` |
| L2 | `tracing` | `nexigen_tracing` | STATIC | `TracerProvider` (~10 MiB), `ScopedSpan`, контекст между потоками/процессами, console/JSONL/Chrome(Perfetto) sinks, `SpanFrame` |
| L2 | `diagnostics` | `nexigen_diagnostics` | STATIC | `Stopwatch` / `ScopedStopwatch`, бюджеты РВ, снимки лапов |
| L2 | `recorder` | `nexigen_recorder` | STATIC | Self-contained raw-сессии: `.nxrec` + `.nxidx` + `.nxvid` + `session.json`, writer/reader, CRC-32C, ротация/retention, inventory. **Не линкует COM/media/MCAP** |
| L3 | `app` | `nexigen_app` | STATIC | CLI, слияние настроек, telemetry layout, `Infrastructure<Policy>` (логгер+метрики+трейсинг+time base, `signalfd`) |
| L3 | `exec` | `nexigen_exec` | STATIC | Suite/instance lifecycle, detached-супервизор, `fork`/`setsid`/`execvp`, readiness, Function Groups, манифесты, cgroup/rlimit |
| L3 | `hub` | `nexigen_hub` | STATIC | Discovery (inotify), tailer'ы с `GapMarker`, один epoll-цикл, HTTP/SSE + web UI, read-only |
| L3 | `phm` | `nexigen_phm` | STATIC, gate | Checkpoints (alive/logical/deadline), claims/security, recovery, fault log, `/dev/watchdog` |
| L3 | `sm` | `nexigen_sm` | STATIC, gate | Арбитраж Function Groups, RootFG, LSM, UCM, аудит переходов |
| L3 | `synchro` | `nexigen_synchro` | STATIC, gate | Фазовая сетка поверх `com` event-каналов: `SyncGenerator`/`SyncConsumer`/`SyncRunner`, `SyncSchedule`, watchdog, латентность IPC/e2e |

Дополнительные цели только при `NEXIGEN_ENABLE_COM=ON`: `nexigen_state_client`,
`nexigen_exec_control` (COM-реализация `ControlHost`), `nexigen_hw_can_com`,
`nexigen_synchro`, интерфейсные `nexigen_logging_com` / `nexigen_metrics_com` /
`nexigen_tracing_com` (ядро телеметрии остаётся свободным от iceoryx2).

Поток управления: `sm --SetState--> exec/EM --> app`; readiness/ack обратно;
`app --checkpoints/health/claims--> phm --> recovery --> exec/sm`.
Телеметрия: hot call → per-thread SPSC → cold worker/harvester → console / JSONL
/ **iceoryx2 frame** → `logd`/`metricsd`/`traced`/`recd` → диск/сессии → `hub` →
HTTP/SSE/Prometheus. Время: `chronod` → shm-блок `/nexigen-timebase` → все
приложения и sink'и.

## 3. Инструменты (19 бинарников, `tools/`)

- **Управление**: `nexigen-exec` (CLI suite/instance), `nexigen-em` (front-end EM,
  тот же detached-супервизор), `nexigen-sm`, `nexigen-phmd`, `nexigen-chronod`
  (time-base демон; единственный «всегда собирается» демон, без COM).
- **Телеметрия**: `nexigen-logd` (JSONL), `nexigen-metricsd` (Prometheus +
  JSONL-история), `nexigen-traced` (RAM-ring, дамп `incident_trace_<UTC>.nxtr`),
  `nexigen-recd` (сессии `.nxrec`), `nexigen-export` (`inspect`/`list`/`info`/
  `extract`/`verify`; JSONL/Perfetto/MCAP, MP4/MKV — follow-up).
- **Обсерватория**: `nexigen-hub` (HTTP/SSE + web UI), `nexigen-logview`
  (GUI, ImGui/GLFW), `nexigen-canview` (GUI CAN/CAN-FD, ImPlot heatmap, CANopen,
  сессии, offline replay).
- **CAN-консоль**: `nexigen-canmon`, `nexigen-canrx`, `nexigen-cantx`,
  `nexigen-canvbus` (виртуальная шина), `nexigen-canrx-hw`, `nexigen-cantx-hw`
  (вендорское железо); собираются при COM (публикуют через iceoryx2), общий код —
  `tools/detail/can_tool_common.hpp`.

Сборка: `NEXIGEN_BUILD_TOOLS` (ON по умолчанию), GUI — отдельно
`NEXIGEN_BUILD_LOGVIEW`/`NEXIGEN_BUILD_CANVIEW` (OFF, требуют COM) и
`*_GUI` для X11/OpenGL-части.

## 4. iceoryx2 (обязательно к учёту)

- **Всё за гейтом `NEXIGEN_ENABLE_COM=ON`** (по умолчанию `OFF`). Включение требует
  **Rust toolchain**: `cargo → iceoryx2-ffi-c → iceoryx2-bb-cxx → iceoryx2-cxx`.
- `FetchContent` из `github.com/ekxide/iceoryx2.git`, **пин на коммит
  `b0660b5b3574d3cc18911f3c115e270630120b75`** (main 0.10.999-dev, сразу после
  стабильного 0.10.0); тегов/релизов в репо нет. Обязательно
  `IOX2_BB_CXX_CONFIG_USE_STD_EXPECTED=ON` (проект живёт на `std::expected`),
  `BUILD_EXAMPLES=OFF`.
- `nexigen_com` линкует `iceoryx2-cxx::static-lib-cxx` и **локально включает RTTI**
  (iceoryx2 выводит имена payload-типов через `typeid`), хотя весь остальной проект
  собирается с `-fno-rtti`. Это осознанное исключение.
- `com` — тонкая обёртка, скрывающая `iox2::` от прикладного кода; `com::Error` —
  своя модель ошибок, типы iceoryx2 наружу не протекают.
- **Event-система** (`com/event-system`, см. `docs/com-event-system.md`):
  `EventChannel<T>::create(node, name, config)` → `producer(app_id)`/`consumer()`.
  `EventHeader` — **80 байт**: sequence, каноническое время + домен/epoch,
  producer epoch, host ingest time, hardware-поля, канонический event code и
  **`char app_id[16]`** на offset 64. `EventConsumer` различает gap
  (`lost_count`), duplicate, sequence reset и смену epoch.
  `EventConsumerSet<MaxConsumers>` (до 16) мультиплексирует потребителей в одном
  потоке.
- **Форматы на проводе**: `LogFrame` 256 B (4 cache line), `MetricFrame`
  (trivially copyable, 16-байтовый `app_id`), `SpanFrame` 384 B,
  `EventHeader` 80 B, `SyncPayload` 32 B, `TimeBaseBlock` v2 (magic `NXTB`,
  seqlock-генерация, домены, UTC/TAI, heartbeat), `CanMessageBatch` до 100
  сообщений / 11 216 B. Публикация — `DiscardData` со счётчиками дропов;
  потребители восстанавливают пропуски по `sequence`.
- **Имена сервисов**: `Nexigen/Log/nexigen`, `nexigen/metrics/stream` (+`Event`),
  `nexigen/trace/stream`, `Nexigen/SM/State/<group>` (Field),
  `Nexigen/SM/TriggerIn`, `Nexigen/SM/Recovery`(+`Ack`), `Nexigen/PHM/Claim`,
  `Nexigen/PHM/Security`, `Nexigen/PHM/ExecutionState`, `Nexigen/EM/State/<app>`,
  `Nexigen/EM/StateCommand`, CAN — `nexigen/hw/can/<ch>/rx|tx` и `vbus/rx`.
- **Ловушка waitset'ов**: активации iceoryx2 level-triggered; если не вызвать
  `try_wait()`/`drain()` для сработавшего listener, `wait_for()` крутится без сна
  (наблюдалось как 100–200 % CPU). `EventConsumerSet` и все демоны дренируют
  активации перед обработкой.
- **Только с COM** собираются `phm`, `sm`, `synchro`, `hw_can_com`, демоны
  `nexigen-logd`/`-metricsd`/`-traced`/`-recd`/`-phmd`/`-sm`/`-em`, CAN-консоль
  (`canmon`/`canrx`/`cantx`/`canvbus` и `-hw`-варианты) и GUI-инструменты.
  Без gate собираются `nexigen-exec`, `nexigen-hub`, `nexigen-chronod`
  и `nexigen-export`; библиотеки `hw_can` и `canopen` тоже COM-независимы.

## 5. Сборка и проверки

```bash
scripts/build.sh build-asan-com build-release-com   # всегда -j 64 (не снижать)
cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j 64 && ctest --test-dir build/release --output-on-failure
cmake -B build/com -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEXIGEN_ENABLE_COM=ON
cmake -B build/logview -G Ninja -DNEXIGEN_ENABLE_COM=ON -DNEXIGEN_BUILD_LOGVIEW=ON
```

- Оси матрицы: Debug (+`NEXIGEN_ENABLE_FAULT_INJECTION`), Release
  (`-O3 -march=native`+LTO, `-Werror`), Portable Release
  (`-DNEXIGEN_NATIVE_ARCH=OFF`), TSan, ASan/UBSan (TSan+ASan — ошибка конфигурации),
  Clang с C++23-libstdc++; плюс COM-деревья, coverage и GUI.
- **Coverage**: `NEXIGEN_ENABLE_COVERAGE=ON`, цель `coverage`; CI-ратчет
  **92 %** без COM / **91 %** с COM + per-module floors (`core 96`, `metrics 95`,
  `app 95`, `hub 95`, `logging 94`, `tracing 94`, `chrono 93`, `diagnostics 93`,
  `exec 82`; при COM — `phm 91`, `sm 88`, `com 85`, `tools 85`) и файловые floors
  для phmd/claim/identity.
- **Complexity**: `python3 cmake/check_complexity.py`, цель `complexity-check`,
  CTest `nexigen_complexity_gate` — жёсткий предел **CCN 15**, цель **12**;
  для нового GUI-инструмента средняя CCN ≤ 3.0 (таргет 2.5). Сейчас гейт зелёный,
  средняя CCN по проекту ~3.0.
- **Format**: clang-format **22.x** (`format` / `format-check`, CI job `format`).
- **Тесты**: zero-dependency харнесс `NX_CHECK` (без GTest/Catch2), CTest
  (~57 записей в Debug, больше в COM/coverage); GUI-тесты через `xvfb-run`
  с точными строками вывода (`UI_VIEWS`, `UI_FOCUS`, `UI_SPLASH`, `UI_ACTIONS`).
- Дополнительно: libFuzzer для HTTP-парсера (`NEXIGEN_BUILD_FUZZERS`, Clang),
  fault injection, latency-гейты (`NEXIGEN_STRICT_LATENCY_TESTS`), бенчмарки
  хранилища (`NEXIGEN_BUILD_STORAGE_BENCHMARKS`), MCAP (`NEXIGEN_ENABLE_MCAP`).
- CI (`.github/workflows/ci.yml`, `CMAKE_BUILD_PARALLEL_LEVEL=2` — не менять):
  `build-test`, `clang`, `format`, `coverage`, `com`, `coverage-com`, `openspec`,
  `complexity`.
- Установка: `cmake --install`, `find_package(nexigen CONFIG)`, цели
  `nexigen::core|chrono|logging|metrics|tracing|diagnostics|app|exec|hub|recorder`
  (+ `com`/`phm`/`sm` при gate); пакет сам разрешает `Threads` и nlohmann.

## 6. Процесс и знания

- **OpenSpec** — единственный источник требований: `openspec/specs/`, изменения в
  `openspec/changes/` (proposal → delta specs → design → tasks), архив —
  `openspec/changes/archive/`. Валидация: `openspec validate --strict`; скиллы
  `openspec-*` в `.opencode`.
- **Issue tracker** — markdown в `.scratch/<feature-slug>/`:
  `issues/NN-<slug>.md` со строками `Status:`, `Type:`, `Blocked by:` (см.
  `docs/agents/issue-tracker.md`).
- **Домен** — `GLOSSARY.md` в корне + ADR в `docs/adr/`
  (`docs/agents/domain.md`). Принятые ADR: time-source policy, время жизни
  телеметрических хэндлов, диагностический канал, 16-байтовый app id.
- **Беклог** — `TODO.md` (P0–P3) и `docs/em-deferred.md` (осознанно отложенное).
- **Новый monitor-инструмент** — строго по `docs/agents/monitor-tool-recipe.md`:
  сначала коммитится компилирующийся скелет, CCN 12 (hard 15), `PaneSpec`-таблицы,
  CTest + coverage с первого коммита, golden pane list; после поставки —
  обновить refinement log.
- **graphify** — граф знаний в `graphify-out/` (god nodes, сообщества,
  query/path/explain); после правок — `graphify update .`.
- `docs/articles/` — публицистические разборы подсистем (logging, com, platform).

## 7. Отложено / беклог (кратко)

- **`TODO.md`**: дашборд метрик в web UI `nexigen-hub`; P3-30 — разрезка
  `HttpServer::Impl` (801 строка); P3-31 — декомпозиция `run_instance_supervisor`
  (206) и `run_suite_supervisor` (137).
- **`.scratch/` (Status: open)**: `canopen-observability` (где включать
  логи/метрики/трейсинг CANopen; предусловие для multi-node runtime и
  `nexigen-canopend`), `viewer-observability` (локальная наблюдаемость
  canview/logview без демонов), `canview-heatmap` (доводить или выключить opt-in).
- **`docs/em-deferred.md`**: REQ_EM_010 authentic execution (подпись/security-лог),
  полная capability-политика `CAP_SYS_NICE`, REQ_SM_008 OTA/UCM, YAML-манифесты
  (конфликт с mandate 12), переименование `logging→log`/`tracing→trace`
  (BREAKING), event-нотификация `StatePublication` (сейчас polling).
- **Спеки/доки**: trace sessions/category gating (`add-trace-session-control`),
  kernel ftrace (`add-ftrace-capture`), Perfetto `.pftrace`
  (`add-perfetto-export`); MP4/MKV remux — до `nexigen_media`;
  `OverflowPolicy::OverwriteOldest` не реализован в hot path; `context_id` в
  `LogFrame` зарезервирован; `BootstrapDiagnostics` — follow-up к ADR 0002.

## 8. Правила для новой функциональности

1. Начни со спеки в `openspec/specs/<module>/<capability>/spec.md`, затем код;
   задачи — в `.scratch/<feature-slug>/issues/`.
2. Ничего не выделяй и не блокируй на producer/worker hot path; всё тяжёлое — на
   cold path.
3. Ошибки — только `ErrorCode`/`com::Error`/модульные `*Error`, без исключений;
   никогда `.value()` у `Result`.
4. Публичный тип обязан получить `to_string` + `to_json`/`from_json` +
   `std::formatter` (для enum — ещё `constexpr` пару).
5. Всё, что тянет iceoryx2, — строго за `NEXIGEN_ENABLE_COM`; ядро модуля не
   видит `iox2::`. Новые фиксированные кадры — trivially copyable, pointer-free,
   с `static_assert(sizeof(...))`, 16-байтовым `app_id` и счётчиками дропов.
6. Не создавай второй `FetchContent` для ImGui/GLFW — только
   `cmake/GuiDependencies.cmake`; для мониторов — рецепт из §6.
7. Проверки перед сдачей: `clang-format`, `check_complexity.py`, CTest во всех
   деревьях (Release/ASan/TSan/COM), `coverage` при новом коде, `graphify update .`.
8. Сборка локально — `scripts/build.sh` / `cmake --build ... -j 64`.
9. Комментарии/доки в коде — по-английски; архитектурные документы и беклог —
   по-русски.

## 9. Куда смотреть

| Вопрос | Документ |
|---|---|
| Карта проекта и инфраструктуры | `docs/project-overview.md` |
| Глубокая архитектура, диаграммы, контракты | `docs/architecture.md` |
| Event-система и `EventHeader` | `docs/com-event-system.md` |
| Формат сессий и `nexigen-export` | `docs/recorder.md` |
| Trace-демон и дампы | `docs/trace-daemon.md` |
| Hub API/UI | `docs/nexigen-hub.md` |
| Низкие задержки, потоки, метрики | `docs/low-latency-guide.md`, `docs/thread-management.md`, `docs/metrics-guide.md` |
| Suite runner | `docs/runner-suite.md` |
| Термины и спорные названия | `GLOSSARY.md` |
| Решения | `docs/adr/` |
| Требования по capability | `openspec/specs/<module>/<capability>/spec.md` |
| Беклог и приоритеты | `TODO.md`, `docs/em-deferred.md`, `.scratch/` |
| Примеры | `examples/` (`app`, `logging`, `metrics`, `tracing`, `exec`, `recorder`, `integration`) |
| Граф знаний | `graphify query "<вопрос>"` / `graphify explain` / `graphify path` |
