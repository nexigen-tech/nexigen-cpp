# Проработать локальное логирование и сбор метрик в canview и logview

Status: open
Type: task

## Контекст

`nexigen-canview` и `nexigen-logview` — локальные GUI-инструменты с собственными
потоками захвата/ингеста, bounded-историей, фоновыми перестройками индексов,
импортом/экспортом и resource guard (ECO). Сейчас диагностика в основном
разбросана по status-панелям, счётчикам модели и отдельным `printf`/stderr;
нет единого локального контракта «лог + метрики», пригодного для разбора
проблем на стенде без запуска платформенных демонов.

Нужно отдельно проработать локальную наблюдаемость обоих инструментов, не
превращая её в телеметрический демон и не добавляя аллокаций/блокировок на
горячие пути.

## Что нужно проработать

- **Общий контракт для обоих инструментов**:
  - уровни/категории логов и формат (консоль, файл, ring для UI);
  - локальные счётчики/гистограммы и способ их чтения (status panel, JSON dump,
    диагностический файл);
  - где хранится состояние: process-local, без COM-сервисов и без обязательного
    `App<Policy>`.
- **canview**:
  - захват: rx-батчи, sequence gaps, overflow ingress, dropped, reconnects;
  - CANopen: EDS load/parse errors, SDO abort/timeout, PDO mapping errors, NMT/
    heartbeat stale, dynamic mapping evictions;
  - модель/UI: ingest rate, history occupancy/evictions, rebuild time, filter/find
    latency, render FPS/frame time, ECO transitions, CPU/memory;
  - экспорт/импорт: throughput, skipped records, errors, phase timing;
  - ошибки конфигурации и layout/INI.
- **logview**:
  - ingest rate, dropped/overflow, ring evictions, backpressure;
  - per-view rebuild/index time, filter/find latency, pause snapshot size;
  - render FPS/frame time, ECO transitions, memory/CPU;
  - импорт/экспорт и replay: progress, skipped/error records, throughput;
  - COM loss/restart и ошибки discovery.
- **Формат и стоимость**:
  - hot path только fixed-size counters; форматирование/запись — на cold path;
  - rate limiting и защита от log storm;
  - бюджет overhead по latency/CPU/памяти и способ проверки;
  - не ломать offline/headless режимы и не требовать `nexigen-logd`/`nexigen-metricsd`.
- **Диагностический экспорт**:
  - единый snapshot (JSON) для приложения к bug report: версия, конфиг, счётчики,
    последние N логов, состояние ECO/очередей.

## Критерии готовности проработки

- [ ] Согласован общий локальный контракт для canview и logview.
- [ ] Зафиксирован список логов, уровней и anti-storm политики.
- [ ] Зафиксирован список метрик с именами, единицами и местами отображения.
- [ ] Определён JSON-снимок диагностики для bug report.
- [ ] Зафиксированы overhead-бюджеты и способ их измерения.
- [ ] Подготовлен OpenSpec change (spec/design/tasks) на реализацию.

## Открытые вопросы

- Использовать ли существующие `nexigen_logging`/`nexigen_metrics` как
  библиотеки или сделать tool-local fixed-size observability без `App`?
- Нужен ли постоянный файловый лог по умолчанию, или только opt-in `--log-file`
  и диагностический snapshot?
- Должны ли метрики быть видны в UI всегда, или только в отдельной diagnostics
  панели/по запросу?
- Нужен ли общий код observability для canview/logview, или допустимо два
  независимых небольших слоя?

## Comments
