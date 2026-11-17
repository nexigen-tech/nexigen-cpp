# nexigen-cpp — контекст проекта

Сжатый брифинг для загрузки в контекст ИИ-модели перед проработкой новых функций.
Актуально на 16.11.2026. Источники: `README.md`, `docs/architecture.md`,
`openspec/specs/`, `CMakeLists.txt`.

## 1. Что это

`nexigen-cpp` — ядро экосистемы **nexigen** (рядом: `nexigen-csharp`, `nexigen-python`):
библиотека **C++23** для Linux под **жёсткий real-time** и симуляторы аппаратуры.
Ubuntu 26.04 LTS, целевые платформы x86-64/ARM64.

Ключевые принципы:

- **Zero-lock на горячем пути** — только SPSC wait-free очереди и атомики, никаких
  мьютексов, аллокаций, форматирования и I/O у производителя.
- **`-fno-exceptions -fno-rtti`** глобально. Fallible API возвращает
  `core::Result<T, ErrorCode>` / `core::Status` (поверх `std::expected`);
  `.value()` запрещён — ветвление через `has_value()`/`.error()`.
- **Горячий/холодный путь** — строгое разделение: producer только стампит и кладёт
  в свой shard; форматирование, JSON, файловый и сетевой I/O — на worker'ах.
- **Спецификация первична**: поведение описано в
  `openspec/specs/<module>/<capability>/spec.md`; код приводится к спеке, а не
  наоборот (121 capability, ~834 требования, ~2087 сценариев).
