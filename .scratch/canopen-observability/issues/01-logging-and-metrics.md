# Проработать логирование и сбор метрик в компонентах CANopen

Status: open
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

- [ ] Зафиксирован слой-владелец логирования/метрик/tracing.
- [ ] Согласован список метрик с именами, единицами и labels.
- [ ] Определены правила логирования и anti-storm политика.
- [ ] Определены spans и точки корреляции.
- [ ] Зафиксированы требования к overhead и способ их проверки.
- [ ] Подготовлен OpenSpec change (spec/design/tasks) на реализацию.

## Открытые вопросы

- Давать ли core внедряемый `CanOpenTelemetry` интерфейс или считать метрики только
  в runtime-адаптере?
- Нужны ли per-node/per-PDO labels или достаточно агрегатов по bus/node?
- Какие события должны немедленно менять `HealthState`/инициировать SM-переход?
- Нужна ли отдельная метрика для каждого сигнала из `*.nxmap.json`, или только
  агрегированные quality/staleness counters?

## Comments
