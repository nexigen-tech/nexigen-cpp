# `metrics`: инструменты, harvester и sinks

`nexigen::metrics` — телеметрия чисел. Приложение создаёт инструмент, обновляет его на горячем пути, а фоновый harvester собирает снимки и отдаёт их в sink. Модуль не форматирует и не пишет JSON на горячем пути; он платит за инкремент счётчика и оставляет сериализацию холодному пути.

## Ответственность

- Инструменты: `Counter`, `Gauge`, `StateGauge`.
- Гистограммы и статистика: `ExplicitHistogram`, `ExponentialHistogram`, `DescriptiveStatistics`, снимки `HistogramSnapshot`, `StatisticsSnapshot`.
- `Meter` — фабрика инструментов и владелец идентичности метрики.
- `MetricProvider` — реестр, фабричный корень и фоновый harvester: один выровненный блок памяти на арены, таблицы слотов и буфер сбора; поток применяет `ThreadSpec` до первой партии.
- Sinks: `ConsoleMetricSink`, `JsonFileMetricSink`, `Iceoryx2MetricSink`, `NullMetricSink`, `CompositeMetricSink`; тип-стёртая привязка через указатели на функции, без виртуальности и RTTI.
- `MetricFrame` — фиксированный кросс-процессный кадр; `MetricDaemonConfig` и `metrics_settings` — конфигурация демона и загрузчик настроек.
- Холодный рендеринг Prometheus и запись JSONL-истории.

## Ключевые типы

| Тип | Роль |
|---|---|
| `Counter` | монотонный счётчик |
| `Gauge` | числовое значение |
| `StateGauge` | именованное состояние |
| `ExplicitHistogram` | гистограмма с явными границами |
| `ExponentialHistogram` | экспоненциальная гистограмма |
| `DescriptiveStatistics` | min/max/mean/квантили |
| `HistogramSnapshot` / `StatisticsSnapshot` | снимки |
| `Meter` | фабрика и идентичность |
| `MetricProvider` | реестр, арены, harvester |
| `MetricBatchSnapshot` / `MetricSnapshot` / `MetricRecord` | снимки и записи |
| `MetricFrame` | фиксированный кадр |
| sinks | console / JSONL / iceoryx2 / null / composite |
| `MetricDaemonConfig` / `metrics_settings` | конфигурация |

## Модель инструментов

`Meter` создаёт инструменты и владеет их идентичностью. Компонент получает `Meter` и создаёт инструменты сам; указатели на инструменты не передаются в другие модули. Выданные хэндлы — заимствованные: они memory-safe, пока жив объект `Infrastructure`, а `shutdown()` останавливает подсистемы (quiesce), не освобождая их память. Полный контракт — ADR `docs/adr/0002-telemetry-handle-lifetime.md`.

Инструменты бывают write-only: чтение не требуется контрактом. `CounterLike`, `GaugeLike`, `StateGaugeLike`, `DescriptiveStatisticsLike`, `HistogramLike`, `MeterLike` живут в `core::concepts`, поэтому модуль может принимать инструменты, не завися от `nexigen::metrics`.

Политики:

- `ConcurrencyPolicy` — `SingleThreaded`/`MultiThreaded`;
- `AccessPolicy` — `Readable`/write-only;
- `ResetPolicy` — сброс между окнами;
- `MetricKind`/`MetricIdentity` — словарь и идентичность;
- `ScalarValue`/`StateValue` — значения.

## Harvester

`MetricProvider` владеет фиксированным блоком памяти: арены, таблицы слотов и буфер сбора создаются при инициализации. Harvester — фоновый поток; он собирает снимки, применяет `ThreadSpec` и отдаёт батч в sink. На горячем пути обновление инструмента не аллоцирует и не берёт блокировок; на холодном — сериализация, Prometheus-текст и JSONL.

`MetricFrame` — фиксированный кадр для cross-process доставки; `nexigen-metricsd` принимает его, агрегирует, пишет JSONL-историю с квотами и отдаёт Prometheus `/metrics`. `Iceoryx2MetricSink` живёт в `nexigen_metrics_com`; сам `nexigen_metrics` от COM не зависит.

## Связи

```text
application
  Meter -> Counter/Gauge/Histogram/Statistics
     |            (hot: increment/record)
     v
MetricProvider (fixed arenas + harvester)
     |
     +--> ConsoleMetricSink
     +--> JsonFileMetricSink -> JSONL history
     +--> Iceoryx2MetricSink -> nexigen-metricsd -> Prometheus /metrics
     +--> NullMetricSink
     +--> CompositeMetricSink
```

`metrics` зависит от `core` и `chrono`; UTC в JSONL резолвится через `TimeBaseClock`, а не через локальные часы. `synchro` использует `metrics::Meter` для своих инструментов; имена фиксированы, дубликат регистрации — ошибка создания канала.

## Горячий и холодный путь

Горячий путь — `inc()`, `set()`, `record()`: фиксированные слоты, атомики/политики, без аллокаций и блокировок. Холодный — harvest, snapshot, Prometheus, JSONL, запись в файл/кадр. `MetricProvider` не пытается «сортировать» или «дополнять» данные на горячем пути: он только принимает измерения.

## Контракты и границы

- `Counter` монотонен; `Gauge` принимает set/add/sub; `StateGauge` — именованное состояние.
- Гистограммы имеют фиксированные границы/scale; снимки неизменяемы.
- `MetricProvider` — один выровненный блок; арены и слоты не растут после старта.
- Инструменты могут быть write-only; reading не требуется контрактом.
- Тип-стёртая привязка sink'ов — указатели на функции, без виртуальности/RTTI.
- `Iceoryx2MetricSink` — только через `nexigen_metrics_com`.
- Prometheus и JSONL — холодный путь; на горячем пути нет JSON и строк.
- Квоты истории метрик-демона ограничивают рост JSONL.

## Capabilities

`counter-gauge`, `descriptive-statistics`, `examples`, `explicit-histogram`, `exponential-histogram`, `iceoryx2-metric-sink`, `metric-concepts`, `metric-provider`, `metrics-daemon`, `metric-sinks`, `metrics-settings`, `policy-enums`, `snapshot-format`, `state-gauge`.

## Читать дальше

- [`docs/architecture.md`](../architecture.md), раздел 4.5 — карта типов и провайдера.
- [`platform.md`](platform.md) — место `metrics` в наблюдаемости.
- [`app.md`](app.md) — `Meter` и порядок shutdown.
- [`logging.md`](logging.md), [`tracing.md`](tracing.md) — родственные конвейеры.
- Спецификации: `openspec/specs/metrics/*`.
