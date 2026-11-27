# Deferred: отложенные работы платформы (не забыть доделать)

Зафиксировано 2026-11-10 после аудита `initial_spec/exec/execution_manager.md`
и SDV-архитектуры. Всё ниже — осознанно отложено; снимать пункт можно только
отдельным OpenSpec-change (или пометкой «не актуально» с причиной).

## EM — продакшн-харденинг (отложено до этапа production hardening)

- [x] **REQ_EM_009 — Least Privilege (setuid/setgid).** Реализовано в
  `em-least-privilege`: `uid`/`gid` в записи приложения, сброс групп, `setgid`,
  `setuid` и проверка дропа перед `exec`; `uid`/`gid == 0` запрещены;
  `trusted_platform.require_non_root` отказывает запуску под root без явного `uid`.
- [x] **REQ_EM_009b — CAP_SYS_NICE (bounding set).** Реализовано в
  `cap-sys-nice`: по умолчанию ребёнок удаляет `CAP_SYS_NICE` из capability
  bounding set перед `exec`; `allow_rt_priority: true` оставляет её. Осталось:
  полноценная политика permitted/effective/ambient (нужен `libcap` или
  `capget/capset`) — отдельная задача.
- [ ] **REQ_EM_010 — Authentic Execution.** Частично: SHA-256-верификация
  исполняемого файла (`StartOptions::expected_executable_digest`), suite-манифеста
  (`expected_sha256_hex`) и settings-файлов (`settings_digests`) блокирует запуск
  с `SecurityError` при несовпадении. Проверки включаются только при заданных
  дайджестах. Осталось: криптографическая подпись/корень доверия и критическое
  событие в security-логе (сейчас ошибка уходит в `stderr`/`StartReport`).

## SM — интеграция с UCM (отложено до появления Update Manager)

- [ ] **REQ_SM_008 — OTA/UCM coordination.** Проверка безопасных условий и перевод
  обновляемой Function Group в `Update` с блокировкой прикладных процессов.
  Менеджер обновлений не существует; интерфейс проектировать при его появлении
  (зафиксировано в design add-sm Non-Goals).

## Манифесты — YAML-вход (отложено из-за mandate 12)

- [ ] **YAML-поддержка манифестов** (`phm`, `sm`, `exec`). Спеки говорят
  «YAML/JSON», реализация — JSON-only (nlohmann, mandate 12 запрещает новые
  JSON-библиотеки). YAML-конвертер — будущий аддитивный change; канонический
  формат остаётся JSON.

## Переименование под SRS-номенклатуру (planned)

- [ ] **`nexigen::logging` → `nexigen::log`, `nexigen::tracing` → `nexigen::trace`**,
  плюс макросы `NEXIGEN_TRACE_*` → `nexigen_TRACE_*` (и `nexigen_TRACE_NEW_ROOT`).
  Решение 2026-11-11: текущие имена оставлены, переименование — отдельный
  BREAKING-change (обновить include-пути, namespace, CMake-таргеты, спеки/примеры).

## SM — event-нотификация смены состояния (future)

- [ ] **Event-нотификация `StatePublication`**: сейчас SM публикует состояния
  Function Groups только через Field (`history_size=1`); приложения читают
  значение в своём цикле (`SupervisedEntity::current_group_state()`).
  Задел: добавить `com::EventNotifier` в `StatePublication::publish`, чтобы
  подписчики просыпались без polling (REQ_APP_016, «no polling»).


## Контекст аудита EM (2026-11-10)

Полный статус REQ_EM_* — в TODO.md не дублируется; на 2026-11-15: реализованы
001 (dependencies), 003, 004, 005 (Function Groups config) и 005X-logging/metrics;
частично — 002, 006, 007, 008, 010 (SHA-256 для executable/manifest/settings;
нет подписи и security-лога), 011, 012, 014; 009 закрыт для setuid/setgid и
bounding-set для `CAP_SYS_NICE`; осталась полная capability-политика.
Ближайшая волна по недостающему — safety-приоритеты (007/014) и оркестрация
(011/012) — отдельными change'ами.
