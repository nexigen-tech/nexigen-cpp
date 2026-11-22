# `phm`: здоровье и восстановление

`nexigen::phm` — Platform Health Management, замкнутый контур «обнаружение → реакция». Он не управляет состояниями и не запускает процессы; он наблюдает за здоровьем сущностей, проверяет их идентичность, пишет отказы и инициирует recovery через EM или watchdog.

Модуль собирается только при `NEXIGEN_ENABLE_COM=ON` и работает поверх `com`. Словари `sm` (`group_state_payload`, `recovery_notification`) используются на уровне заголовков, потому что PHM и SM обмениваются этими сообщениями.

## Ответственность

- Декларативное описание наблюдаемых сущностей: `PhmManifest`.
- Супервизия: `Supervisor` — по одной headless-машине на процесс.
- Клиентская сторона: `CheckpointEmitter`, `HealthChannel`, `SupervisedEntity` — checkpoints, отчёты о здоровье, чтение состояния Function Group, периодическая публикация claim.
- Идентичность: `ClaimPublisher`, `ClaimRegistry`, `ClaimVerdict`, `SecurityEvent` — проверка `{name, pid, uid}` через `/proc`.
- Recovery: `RecoveryClient`, `RecoveryRequest`, `RecoveryResponse`, `RecoveryAction` — `RestartProcess` (через EM, без расхода retry-бюджета) и локальный `TriggerWatchdogReset`.
- Отказы: `FaultStorage`, `FaultKind`, `verify_fault_log`/`FaultLogReport` — неблокирующая запись с монотонным номером и CRC-32C.
- Watchdog: `Watchdog`, `WatchdogDevice`, `PosixWatchdogDevice`, `WatchdogState`, `WatchdogConfig` — `/dev/watchdog`, период кормления не более половины таймаута, magic close при остановке.
- Логические fork/join: `LogicalFork`, `EmLifecycleState`, `SupervisionStatus` — приостановка супервизии на время штатной остановки.

## Ключевые типы

| Тип | Роль |
|---|---|
| `PhmManifest` | декларация сущностей и правил |
| `Supervisor` | headless-машина супервизии на процесс |
| `CheckpointEmitter` / `HealthChannel` | клиентские checkpoints и health |
| `SupervisedEntity` | фасад клиента PHM |
| `ClaimPublisher` / `ClaimRegistry` / `ClaimVerdict` | identity claims |
| `SecurityEvent` | событие чужого/неизвестного claim |
| `RecoveryClient` / `RecoveryRequest` / `RecoveryResponse` | recovery-запросы |
| `RecoveryAction` | `RestartProcess` / `TriggerWatchdogReset` |
| `FaultStorage` / `FaultKind` | журнал отказов |
| `verify_fault_log` / `FaultLogReport` | проверка целостности журнала |
| `Watchdog` / `WatchdogDevice` / `WatchdogConfig` | аппаратный watchdog |
| `LogicalFork` / `EmLifecycleState` | логические регионы и пауза супервизии |
| `SupervisionStatus` | состояние супервизии |

## Три режима супервизии

- **Alive** — счётчики за окно и допуски: сущность должна подавать признаки жизни.
- **Logical** — граф разрешённых переходов и fork/join: проверяется не только «жив», но и «в правильном ли состоянии».
- **Deadline** — пары source→target: событие должно произойти до дедлайна.

Толерантность — `Ok → Failed → Expired`. Все горячие пути O(1) по размеру манифеста, без выделений и блокировок: Supervisor не должен становиться узким местом.

## Recovery

При `Expired` PHM выполняет настроенное правило:

- `RestartProcess` — через `Nexigen/PHM/Recovery` в EM; это не расходует retry-бюджет супервизора.
- `TriggerWatchdogReset` — локальный сброс через watchdog.
- SafeState-команда в SM — если правило требует не рестарта, а безопасного состояния.

Watchdog по умолчанию отключён. Если включён, кормление идёт с периодом не более половины таймаута, а при остановке выполняется magic close, чтобы устройство не сбросило систему на штатном выходе.

## Идентичность

`ClaimPublisher` публикует `{name, pid, uid}`; `ClaimRegistry` проверяет claim через `/proc`. Чужой или неизвестный claim порождает `SecurityEvent`. Checkpoints принимаются только после проверки claim: иначе процесс мог бы выдать себя за другую сущность.

## Отказы

`FaultStorage` пишет отказы неблокирующе: монотонный номер, `FaultKind`, CRC-32C. `verify_fault_log`/`FaultLogReport` различают целые, повреждённые, усечённые и legacy записи; `nexigen-phmd --verify-fault-log` — отдельный режим проверки.

## Связи

```text
application
  SupervisedEntity -> CheckpointEmitter/HealthChannel -> Nexigen/PHM/*
  ClaimPublisher --------------------------------------> ClaimRegistry
        |
        v
nexigen-phmd: Supervisor -> Ok/Failed/Expired
        |                         |
        |                         +--> RecoveryClient -> EM (RestartProcess)
        |                         +--> SM (SafeState)
        +--> Watchdog -> /dev/watchdog
        +--> FaultStorage -> fault log
```

`phm` зависит от `com`; с `exec` и `sm` общается сообщениями. `exec` публикует `Running`/`Terminating`/`Terminated`, чтобы PHM приостанавливал супервизию на время штатной остановки.

## Контракты и границы

- Модуль доступен только при `NEXIGEN_ENABLE_COM=ON`.
- Супервизия O(1) по размеру манифеста; без выделений и блокировок на горячем пути.
- Checkpoints принимаются только после проверки identity claim.
- Watchdog отключён по умолчанию; feed ≤ половины таймаута; magic close при остановке.
- Fault-журнал — с монотонным номером и CRC-32C; проверка различает целые/повреждённые/усечённые/legacy записи.
- Recovery не обходит state machine: `RestartProcess` идёт через EM, SafeState — через SM.
- PHM не управляет процессами напрямую.

## Capabilities

`health-channel`, `identity`, `manifest`, `recovery`, `supervised-entity`, `supervision`, `watchdog`.

## Читать дальше

- [`docs/architecture.md`](../architecture.md), раздел 4.11 — полная карта типов и сценариев.
- [`platform.md`](platform.md) — место `phm` в плоскости управления.
- [`exec.md`](exec.md), [`sm.md`](sm.md) — исполнение recovery и SafeState.
- [`com.md`](com.md) — транспорт health/claims.
- Спецификации: `openspec/specs/phm/*`.
