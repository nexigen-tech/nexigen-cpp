# `exec`: жизненный цикл suite и приложений

`nexigen::exec` — управляющая плоскость L3. Он отвечает за то, какие процессы должны работать, как они запускаются, как подтверждают готовность, как останавливаются и что остаётся на диске после запуска. Это не runtime-библиотека для такта; это Execution Manager (EM), который живёт рядом с приложениями и управляет ими через манифесты, процессы и сообщения.

Модуль зависит от `core` и `app`. Его незагеймленное ядро `nexigen_exec` не содержит COM-символов; `state_client.cpp`, `control_host_com.cpp` и `recovery_handler.cpp` вынесены в отдельные цели, подключаемые только при `NEXIGEN_ENABLE_COM=ON`.

## Ответственность

- Манифесты suite: `SuiteManifest`, `AppSpec`, `FunctionGroupConfig`, `initial_state`; `ManifestAccessor` определяет JSON/ARXML и ведёт единый путь `from_json` + `validate_suite_manifest`; `MachineManifest` кросс-проверяет ссылки на процессы машины.
- Идентичность приложения: `AppSpec::app_id` — 1–16 символов `A-Z`, `a-z`, `0-9`, `_`, `-`; пустое значение означает `app`; дубликаты внутри suite и невалидный формат отклоняются при загрузке манифеста (`InvalidArgument`), имя длиннее 16 байт не обрезается. Разные suite могут переиспользовать код. Разрешённый id передаётся ребёнку как `--app-id` рядом с `--app-name`.
- Запуск и остановка: `ExecutionManager` — stateless-контроль одного instance через его директорию; `start` создаёт layout, генерирует настройки, делает `fork`/`setsid`/`execvp` и пишет `resource.json`; `stop`/`status` работают только с диском.
- Detached-супервизор: `start_suite`/`stop_suite`, `ChildProcess`, `ExecutionStateTracker`; readiness каждого процесса и результаты переходов.
- Модель процессов: `ProcessType` (`daemon`/`oneshot`), `ProcessState`, `AppState`, `ApplicationStateReporter`, `SuiteState`, `SuiteStatus`, `UnrecoverableState`.
- Function Groups: `FunctionGroupRegistry`, `StateCommand`/`StateAck`/`StateResult`, `StateClient`, `state_dependent_startup_config`, `request_id`, access control по `function_group_port_mapping`.
- Артефакты: `SuitePaths`, `InstanceLayout`, `InstanceSettings`, `InstanceFileIO`, `SuiteMetadata`, `SuiteAppMetadata`, `SuiteExit`, `ResourceMetadata`, `ExitMetadata`, run-лог.
- Platform controls: `ResourceGroupConfig` (RLIMIT_AS, cgroup v2 CPU/memory, CPU set, working folder), `cgroup_budget`, `sha256_file`/`executable_digest`, `SettingsDigest`, `TrustedPlatformConfig`, `uid`/`gid`, сброс `CAP_SYS_NICE`.
- `DeterministicClient` — фиксированный такт, seeded splitmix64 и работа без выделений для одного execution-потока.
- `RootFG`/`RootStateQuery` — словарь имён и состояний корневой Function Group и payload результата начального перехода.

## Ключевые типы

| Тип | Роль |
|---|---|
| `ExecutionManager` | stateless-контроль instance через директорию |
| `SuiteManifest` / `AppSpec` / `FunctionGroupConfig` | декларативное описание suite |
| `start_suite` / `stop_suite` | detached-супервизор |
| `ChildProcess` | fork/exec/setsid и остановка ребёнка |
| `ExecutionStateTracker` | readiness и состояние процессов |
| `ProcessType` / `ProcessState` / `AppState` | модель процесса и приложения |
| `SuiteState` / `SuiteStatus` / `UnrecoverableState` | состояние suite |
| `ManifestAccessor` / `MachineManifest` | разбор и кросс-проверка манифестов |
| `ControlHost` | COM-свободная граница супервизора |
| `StateClient` / `FunctionGroupRegistry` | приём `SetState` и разрешение в процессы |
| `StateCommand` / `StateAck` / `StateResult` | протокол перехода |
| `ResourceGroupConfig` / `cgroup_budget` | лимиты и cgroup v2 |
| `TrustedPlatformConfig` / `SettingsDigest` | безопасность запуска |
| `DeterministicClient` | детерминированный такт |
| `RootFG` / `RootStateQuery` | корневая группа |