- **Один namespace = один модуль**, внутренности в `detail/`, зависимости только вниз.
- **Мандат сериализации**: у публичных типов холодные `to_string()`, ADL
  `to_json`/`from_json` (при `JSON_NOEXCEPTION=1`), у enum'ов — `constexpr
  to_string`/`from_string` + `std::formatter`.
- **Мандат 12**: nlohmann/json — единственная разрешённая JSON-библиотека,
  **ровно v3.12.0** (`find_package(... 3.12.0 EXACT)`, fallback — pinned
  FetchContent).

## 2. Модули (12) и CMake-цели

| Слой | Модуль | Цель | Тип | Назначение |
|---|---|---|---|---|
| L0 | `core` | `nexigen_core` | INTERFACE | `Result`/`ErrorCode`, сериализация, enum-словари, `SpscRingBuffer`, `ShardPool`, `thread_management` (`CpuSet`/`ThreadSpec`/`apply_thread_spec_self`), `worker_thread`, `sink_pack::Fanout`, `sink_io`, `settings_io`, `integrity` (CRC-32C, CRC-64/XZ, SHA-256) |
| L1 | `chrono` | `nexigen_chrono` | STATIC | `Timestamp` (CLOCK_MONOTONIC_RAW), `StampSource` (калиброванный TSC, hot path без syscall), `CivilTime`/ISO-8601 ns, `Provider`/`Worker`, UTC-offset, `TimeSyncMonitor` |
| L1 | `com` | `nexigen_com` | STATIC, **gate** | SOA поверх **iceoryx2**: `Node`, pub/sub, req/resp, `Event`, `Field`, `WaitSet`, `ServiceManifest`, `e2e` |
| L2 | `logging` | `nexigen_logging` | STATIC | Асинхронные логгеры (Console/File/Composite/Null), `LogRecord` с отложенным форматом, anti-spam, ротация, `nexigen-logd`-транспорт |
| L2 | `metrics` | `nexigen_metrics` | STATIC | Counter/Gauge/StateGauge, explicit+exponential histogram, descriptive stats, `MetricProvider` (harvester), sinks, Prometheus, кадры |
| L2 | `tracing` | `nexigen_tracing` | STATIC | `TracerProvider` (~10 MiB, статическая/куча), `ScopedSpan`, контекст между потоками/процессами, console/JSONL/Chrome(Perfetto) sinks |
| L2 | `diagnostics` | `nexigen_diagnostics` | STATIC | `Stopwatch` / `ScopedStopwatch`, бюджеты РВ |
| L3 | `app` | `nexigen_app` | STATIC | CLI, слияние настроек, telemetry layout, `Infrastructure<Policy>` (владение подсистемами, `signalfd`) |
| L3 | `exec` | `nexigen_exec` | STATIC | Suite/instance lifecycle, detached-супервизор, `fork`/`setsid`/`execvp`, readiness, Function Groups, манифесты, cgroup/rlimit |
| L3 | `hub` | `nexigen_hub` | STATIC | Discovery (inotify), tailer'ы с `GapMarker`, один epoll-цикл, HTTP/SSE + web UI, read-only |
| L3 | `phm` | `nexigen_phm` | STATIC, gate | Health management: checkpoints, claims (`/proc`, uid/gid), recovery, fault log с CRC-32C, `/dev/watchdog` |
| L3 | `sm` | `nexigen_sm` | STATIC, gate | State Manager: арбитраж Function Groups, RootFG, аудит переходов, LSM, UCM |

Инструменты (`tools/`): `nexigen-exec`, `nexigen-em`, `nexigen-hub`, `nexigen-logd`,
`nexigen-metricsd`, `nexigen-phmd`, `nexigen-sm`, `nexigen-traced`.

Поток управления: `sm --SetState--> exec/EM --> app`; readiness/ack обратно;
`app --checkpoints/health/claims--> phm --> recovery --> exec/sm`.

Поток телеметрии: hot call → per-thread SPSC shard → cold worker/harvester →
console / JSONL / **iceoryx2 frame** → `logd` / `metricsd` / `traced` → диск →
`hub` → HTTP/SSE/Prometheus.

## 3. iceoryx2 (обязательно к учёту)

- **Всё за гейтом `NEXIGEN_ENABLE_COM=ON`** (по умолчанию `OFF`). Включение требует
  **Rust toolchain**: цепочка `cargo → iceoryx2-ffi-c → iceoryx2-bb-cxx → iceoryx2-cxx`.
- Зависимость тянется через `FetchContent` из `github.com/ekxide/iceoryx2.git`,
  **пин на коммит `b0660b5b3574d3cc18911f3c115e270630120b75`** (main на 0.10.999-dev,
  сразу после стабильного 0.10.0); тегов/релизов в репо нет. Обязательно
  `IOX2_BB_CXX_CONFIG_USE_STD_EXPECTED=ON` (проект живёт на `std::expected`),
  `BUILD_EXAMPLES=OFF`.
- `nexigen_com` линкует `iceoryx2-cxx::static-lib-cxx`, **локально включает RTTI**
  (iceoryx2 выводит имена payload-типов через `typeid`), хотя весь остальной проект
  собирается с `-fno-rtti`. Это осознанное исключение.
- `com` — тонкая обёртка, скрывающая `iox2::` от прикладного кода; `com::Error` —
  своя модель ошибок, типы iceoryx2 наружу не протекают.
- Только с `COM=ON` появляются цели: `nexigen_state_client`, `nexigen_exec_control`,
  интерфейсные `nexigen_logging_com` / `nexigen_metrics_com` / `nexigen_tracing_com`
  (ядро модулей телеметрии остаётся свободным от iceoryx2), а также `phm`/`sm` и
  инструменты `nexigen-logd`, `nexigen-metricsd`, `nexigen-traced`, `nexigen-phmd`,
  `nexigen-sm`. `nexigen_exec` и `nexigen_hub` собираются и без gate.
  `control_host_com.cpp` вырезается из ядра через `list(FILTER ... EXCLUDE REGEX)`.
- Транспорт: фиксированные pointer-free кадры (`LogFrame`, `MetricFrame`,
  `SpanFrame` = 384 B), публикация `DiscardData` со счётчиками дропов, потребители
  дренят по интервалу/событию и восстанавливают пропуски по `sequence`
  (gap-маркеры).
- Именование сервисов: `Nexigen/<Domain>/...` — например `Nexigen/Log/nexigen`,
  `Nexigen/EM/State/<app>`, `Nexigen/EM/StateCommand`, `Nexigen/SM/State/<group>`,
  `Nexigen/PHM/Claim`, `Nexigen/PHM/Recovery`, `Nexigen/PHM/Security`; у
  metrics/tracing дефолты `nexigen/metrics/stream` и `nexigen/trace/stream` +
  парный `<service>Event`.
- Демоны: `nexigen-logd` (почасовые JSONL), `nexigen-metricsd` (история +
  Prometheus `/metrics`), `nexigen-traced` (RAM-ring 65 536 записей, дамп
  `incident_trace_<UTC>.nxtr` по событию PHM).

## 4. Сборка и проверки

```bash
scripts/build.sh build-asan-com build-release-com   # всегда -j 64 на этой машине
cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j 64 && ctest --test-dir build/release --output-on-failure
cmake -B build/com -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEXIGEN_ENABLE_COM=ON
```

Матрица (все должны давать 100% tests passed): Debug (+fault injection), Release
(`-O3 -march=native` + LTO, `-Werror`), Portable Release
(`-DNEXIGEN_NATIVE_ARCH=OFF`), TSan, ASan/UBSan (TSan и ASan взаимоисключающи),
плюс Clang с C++23-libstdc++. Покрытие: `NEXIGEN_ENABLE_COVERAGE=ON`, цель
`coverage`, порог 95% (runner/hub ≥ 90%), сейчас ~95.3%. Форматирование —
clang-format **22.1.8** (`format` / `format-check`). Тесты — zero-dependency
харнесс (без GTest/Catch2). Есть fault-injection и libFuzzer для HTTP-парсера.
CI: `CMAKE_BUILD_PARALLEL_LEVEL=2` (не менять).

## 5. Где смотреть при проработке новой функции

- `docs/architecture.md` — карта модулей, зависимостей, mermaid-диаграммы процессов
  и каналов (самый плотный источник контекста).
- `README.md` — подробные API-контракты и примеры по каждому модулю (~72 KB).
- `openspec/` — workflow: proposal → design → specs (delta) → tasks → архив в
  `openspec/changes/archive/`; скиллы `openspec-*` в `.opencode`.
- `docs/` — `low-latency-guide.md`, `thread-management.md`, `metrics-guide.md`,
  `nexigen-hub.md`, `runner-suite.md`, `trace-daemon.md`, `safety/`,
  `project-analysis.md`.
- `examples/` — `core`, `chrono`, `logging`, `metrics`, `tracing`, `phm`, `suite`,
  `integration` (последний — только COM=ON).
- `graphify-out/` — граф знаний проекта; для вопросов по коду сначала
  `graphify query "<вопрос>"`, затем `graphify path` / `graphify explain`; после
  правок — `graphify update .`.

## 6. Практические правила для новой функциональности

1. Начни со спеки в `openspec/specs/<module>/<capability>/spec.md`, затем код.
2. Ничего не выделяй и не блокируй на producer/worker hot path; всё тяжёлое — на
   cold path.
3. Новые ошибки — только через `ErrorCode`/`com::Error`, без исключений.
4. Публичный тип обязан получить `to_string` + `to_json`/`from_json` +
   `std::formatter` (для enum — ещё `constexpr` пару).
5. Всё, что тянет iceoryx2, — строго за `NEXIGEN_ENABLE_COM`; ядро модуля не
   должно видеть `iox2::`.
6. Комментарии/доки — по-английски в коде, архитектурные документы частично
   русские.
7. После правок: `clang-format`, полная матрица, `graphify update .`.
