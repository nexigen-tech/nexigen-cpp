# `app`: точка сборки приложения

`nexigen::app` — прикладной слой L3. Он не двигает данные и не управляет процессами; он собирает приложение из подсистем, задаёт порядок инициализации и остановки, владеет обработкой сигналов и телеметрической раскладкой. Если `core` — фундамент, то `app::Infrastructure` — каркас, на который вешается `main`.

Модуль зависит от `core`, `chrono`, `logging`, `metrics` и `tracing`; `com` в него не входит. Политика приложения может использовать COM-sink'и, но сама `Infrastructure` остаётся COM-свободной.

## Ответственность

- Разбор командной строки: `parse_command_line`, `CommandLineOptions`, `CommandLineError`, `CommandLineDiagnostic`. `-h/--help` короткозамыкает разбор. `-an/--app-name` задаёт длинное имя, `-ai/--app-id` — 16-байтовый код для кадров; EM передаёт оба ребёнку.
- Слияние настроек: `app::Settings` и `AppSettingsPaths` собирают logging/metrics/tracing/chrono из global/local JSON.
- Идентичность запуска: `RunIdentity` читает `NEXIGEN_RUN_ID`, `NEXIGEN_EXEC_DIR`, `NEXIGEN_INSTANCE_ID`, `NEXIGEN_INSTANCE_DIR`; `telemetry_layout`/`TelemetryPaths` отображают её в пути metrics/traces/chrono.
- `Infrastructure<Policy>` — heap-only сервис, создаваемый только через `make_infrastructure<Policy>()`. Владеет логгером, метриками, трассировкой и chrono, монопольно владеет `SIGINT`/`SIGTERM`/`SIGHUP` через `signalfd`, выполняет `run(tick)`, фиксирует причину остановки и освобождает ресурсы в обратном порядке.
- `app::App<Policy>` — фасад для минимальных приложений: `create`/`run`/`shutdown`, метрики одной строкой, до 16 подписок одного payload-типа и опциональные выходы, накопление диагностики и обязательный `app_id`.
- `InfrastructureStopReason` — причина остановки: запрос, `SIGINT`/`SIGTERM`/`SIGHUP`, потеря time base.
- `app::exit_code(reason)` — единая таблица кодов выхода: штатные причины дают 0, потеря time base — 75, `None` — 1.

### Причины остановки и коды выхода

| `InfrastructureStopReason` | Источник | `app::exit_code` |
|---|---|---|
| `Requested` | `Infrastructure::request_stop()` | 0 |
| `Sigint` | `SIGINT` через `signalfd` | 0 |
| `Sigterm` | `SIGTERM` через `signalfd` | 0 |
| `Sighup` | `SIGHUP` через `signalfd` | 0 |
| `TimeBaseLost` | внешняя база времени пропала | 75 |
| `None` | `run()` ещё не завершился | 1 |
- Диагностика переходов: `DiagnosticBinding` + `DiagnosticForwarder<Logger>`; до появления логгера — `BootstrapDiagnostics` в stderr.

## Ключевые типы

| Тип | Роль |
|---|---|
| `parse_command_line` / `CommandLineOptions` | холодный разбор аргументов |
| `Settings` / `AppSettingsPaths` | агрегат четырёх подсистем и пути источников |
| `RunIdentity` | идентичность запуска из `NEXIGEN_*` |
| `telemetry_layout` / `TelemetryPaths` | пути metrics/traces/chrono |
| `Infrastructure<Policy>` | владелец телеметрии и runtime-цикла |
| `make_infrastructure<Policy>()` | единственный способ создать `Infrastructure` |
| `InfrastructureStopReason` | причина остановки |
| `DiagnosticBinding` / `DiagnosticForwarder` | переходы подсистем в логгер |
| `BootstrapDiagnostics` | диагностика до старта логгера |

## Политика

Политика — stateless-тип с тремя статическими хуками:

```cpp
struct FilePolicy {
    using Logger = nexigen::logging::FileLogger<>;
    using MetricsSink = nexigen::metrics::JsonFileMetricSink;
    using TraceSink = nexigen::tracing::JsonFileTraceSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Settings& settings,
                                       const TelemetryIdentity& identity) noexcept;
    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings& settings,
                                             const TelemetryIdentity& identity) noexcept;
    static core::Status emplace_trace_sink(std::optional<TraceSink>& out,
                                           const tracing::Settings& settings,
                                           const TelemetryIdentity& identity) noexcept;
};
```

Политика не хранит состояние: она вызывается один раз на инициализации и конструирует компоненты в `std::optional`. Именно здесь приложение выбирает, куда писать: файл, консоль, iceoryx2-кадры, композит или null.

`TelemetryIdentity` несёт два поля: длинное `app_name` для файловых стоков и короткий `core::AppId` для кадров. `Settings::app_id` заполняется из `-ai/--app-id`; EM передаёт его ребёнку из манифеста. Если id не задан, `Iceoryx2Policy` использует compile-time tag `A..D` как fallback. Валидный id — 1–16 символов `A-Z`, `a-z`, `0-9`, `_`, `-`.

