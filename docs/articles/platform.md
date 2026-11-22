# Платформа nexigen: три плоскости

Платформа nexigen начинается не с библиотеки, а с разделения. Есть плоскость управления, которая решает, какие процессы должны работать; плоскость данных, которая двигает время и сообщения; и плоскость наблюдаемости, которая объясняет, что произошло. `nexigen-cpp` делает каждую плоскость явной: `exec`/`sm`/`phm` управляют, `chrono`/`com` двигают, `logging`/`metrics`/`tracing`/`recorder`/`hub` наблюдают. Эта статья — о том, почему границы между плоскостями проходят именно здесь, и что ломается, когда их пытаются размыть.

Это обзор устройства платформы: слои, компоненты, связи и сквозные контракты. Подробные разборы модулей вынесены в отдельные статьи — [`core`](core.md), [`chrono`](chrono.md), [`com`](com.md), [`app`](app.md), [`exec`](exec.md), [`sm`](sm.md), [`phm`](phm.md), [`hub`](hub.md), [`metrics`](metrics.md), [`tracing`](tracing.md), [`diagnostics`](diagnostics.md), [`synchro`](synchro.md); логирование и запись сессий уже разобраны в [`logging.md`](logging.md) и [`recorder.md`](recorder.md). Полный справочник по модулям и зависимостям остаётся в [`docs/architecture.md`](../architecture.md).

## Три плоскости

| Плоскость | Модули | Вопрос, на который отвечает |
|---|---|---|
| Управление | `exec`, `sm`, `phm` | кто должен работать, в каком состоянии и что делать при отказе |
| Данные | `chrono`, `com`, `synchro` | какое сейчас время, как доставить сообщение и выдержать фазу |
| Наблюдаемость | `logging`, `metrics`, `tracing`, `diagnostics`, `recorder`, `hub` | что произошло, сколько это стоило и где это лежит |

`core` и `app` стоят поперёк: `core` — фундамент для всех, `app` — точка сборки приложения и владелец телеметрии.

Разделение не косметическое. Управление меняется медленно и живёт в control plane: переходы состояний, readiness, рестарты. Данные обязаны укладываться в наносекунды и не блокироваться. Наблюдаемость не влияет на решение, но обязана честно фиксировать потери. Если смешать эти задачи, получается система, где логгер ждёт диск, а супервизор зависит от логгера; `nexigen` разводит их по разным потокам, процессам и, где нужно, по разным сборкам.

## Один такт

Посмотрим на платформу изнутри одного такта приложения.

1. **Время.** `app::Infrastructure::run(tick)` читает `chrono::Timestamp::now()` и вызывает `time_base_->on_tick(...)`. Если time base потерян, цикл останавливается с причиной `TimeBaseLost`; приложение не продолжает работать по «примерно верному» времени.
2. **Пользовательский такт.** Внутри callback приложение пишет лог, инкрементит счётчик, открывает спан, отправляет `com`-сообщение, ставит PHM checkpoint. Все эти вызовы идут через разные модули, но подчиняются одному правилу: горячий путь не выделяет память, не форматирует и не блокируется.
3. **Телеметрия.** Логгер кладёт `LogRecord` в свой per-thread shard; метрики обновляют инструмент; tracing открывает `ScopedSpan`. Фоновые worker'ы/harvester'ы позже рендерят, сериализуют и отдают запись в sink: консоль, JSONL или фиксированный кадр в iceoryx2.
4. **Транспорт.** `com` публикует сообщение zero-copy; `synchro` публикует фазу по абсолютной сетке; PHM получает checkpoints и health. Если транспорт теряет sample, потребитель видит `lost_count`, а не «дыру».
5. **Наблюдаемость.** JSONL-файлы и кадры попадают в `nexigen-logd`/`nexigen-metricsd`/`nexigen-traced`, затем в run-директорию; `nexigen-hub` читает её и отдаёт HTTP/SSE; `nexigen-export` конвертирует raw-сессии. Ни один из этих шагов не находится на пути такта.
6. **Остановка.** `Infrastructure::shutdown()` останавливает watcher, пишет финальную запись, делает `tracer.quiesce()`, `metrics.quiesce()`, `logger.flush()`/`quiesce()` и `TimeBaseClock::detach()`. Память провайдеров освобождает только деструктор, поэтому выданные ранее хэндлы остаются memory-safe до конца `main`.

