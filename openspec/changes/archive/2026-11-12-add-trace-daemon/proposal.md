# Proposal

## Why

`add-iceoryx2-trace-sink` дал транспорту спанов стабильный wire-контракт (`SpanFrame` 384 Б, interval-drain, `DiscardData`), но у потока нет потребителя: спаны по-прежнему видны только внутри процесса, а центральный `TraceDaemon` из `initial_spec/tracing/trace_daemon_srs.md` (TRCD_001–007) отсутствует. Без него нельзя расследовать инциденты по последним секундам работы платформы — основной сценарий flight recorder.

Этот change закрывает ядро TRCD_001/002: демон-подписчик, RAM-ring «чёрного ящика» и сброс снимка по аварийному триггеру PHM. Управляемые сессии (TRCD_003), ftrace (004) и Perfetto-экспорт (005/006) остаются отдельными change'ами, чтобы контракт снимка стабилизировался до появления рендереров.

## What Changes

- Новый процесс `tools/nexigen-traced` — центральный `TraceDaemon`:
  - `Subscriber<SpanFrame>` на сервисе спанов + `WaitSet` с interval-таймером (по умолчанию 20 мс) и notification-вложением на событие аварийного канала. В простое — сон в ядре, 0% CPU, без polling.
  - Кольцевой буфер в RAM фиксированной ёмкости (по умолчанию 65 536 записей ≈ 24 MiB): перезапись самых старых при переполнении, счётчик перезаписей.
  - Триггер аварии: подписка на `Nexigen/SM/Recovery` (`nexigen::sm::RecoveryNotification`) и `Nexigen/SM/RecoveryEvent`; по событию демон дочитывает спаны, «замораживает» ring и пишет снимок.
  - Снимок — версионированный бинарный файл `<dump_directory>/incident_trace_<UTC>.nxtr`: 96-байтный заголовок (magic, version, frame_count, first/last ns, overwrites, trigger group, trigger timestamp) + `frame_count × SpanFrame`. Бинарный формат выбран вместо JSON: он в 2–3 раза компактнее на eMMC и является готовым входом для Perfetto-рендерера (A3).
  - После успешного снимка ring очищается; сбой записи не завершает демон, а виден в диагностике.
  - Ротация дампов: не более `max_dump_files` файлов, самый старый удаляется.
- `TraceDaemonConfig` (`service_name`, recovery-каналы, глубина очереди, `drain_interval_ms`, `ring_capacity_records`, `dump_directory`, `max_dump_files`, `trigger_on_recovery`, `app_name`, `bg_cpu_affinity`) с валидацией и trio `to_string`/`std::formatter`/JSON.
- Lifecycle: `ApplicationStateReporter::report(Running)` после инициализации, best-effort `mlockall`, привязка к сервисному ядру, корректный выход по `SIGTERM`/`SIGINT` **без** дампа (аварийный снимок не должен затираться штатной остановкой).
- **BREAKING**: нет. `SpanFrame`, sink, `com`, PHM и SM не меняются.

**Вне скоупа:** on-demand профилировочные сессии и включение категорий в клиентах (TRCD_003 → `add-trace-session-control`), ftrace/kernel events (TRCD_004 → `add-ftrace-capture`), Perfetto `.pftrace` и flow-стрелки (TRCD_005/006 → `add-perfetto-export`), JSONL-рендер снимка и ручной dump-триггер (отдельные аддитивные change'и).

## Capabilities

### New Capabilities

- `tracing/trace-daemon`: центральный flight recorder — приём спанов, ограниченный RAM-ring с вытеснением, аварийный триггер PHM, версионированный бинарный снимок и политика хранения дампов.

### Modified Capabilities

Нет: wire-контракт `tracing/iceoryx2-trace-sink`, каналы PHM/SM и API `nexigen::com` не меняются.

## Impact

- **Код**: новый `tools/nexigen-traced/main.cpp`; публичный `include/nexigen/tracing/trace_daemon_config.hpp` (+ JSON в `src/tracing/`); внутренние `include/nexigen/tracing/detail/span_ring.hpp`, `detail/trace_dump.hpp`; тесты `tests/tracing/trace_daemon_test.cpp`.
- **Производительность**: steady-state — только interval-пробуждение и дренаж очереди; на кадр приходится копия 384 Б в ring (десятки наносекунд на спане, без аллокаций), при 10 000 спанов/с и 20 мс это ~200 кадров за пробуждение. Снимок — один последовательный write ~24 MiB на аварийной ветке (cold path, допустимы аллокации и `fsync`); во время записи демон не дренирует, поэтому новые спаны могут теряться по `DiscardData` — это видно по `sequence` в кадрах.
- **Память**: ring 65 536 × 384 Б ≈ 24 MiB pre-allocated при старте + конфигурация и очереди iceoryx2. Диск в штатном режиме — **0 байт/с**.
- **Зависимости**: только существующие `nexigen::com`, `nexigen::tracing`, `nexigen::chrono`, `nexigen::exec` (readiness), `nexigen::sm` (payload). Сборка под `NEXIGEN_ENABLE_COM=ON`, новых third-party зависимостей нет.
- **Провенанс**: реализует TRCD_001, TRCD_002 и секцию `REQ_TRCD_007` (конфигурация); TRCD_003–006 явно отложены на перечисленные change'и.