```cpp
auto created = nexigen::app::make_infrastructure<FilePolicy, 256, 8>();
if (!created) {
    return 1;
}
auto infra = std::move(*created);

auto settings = nexigen::app::Settings::create("demo");
settings.logging.options.log_root_directory = "/var/log/nexigen/demo";
settings.chrono.segment_name = "/nexigen-timebase";

if (!infra->initialize(settings)) {
    return 1;
}

auto logger = infra->logger();
if (!logger.has_value()) {
    return 1;
}
(*logger)->info("ready");
infra->run([&] { /* один такт */ });
if (!infra->shutdown()) {
    return 1;
}
```

### Ожидание данных вместо busy-loop

`examples/app/ex_app_wait_loop.cpp` показывает минимальное приложение, которое не крутит `run` вхолостую: producer-поток складывает события в очередь, а tick ждёт их на `std::condition_variable` с таймаутом 100 мс. Таймаут нужен, чтобы `run` успевал заметить `SIGINT`/`SIGTERM` и потерю time base. На каждое событие приходят лог, счётчик и спан; `chrono::Timestamp` события берётся из `Timestamp::now()`.

Это переносимый вариант без COM. В COM-сборке очередь заменяется на `com::WaitSet`/`EventChannel`, а обвязка `Infrastructure` (logger, meter, tracer, shutdown) остаётся той же.

### Готовые политики

В `include/nexigen/app/policies/` лежат готовые stateless-политики:

| Политика | Куда пишет | Зависимости |
|---|---|---|
| `app::policies::ConsolePolicy` | консоль: лог в fd 1/2, метрики и спаны в console-sink | `nexigen::app` |
| `app::policies::FilePolicy` | JSONL: лог в `log_root_directory`, метрики/трейсы в `settings.sink.path` | `nexigen::app` |
| `app::policies::NullPolicy` | никуда (бенчмарки, тесты) | `nexigen::app` |
| `app::policies::Iceoryx2Policy<A,B,C,D>` | всё в iceoryx2: `LogFrame`, `MetricFrame`, `SpanFrame` | `nexigen::app` + `nexigen::logging_com`, `nexigen::metrics_com`, `nexigen::tracing_com`, `nexigen::com` |

Базовые политики подключаются через `<nexigen/app/policies/policies.hpp>`. Iceoryx2-политика — отдельный заголовок `<nexigen/app/policies/iceoryx2_policy.hpp>`, потому что требует `NEXIGEN_ENABLE_COM=ON`; она держит один общий `com::Node` для metric/trace-sink'ов, а логгер создаёт свой node сам. Эффективный `app_id` берётся из runtime-настроек; четыре template-символа — только fallback для standalone-приложений и тестов.

`FilePolicy` требует, чтобы в `app::Settings` были заданы `logging.options.log_root_directory`, `metrics.sink.path` и `tracing.sink.path`; пустой путь — ошибка инициализации соответствующего sink'а. `ConsolePolicy` и `NullPolicy` работают с настройками по умолчанию.

Примеры: `examples/app/ex_app_wait_loop.cpp` — console, `examples/app/ex_app_wait_loop_iceoryx2.cpp` — все три потока в iceoryx2 (собирается при `NEXIGEN_ENABLE_COM=ON`). Контракт политик — `openspec/specs/app/policies/spec.md`. Тесты: `tests/app/policies_test.cpp` (Console/File/Null) и `tests/app/policies_com_test.cpp` (Iceoryx2, log/metric/trace frames). У `Iceoryx2Policy` есть необязательный `NodeProvider` (по умолчанию `SharedNodeProvider` с process-wide node); тест подставляет провайдер, возвращающий `nullptr`, и проверяет `SystemError`.

## `App<Policy>`: минимальное приложение

`App<Policy>` собирает типовой cold-path сценарий: разбор опций, настройки по умолчанию или из каталога, time base (внешняя или явно встроенная), `Infrastructure`, метрики, до 16 подписок одного payload-типа и опциональные выходы. `run()` не стартует, если хотя бы один объявленный instrument или port не создался: ошибки копятся в `diagnostics()` и возвращаются как `InvalidArgument`.

```cpp
using Policy = nexigen::app::policies::Iceoryx2Policy<'Q', 'S', 'T', 'R'>;
auto created = nexigen::app::App<Policy>::create(
    argc, argv, "quickstart", nexigen::app::TimeBaseMode::Embedded);
if (!created) return 1;
if (!*created) return 0; // --help
auto app = std::move(*created);
auto processed = app->counter("signals_processed");
auto output = app->publish<Signal>("Nexigen/Example/Quickstart/Out");
app->subscribe<Signal>("Nexigen/Example/Quickstart/In", [&](const Signal& in) {
    static_cast<void>(output->publish_now(Signal{in.sequence, in.value * 2.0}, 0x2001U));
    processed->inc();
});
const auto reason = app->run();
const auto status = app->shutdown();
if (!reason || !status) return 1;
return nexigen::app::exit_code(*reason);
```