## Как проходит запуск

```text
Suite manifest (JSON/ARXML)
        |
        v
ManifestAccessor -> SuiteManifest::validate -> preflight (CPU/priorities)
        |
        v
Detached suite supervisor
        |
        +-- ControlHost (COM boundary)
        +-- StateClient + readiness tracker
        +-- ChildProcess на приложение: setsid -> fork -> exec
        |
        v
suite-state.json / suite.json / suite-exit.json / run log
```

1. `nexigen-exec start --config` или `nexigen-em --config` загружают манифест, проверяют его и при необходимости сверяют с Machine Manifest.
2. Запускается detached-супервизор; при COM-сборке он устанавливает `ControlHost` и создаёт `StateClient` с трекером готовности.
3. Супервизор входит в `initial_state` через тот же движок переходов, что и любой `SetState`, а не прямым циклом запуска.
4. `daemon`-процессы обязаны сообщить `Run`, `oneshot` — завершиться с кодом 0; только после этого переход подтверждается.
5. `stop` останавливает приложения с grace-периодом, затем супервизор фиксирует `suite-exit.json` и причину остановки.

## Связь с SM и PHM

`nexigen-sm` отправляет `SetState` в `StateClient`; EM разрешает состояние в набор процессов через `FunctionGroupRegistry` и выполняет stop/restart/start через `ChildProcess`. Результат перехода возвращается как `StateAck`/`StateResult`, а SM публикует состояние и audit record.

`nexigen-phmd` при `Expired` может вызвать `RestartProcess` через EM или `TriggerWatchdogReset`; это не обходит state machine, а входит в неё как recovery-запрос. EM также публикует `Running`/`Terminating`/`Terminated` в `Nexigen/PHM/ExecutionState`, чтобы PHM приостанавливал супервизию на время штатной остановки.

## Детерминизм и безопасность

`DeterministicClient` задаёт фиксированный такт, seeded splitmix64 и работу без выделений для одного execution-потока. Это не «детерминизм всей системы»: EM гарантирует только тот контур, который явно объявлен детерминированным.

Перед `fork`/запуском проверяются SHA-256 манифестов, исполняемых файлов и настроек. `trusted_platform` требует абсолютных команд; `require_non_root` и `uid`/`gid` сбрасывают привилегии до `exec`; `CAP_SYS_NICE` сбрасывается, если не разрешён явно. Ошибка — `SecurityError`, запуск не продолжается.

## Горячий и холодный путь

`exec` — целиком control plane: манифесты, fork/exec, файлы, cgroup, readiness. Здесь допустимы аллокации и системные вызовы. `DeterministicClient` — отдельный hot-path контур для execution-потока; он не аллоцирует и не блокируется.

## Контракты и границы

- `nexigen_exec` не содержит COM-символов и собирается при выключенном gate.
- COM-части (`StateClient`, `ControlHost`, `RecoveryHandler`) — отдельные цели для инструментов.
- `ExecutionManager` stateless: состояние живёт на диске, а не в объекте.
- `daemon` подтверждает `Run`, `oneshot` — exit 0; иначе переход не подтверждён.
- Остановка — grace-период, затем `SIGTERM` → `SIGKILL`.
- `StateClient` проверяет access control по `(requester, Function Group)` и отменяет устаревшие `request_id`.
- `ResourceGroupConfig` применяет RLIMIT_AS, cgroup v2 CPU/memory, CPU set и working folder; ошибка обязательного лимита останавливает запуск.
- Артефакты запуска — часть контракта: `suite.json`, `suite-state.json`, `suite-exit.json`, `resource.json`, run-лог.

## Capabilities

`application-state`, `app-suite`, `deterministic-execution`, `function-group-state`, `instance-lifecycle`, `logging`, `machine-manifest`, `manifest-accessor`, `process-lifecycle-events`, `process-supervision`, `process-types`, `resource-groups`, `root-function-group`, `state-client`, `state-client-access-control`, `state-request-ids`, `trusted-executable`, `update-state`.

## Читать дальше

- [`docs/architecture.md`](../architecture.md), раздел 4.9 — полная карта типов и сценариев.
- [`platform.md`](platform.md) — место `exec` в плоскости управления.
- [`sm.md`](sm.md), [`phm.md`](phm.md) — соседние узлы контура.
- [`app.md`](app.md) — как EM запускает приложение.
- Спецификации: `openspec/specs/exec/*`.