Управляющая плоскость в этом такте не участвует напрямую: `exec` уже запустил процесс, `sm` уже разрешил состояние, `phm` ждёт checkpoint. Но именно они определяют, должен ли такт вообще происходить. Поэтому платформа — это не только runtime-путь, но и контекст, который его разрешает.

## Три бюджета

У каждой системы реального времени есть валюта, в которой она платит. В nexigen этих валют три.

| Бюджет | Что защищает | Кто платит | Чем платит |
|---|---|---|---|
| Наносекунды | горячий путь | `core`, `logging`, `metrics`, `tracing`, `com`, `synchro`, `diagnostics` | SPSC-шарды, relaxed atomics, отложенный формат, zero-copy, фиксированные кадры |
| Детерминизм времени | единая шкала и фаза | `chrono`, `synchro`, `exec` | калиброванный TSC, time base, seqlock, абсолютные дедлайны, seeded splitmix64 |
| Целостность | данные и идентичность | `core`, `com`, `exec`, `phm`, `recorder` | CRC-32C/64, SHA-256, E2E-профили, claims, fault-журнал, атомарный манифест |

Бюджеты конфликтуют. Проверка CRC на каждой записи стоит времени; E2E-заголовок увеличивает payload; `fsync` в рекордере уменьшает пропускную способность; строгий time base требует демона. Платформа не пытается «оптимизировать всё»: она выбирает, где бюджет тратится, и делает цену видимой. Логирование платит 20 нс за принятую запись и честно считает дропы; `com` не копирует payload, но требует shared memory; `recorder` не делает `fsync` на каждую запись и говорит, что именно останется после падения.

## Карта модулей

| Слой | Модуль | Тип | Роль | Подробно |
|---|---|---|---|---|
| L0 | `core` | INTERFACE | ошибки, сериализация, SPSC, потоки, integrity, concepts | [core.md](core.md) |
| L1 | `chrono` | STATIC | время, TSC, time base, `nexigen-chronod` | [chrono.md](chrono.md) |
| L1 | `com` | STATIC, gate | iceoryx2 SOA, pub/sub, events, waitset, E2E | [com.md](com.md) |
| L2 | `logging` | STATIC | асинхронные логи, `LogFrame`, `nexigen-logd` | [logging.md](logging.md) |
| L2 | `metrics` | STATIC | инструменты, harvester, sinks, `nexigen-metricsd` | [metrics.md](metrics.md) |
| L2 | `tracing` | STATIC | спаны, контекст, sinks, `nexigen-traced` | [tracing.md](tracing.md) |
| L2 | `diagnostics` | STATIC | `Stopwatch`, `ScopedStopwatch` | [diagnostics.md](diagnostics.md) |
| L2 | `recorder` | STATIC | raw-сессии, writer/reader, конвертер | [recorder.md](recorder.md) |
| L3 | `app` | STATIC | CLI, настройки, `Infrastructure`, signals | [app.md](app.md) |
| L3 | `exec` | STATIC | suite/instance lifecycle, супервизия, Function Groups | [exec.md](exec.md) |
| L3 | `hub` | STATIC | discovery, streaming, HTTP/SSE, web UI | [hub.md](hub.md) |
| L3 | `phm` | STATIC, gate | супервизия, checkpoints, recovery, watchdog | [phm.md](phm.md) |
| L3 | `sm` | STATIC, gate | арбитраж Function Groups, RootFG, UCM, LSM | [sm.md](sm.md) |
| L3 | `synchro` | STATIC, gate | фазовая сетка поверх event-каналов | [synchro.md](synchro.md) |