`--app-id` обязателен и приходит из `--app-id` или suite-манифеста; fallback на имя приложения убран. `Embedded` нужен только для standalone examples/tests; production использует `External` и опубликованную базу `nexigen-chronod`.

Готовый пример: `examples/app/ex_app_quickstart.cpp` — тело `main` не длиннее 30 строк. Запуск:

```bash
./ex_app_quickstart --app-id QSTRT
```

Запись через `nexigen-recd`:

```bash
nexigen-recd --session-root ./recordings \
  --service-metrics nexigen/metrics/stream \
  --service-traces nexigen/trace/stream \
  --event-service Nexigen/Example/Quickstart/In \
  --event-service Nexigen/Example/Quickstart/Out
```

## Порядок инициализации и остановки

Инициализация: **time base → логгер → метрики → трассировка → watcher**. Time base обязателен: без него файловый sink не получит UTC, а `run` не сможет проверить потерю базы. Логгер создаётся первым, чтобы последующие шаги могли сообщать о проблемах.

Остановка: **watcher → финальная запись в лог → `tracer.quiesce()` → `metrics.quiesce()` → `logger.flush()` → `logger.quiesce()` → `TimeBaseClock::detach()`**.

`shutdown()` не уничтожает провайдеры и sink'и: он останавливает их (quiesce), а память освобождает только деструктор `Infrastructure`. Поэтому выданные ранее хэндлы остаются memory-safe до конца `main`. После shutdown запись метрик принимается, но не собирается; спаны отвечают `NotInitialized`; записи лога дропаются; `meter()`/`tracer_for()` закрыты.

## Сигналы

`Infrastructure` блокирует `SIGINT`, `SIGTERM` и `SIGHUP` до старта воркеров и создаёт `signalfd`-watcher. Все три сигнала завершают приложение штатно: `run()` возвращает соответствующую причину, `shutdown()` дренирует телеметрию, а `app::exit_code` даёт 0. Потеря time base остаётся отдельным кодом 75. Только один экземпляр `Infrastructure` может быть инициализирован в процессе; второй `initialize` возвращает `ErrorCode::Conflict`. Это защищает от двух владельцев сигналов и двух наборов подсистем. `App` дополнительно ставит `SIGPIPE` в `SIG_IGN` на время жизни и восстанавливает прежнюю диспозицию после quiesce, чтобы запись в закрытый терминал/pipe возвращала `EPIPE`, а не убивала процесс.

## Диагностика переходов

Подсистемы не знают о логгере. Они сообщают о переходах через `core::DiagnosticBinding` (16 байт, borrowed, null = no-op): `DiagnosticSource`, `DiagnosticKind`, `ErrorCode`. Приложение подставляет `app::detail::DiagnosticForwarder<Logger>`. Эмиссия — только холодный путь и только на переход (`Started`, `Degraded`, `Recovered`, `Quiesced`). До появления логгера провал attach time base пишется `BootstrapDiagnostics` в stderr.

Runtime-пути не читают `CLOCK_REALTIME`: метрики резолвят UTC через `TimeBaseClock` и несут `utc_resolved`; trace-демон использует attached time base, а startup-offset — только как fallback, отчитываясь один раз.

## Связи

```text
main
  |
  +-- parse_command_line -> Settings
  +-- make_infrastructure<Policy>()
        |
        +-- chrono::TimeBaseClient/Clock
        +-- Policy::emplace_logger
        +-- Policy::emplace_metrics_sink
        +-- Policy::emplace_trace_sink
        +-- signalfd watcher
        +-- run(tick) / shutdown()
```

`app` — единственный владелец подсистем телеметрии. `exec` использует `app` для запуска приложений, но не тянет `com` в незагеймленное ядро. `hub` читает артефакты, которые создаёт приложение/EM.

## Контракты и границы

- `Infrastructure` создаётся только фабрикой `make_infrastructure`; конструктор приватный, автоматические/статические объекты невозможны.
- Один инициализированный экземпляр на процесс; второй — `Conflict`.
- Time base обязателен; без него инициализация падает до создания логгера.
- `shutdown()` идемпотентен; повторный вызов ничего не делает.
- Политика stateless; хуки `noexcept` и возвращают `core::Status`.
- `app` не зависит от `com`; COM-sink'и подключаются политикой через `nexigen_*_com`.
- `AppSettingsPaths` задаёт четыре пары global/local; пустая пара отключает источник.
- `RunIdentity` не угадывает значения из cwd/hostname/pid; только явные `NEXIGEN_*`.

## Capabilities

`application`, `command-line`, `settings`, `telemetry-layout`.

## Читать дальше

- [`docs/architecture.md`](../architecture.md), раздел 4.8 — полный порядок и диаграмма.
- [`platform.md`](platform.md) — место `app` в трёх плоскостях.
- [`core.md`](core.md), [`chrono.md`](chrono.md), [`logging.md`](logging.md), [`metrics.md`](metrics.md), [`tracing.md`](tracing.md).
- Спецификации: `openspec/specs/app/*`.
