# Проработать логирование и сбор метрик в компонентах CANopen

Status: resolved
Type: task

## Контекст

После `extract-canopen-core` появился transport-independent `nexigen::canopen`
(EDS/DCF, PDO, SDO, COB-ID routing). Следующий слой — multi-node runtime и
`nexigen-canopend`; для них нужны наблюдаемость и диагностика, но сейчас у core
намеренно нет зависимостей от `nexigen_logging`, `nexigen_metrics` и
`nexigen_tracing`.

Нужно отдельно проработать, где и как включить логирование, метрики и трассировку,
не нарушив hot-path и не размыв границу «core без COM/GUI/telemetry».

## Что нужно проработать

- **Граница слоёв**: остаётся ли `nexigen_canopen` чистым, а телеметрия живёт в
  `nexigen_canopen_com`/`nexigen-canopend`, или core получает внедряемые
  интерфейсы/хэндлы метрик.
- **Логирование**:
  - уровни и категории (init/config, NMT state, SDO, PDO, EMCY, bus-off, IPC);
  - rate limiting и защита от log storm на горячем пути;
  - что логируется на cold path, что только счётчиками;
  - формат сообщений и обязательный контекст (bus, channel, node_id, index/sub).
- **Метрики**:
  - EDS: parse duration, failures, object/mapping counts;
  - SDO: client/server requests, responses, aborts, timeouts, bytes, queue depth;
  - PDO: tx/rx, dropped, mapping errors, period/jitter;
  - NMT/Heartbeat: state transitions, missed heartbeats, boot-up counts;
  - EMCY: counts by code/register, history evictions;
  - signals: updates, stale/out-of-range, conversion saturation;
  - runtime: step latency, deadline misses, TX queue depth/drops, IPC publish failures;
  - правила именования, единицы, labels и кардинальность (не создавать метку на
    каждый сигнал).
- **Трассировка**: spans для SDO-транзакций, PDO/SYNC цикла, EDS load; корреляция
  с `nexigen::tracing`, стоимость включения/выключения.
- **PHM/SM**: какие события (bus-off, heartbeat timeout, критичные EMCY, deadline
  miss) эскалируются и через какой контракт.
- **Overhead**: бюджеты по latency/CPU/памяти, замеры на `canvbus`, отсутствие
  аллокаций и форматирования на hot path.

## Критерии готовности проработки

- [x] Зафиксирован слой-владелец логирования/метрик/tracing: runtime-слой; core остаётся
  telemetry-free.
- [x] Согласован список метрик с именами, единицами и labels: Tier 1 на шину, per-bus meter
  `canopen.<bus>`, без per-node/per-PDO labels.
- [x] Определены правила логирования и anti-storm политика: policy A, edge-gate, repeat/recovery,
  global budget, `OBS-SUM`.
- [x] Определены spans и точки корреляции (реализация отложена до runtime-change; зафиксированы
  в `design.md`).
- [x] Зафиксированы требования к overhead и способ их проверки: бюджет hot path, zero allocation,
  benchmark + latency/TSan/ASan-гейты.
- [x] Подготовлен OpenSpec change (spec/design/tasks) на реализацию.

## Открытые вопросы

- Давать ли core внедряемый `CanOpenTelemetry` интерфейс или считать метрики только
  в runtime-адаптере? — Решено: только runtime-адаптер, core остаётся чистым.
- Нужны ли per-node/per-PDO labels или достаточно агрегатов по bus/node? — Решено: labels нет;
  один meter на шину, per-node детали в логах и `OBS-SNAP`.
- Какие события должны немедленно менять `HealthState`/инициировать SM-переход? — Отложено до
  runtime-change: bus-off, heartbeat timeout, critical EMCY, cycle deadline miss.
- Нужна ли отдельная метрика для каждого сигнала из `*.nxmap.json`, или только
  агрегированные quality/staleness counters? — Решено: агрегированные
  `signal_updates/stale/out_of_range`, без per-signal метрик.

## Resolution

Проработка завершена в OpenSpec change
[`add-canopen-observability`](../../../openspec/changes/add-canopen-observability/proposal.md):

- `proposal.md` — runtime-owned observability и границы изменения.
- `design.md` — Tier 1/Tier 2 каталог, meter на шину, error gate, suppression ladder,
  capacity formula, отложенные spans и PHM/SM-корреляция.
- `specs/canopen/observability/spec.md` — нормативный контракт.
- `tasks.md` — план реализации и проверок.

Tier 2 сохранён в `design.md` как deferred; реализация runtime и tracing/PHM — отдельные
follow-up changes.

## Comments