`core` — INTERFACE, потому что состоит из шаблонов и inline-кода. Остальные модули компилируют холодные пути в статические архивы. `com` и всё, что требует iceoryx2, появляется только при `NEXIGEN_ENABLE_COM=ON`; `logging`/`metrics`/`tracing` остаются COM-свободными, а их iceoryx2-sink'и живут в интерфейсных целях `nexigen_logging_com`/`nexigen_metrics_com`/`nexigen_tracing_com`.

## Связи

### Правила зависимостей

```text
core  <-- chrono <-- logging/metrics/tracing/diagnostics/recorder
  ^         ^
  |         +------ app <-- exec <-- hub
  |                   ^
  +----- com <--------+------ phm/sm/synchro
```

- `core` зависит только от стандартной библиотеки и `nlohmann/json`.
- `chrono` и `com` зависят только от `core`; телеметрия — от `core`/`chrono`.
- `recorder` зависит только от `core`/`chrono` и не линкует COM, media, MCAP или FFmpeg.
- `app` — единственный владелец подсистем телеметрии; `exec` использует `app`, но его COM-части вынесены в отдельные цели.
- `hub` зависит от `exec` только ради типов раскладки; он ничего не запускает.
- `phm` и `sm` работают поверх `com`, но общаются с `exec` сообщениями, а не линковкой.
- `synchro` — прикладной слой поверх `com`, `chrono`, `logging` и `metrics`; сам `com` о телеметрии не знает.

Осознанные исключения живут в `detail/` и не являются публичным контрактом: `metrics` использует HTTP-парсер `hub`, `tracing` включает типы `exec`/`sm` для состояния trace-демона, `phm` включает словари `sm` для сообщений.

### Плоскость управления

```text
nexigen-sm --SetState--> nexigen-exec/nexigen-em --fork/exec--> application
     ^                         |                                  |
     |                         | readiness / StateAck             | checkpoints / claims
     |                         v                                  v
     +--------------------- suite state <--------------------- nexigen-phmd
```

- `nexigen-exec`/`nexigen-em` поднимают detached-супервизор, запускают каждое приложение в новой сессии и группе процессов, применяют остановку `SIGTERM → SIGKILL`.
- `nexigen-sm` арбитрирует Function Groups: проверяет переход по графу, приоритеты и RootFG, публикует состояние как `Field`.
- `nexigen-phmd` наблюдает checkpoints/health/claims, проверяет identity через `/proc`, при `Expired` запускает recovery через EM или watchdog.
- `RootFG` — корневая группа (брокер, SM, PHM, EM); пока она не в `Run`, прикладные группы не покидают off-like состояния.

### Плоскость данных

- `chrono` формирует единую монотонную шкалу. `nexigen-chronod` публикует `TimeBaseBlock` в shared memory; приложения прикрепляются к нему через `TimeBaseClient`/`TimeBaseClock`, а не создают собственный `Provider`. Потеря обязательного домена переводит приложение в holdover, затем в `time_base_lost`.
- `com` даёт zero-copy pub/sub, request/response, events, fields и `WaitSet`; ошибки iceoryx2 не протекают наружу, а E2E-профили защищают обмен от искажений, повторов и переупорядочения.
- `synchro` строит абсолютную фазовую сетку `T(c,p) = T_start + c·Tc + ⌊p·Tc/N⌋`, публикует `SyncPayload` и классифицирует опоздания/пропуски, не смешивая потерю транспорта с перегрузкой генератора.

### Плоскость наблюдаемости

```text
hot path: log/span/metric -> per-thread shard -> worker/harvester -> sink
sink: console | JSONL file | fixed frame (iceoryx2)
frame -> nexigen-logd / nexigen-metricsd / nexigen-traced -> run directory
file  -> nexigen-hub -> HTTP/SSE / web UI / Prometheus
raw   -> nexigen-export -> JSONL / Perfetto / MCAP / elementary video
```

Инварианты конвейера: производитель не выделяет память и не блокируется; переполнение ring'а публикует `GapMarker`; у каждого модуля свой shard; JSON, имена и файловый I/O — только на холодном пути.

## Сквозные контракты

