# Proposal

## Why

Gap-анализ AUTOSAR AP SWS PHM (R24-11) показал: петля безопасности PHM → SM → EM
разорвана в трёх местах, и каждый разрыв срабатывает на первом же реальном отказе.

- **PHM → SM**: эскалация адресуется именем процесса вместо имени Function Group,
  поэтому SM молча отбрасывает уведомление (`recovery.hpp:204,239` →
  `recovery_handler.hpp:97-104`). Даже если бы адрес был верным, SM переводит только
  собственную модель: `StateManager::force_state` пишет `state_index` и ничего больше —
  ни публикации, ни команды EM (`state_manager.cpp:187-192`). `SafeState` номинален.
- **PHM → EM**: PHM ждёт ответ 500 мс (`tools/nexigen-phmd/main.cpp:547`), а EM
  перезапускает синхронно с grace до 5 с (`suite_supervisor.cpp:772`). Ответ почти
  всегда не успевает, PHM фиксирует ложный «failed recovery» и эскалирует повторно.
- **Исход recovery**: если перезапуск провалился, PHM об этом не узнаёт — EM публикует
  `Terminated`, что PHM отображает в `Ok`. Процесс мёртв и «здоров» навсегда.

Единственный работающий механизм эскалации сегодня — остановка watchdog на любом
`Expired` (`main.cpp:580-589`). Он несущий, поэтому его нельзя приводить к AUTOSAR
раньше, чем появится настоящий SM-путь.

## What Changes

- **BREAKING** `RecoveryRequest` получает `request_id`; `RecoveryResponse` заменяет
  `accepted: uint32` на `outcome` (`Accepted`/`InProgress`/`Rejected`) плюс `reason`
  и `request_id`. EM отвечает сразу («принято»), перезапуск выполняет асинхронно.
- **BREAKING** `ProcessLifecycleEvent` получает `function_group` и новое состояние
  `Failed`, публикуемое когда бюджет попыток перезапуска исчерпан.
- **BREAKING** `sm::RecoveryNotification` получает `request_id`; SM публикует новое
  событие `sm::RecoveryAck` с `request_id`, `outcome`, `reason`.
- SM применяет recovery-уведомление полностью: `force_state` + публикация состояния
  группы + постановка перехода и команда EM через emergency-канал.
- PHM адресует эскалацию Function Group процесса, полученной из lifecycle-события;
  молчаливый отброс при неизвестной группе запрещён — отказ выражается причиной.
- PHM ждёт `RecoveryAck` в пределах `recovery_timeout`; отсутствие ack, `Rejected`
  или `InProgress` дольше бюджета приводят к остановке feed и `FireWatchdogReaction`.
- **BREAKING** `SupervisionStatus::Stopped` переименовывается в `Exhausted` и
  становится достижимым: терминальное состояние «бюджет восстановления исчерпан».
- EM списывает бюджет `restart_attempts` и на PHM-инициированных перезапусках
  (`suite_restart_process` перестаёт обходить бюджет).

## Capabilities

### New Capabilities

Нет. Изменения ложатся в существующие возможности.

### Modified Capabilities

- `phm/recovery`: адресация эскалации по Function Group, ожидание ack с бюджетом,
  словарь причин отказа, семантика «разослано ≠ восстановлено».
- `phm/supervision`: терминальное состояние `Exhausted`, отображение lifecycle-события
  `Failed` в него, сохранение различия «приостановлено» и «исчерпано».
- `phm/manifest`: параметр `recovery_timeout` для ожидания ack.
- `exec/process-lifecycle-events`: `function_group` и состояние `Failed` в событии.
- `exec/process-supervision`: асинхронный перезапуск, причина отказа в ответе,
  списание бюджета попыток на PHM-перезапусках.
- `sm/recovery-handler`: фактическая активация SafeState (публикация + команда EM)
  и ответное ack-событие.
- `sm/ipc`: канал `RecoveryAck` и его wake-событие.

## Impact

**Код**: `include/nexigen/phm/{recovery,recovery_request,recovery_response,recovery_action,supervision_status}.hpp`,
`include/nexigen/sm/{recovery_handler,recovery_notification,state_publication}.hpp`,
`include/nexigen/exec/process_lifecycle.hpp`, `include/nexigen/exec/recovery_handler.hpp`,
`src/phm/*`, `src/sm/*`, `src/exec/{recovery_handler,control_host_com,suite_supervisor}.cpp`,
`tools/nexigen-phmd/main.cpp`, `tools/nexigen-sm/main.cpp`.

**Производительность и память**: горячий путь мониторинга не меняется — правки в
`Supervisor` касаются только переименования перечислителя и одного перехода по
lifecycle-событию, то есть O(1) без выделений. Сообщения остаются trivially copyable
POD и едут zero-copy: `ProcessLifecycleEvent` растёт на 32 байта (64 → 96),
`RecoveryRequest` — на 8 байт (72 → 80), `RecoveryResponse` — на 12 байт,
`RecoveryNotification` — на 8 байт, новый `RecoveryAck` — 16 байт. Ни одно
сообщение не пересекает границу кэш-линии в горячем пути. Добавляется одно чтение
`Field` на тик в PHM (только при ожидании ack) и одна публикация `Field` в SM на
fault-событие; ни то, ни другое не находится в hot path супервизии. Худшая
дополнительная задержка реакции SM — один вызов emergency-канала EM, который
выполняется в том же WaitSet-callback, что и обработка уведомления.

**Совместимость**: все четыре сообщения меняют wire-формат, поэтому PHM, EM и SM
должны пересобираться вместе. Внешние потребители `phm/recovery` и `sm/ipc`
затрагиваются.