- **Ошибки без исключений.** Глобальные `-fno-exceptions -fno-rtti`; публичные fallible-API возвращают `core::Result<T, ErrorCode>`/`core::Status`; `.value()` запрещён. Исключение — cold-path RTTI в `com` для iceoryx2.
- **Сериализация как мандат.** У публичных типов есть `to_string()` (член и свободная функция), ADL `to_json`/`from_json`, `std::formatter`; у перечислений — `constexpr to_string`/`from_string`. Тела живут в `src/*/*_format.cpp`, чтобы не раздувать TU.
- **Горячий и холодный путь.** Горячий: без выделений, wait-free atomics/SPSC, наносекундный бюджет. Холодный: `std::string`, `nlohmann::json`, файлы, мьютексы control plane.
- **Потоки и affinity.** `CpuSet` до 1024 CPU; `ThreadSpec` задаёт имя, политику и приоритет; режим `required` откатывает инициализацию, `best-effort` виден в `ThreadApplyReport`.
- **Идентичность запуска.** `NEXIGEN_RUN_ID`, `NEXIGEN_EXEC_DIR`, `NEXIGEN_INSTANCE_ID`, `NEXIGEN_INSTANCE_DIR`; телеметрическая раскладка общая для C#, Python и C++.
- **JSONL v2.** Каноническая шкала — `CLOCK_MONOTONIC_RAW`; `mono_ns`/`boot` достоверны всегда, UTC — производное поле и может быть `null`, если калибровка не покрывает instant. Строка без `mono_ns` считается legacy.
- **Безопасность.** SHA-256 для манифестов, исполняемых файлов и настроек; `trusted_platform`, `uid`/`gid`, сброс `CAP_SYS_NICE`; identity claims через `/proc`; E2E; `TimeSyncMonitor`; fault-журнал с CRC.

## Инструменты

| Инструмент | Сборка | Роль |
|---|---|---|
| `nexigen-exec` | всегда | lifecycle suite/instance: `start`, `stop`, `status` |
| `nexigen-hub` | всегда | HTTP/SSE-обсерватория и web UI |
| `nexigen-export` | всегда (MCAP опционально) | on-demand конвертер raw-сессий |
| `nexigen-em` | COM | фронтенд Execution Manager |
| `nexigen-sm` | COM | State Manager |
| `nexigen-phmd` | COM | супервизия, recovery, watchdog |
| `nexigen-logd` | COM | `LogFrame` → почасовой JSONL |
| `nexigen-metricsd` | COM | агрегация метрик, Prometheus, JSONL-история |
| `nexigen-traced` | COM | RAM-ring спанов и аварийный снимок |
| `nexigen-chronod` | всегда | time base: домены, seqlock-блок, `chrono.jsonl` |

## Как читать дальше

Начните с трёх плоскостей и одного такта. Затем откройте модуль, который нужен:

- фундамент: [`core`](core.md)
- время и транспорт: [`chrono`](chrono.md), [`com`](com.md), [`synchro`](synchro.md)
- телеметрия: [`logging`](logging.md), [`metrics`](metrics.md), [`tracing`](tracing.md), [`diagnostics`](diagnostics.md), [`recorder`](recorder.md)
- приложение и управление: [`app`](app.md), [`exec`](exec.md), [`sm`](sm.md), [`phm`](phm.md)
- наблюдаемость: [`hub`](hub.md)

Справочник по всем модулям, зависимостям и сценариям — [`docs/architecture.md`](../architecture.md).

## Сводка

Платформа держится на трёх решениях. Плоскости разделены: управление не блокирует данные, данные не ждут наблюдаемость, наблюдаемость не влияет на решение. Горячий путь платит наносекундами и не выделяет память; холодный путь платит файлами и JSON. Целостность не маскируется: потеря видна как `GapMarker`, время — как `time_base_lost`, отказ — как fault-запись и recovery.

Цена этой схемы — несколько процессов, демонов и сборок с gate'ами. Взамен приложение получает предсказуемый такт, управляемое состояние и артефакты, которые можно прочитать после падения.
