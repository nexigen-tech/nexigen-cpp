# Design

## Context

См. `proposal.md` — Why. Текущее состояние, которое формирует решение:

- `Iceoryx2TraceSink` (`add-iceoryx2-trace-sink`) публикует `SpanFrame` 384 Б в сервис `nexigen/trace/stream`; события на каждый спан нет — контракт требует дренажа по интервалу 20–50 мс.
- PHM при аварии публикует `nexigen::sm::RecoveryNotification` (32-байтный function-group) в pub-sub канал `Nexigen/SM/Recovery` и будит `Nexigen/SM/RecoveryEvent`. SM уже подписан на этот канал; iceoryx2 допускает несколько подписчиков, поэтому второй слушатель не мешает SM.
- SM публикует состояния групп через `Field`-сервисы `Nexigen/SM/State/<group>`; это альтернативный источник триггера (см. D3).
- Существующий daemon-паттерн — `tools/nexigen-metricsd`: `WaitSet` (interval + notification), `ThreadSpec`, `std::signal`, конфиг JSON. Его можно переиспользовать структурно.
- `nexigen::exec::ApplicationStateReporter` даёт отчёт `Running` по каналу `Nexigen/EM/State/<app>`.
- Дампа в Perfetto пока нет: `add-perfetto-export` — следующий change.

## Goals / Non-Goals

**Goals:**

- Дать центральный flight recorder: последние секунды спанов в RAM и снимок по аварийному триггеру.
- Зафиксировать стабильный, версионированный формат снимка, пригодный для Perfetto-рендерера без переделки.
- Не влиять на hot path приложений и не писать на диск в штатном режиме.

**Non-Goals:**

- On-demand сессии и включение категорий в клиентах (TRCD_003), ftrace (004), `.pftrace` и flow-стрелки (005/006).
- Ручной dump-триггер через control-сервис/CLI, JSONL-рендер снимка, сжатие.
- Изменение `SpanFrame`, sink'а, каналов PHM/SM или `nexigen::com`.

## Decisions

### D1 — Один поток: WaitSet, interval + notification

Главный поток демона блокируется в `WaitSet::wait_and_process_once` с двумя вложениями (одна итерация = один пробуждённый источник; цикл `run` проверяет stop-флаг после каждой итерации, а `wait_and_process` не даёт такой границы, если источник продолжает срабатывать):

- `attach_interval(drain_interval_ms)` — плановый дренаж спанов (по умолчанию 20 мс).
- `attach_notification(recovery_listener)` — немедленное пробуждение по аварийному событию.

На каждом пробуждении выполняется один и тот же порядок: **сначала** дренировать спаны, **затем** забрать активации recovery-listener'а через `try_wait` (payload лежит в отдельном `Subscriber<RecoveryNotification>`), **затем** при наличии триггера записать снимок. `WaitSetAttachmentId` не используется для различения источников: metricsd-паттерн «дренаж на любом пробуждении + неблокирующий `try_wait`» проще и не зависит от типа вложения.

Дренаж allocation-free: `Subscriber::receive()` → `SpanRing::push` (копия 384 Б). Ошибка `receive()` логируется и прерывает текущий дренаж, но не цикл.

### D2 — Ring: pre-allocated массив, drop-oldest, без атомиков

```
SpanRing
  std::unique_ptr<SpanFrame[]> slots_   // capacity, выделен в start()
  std::size_t capacity_
  std::size_t head_      // следующий слот записи
  std::size_t count_     // заполнено, ≤ capacity_
  std::uint64_t overwritten_
```

`push(frame)` пишет `slots_[head_]`, двигает `head_` по модулю, инкрементирует `count_` до capacity; при полном кольце инкрементирует `overwritten_` (самый старый кадр теряется). Итерация дампа идёт от `oldest = (head_ + capacity_ - count_) % capacity_` по возрастанию.

Все операции выполняются в одном потоке (дренаж и дамп), поэтому **атомики и `std::memory_order` не вводятся**. Если позже дамп уедет в отдельный поток, потребуется snapshot/seqlock — отдельное изменение.

Память: `capacity × 384 Б`, выравнивание задаёт `alignas(64)` самого `SpanFrame`; массив выделяется один раз в `start()`. Ёмкость по умолчанию 65 536 записей ≈ 24 MiB (окно ~6.5 с при 10k спанов/с, ~1.3 с при 50k/с).

### D3 — Триггер: PHM→SM RecoveryNotification, не SM-поле

Подписка на `Nexigen/SM/Recovery` + listener на `Nexigen/SM/RecoveryEvent`.

Почему не `Nexigen/SM/State/<group>` Field: групп произвольное число, их имена и количество известны только из SM-манифеста; демону пришлось бы либо дублировать конфиг групп, либо заниматься discovery. Recovery-нотификация уже является прямым сигналом «авария, снимай дамп» (SRS TRCD_002) и несёт имя группы для заголовка снимка. SM остаётся единственным арбитром перехода в `SafeState`; трейсер только читает уведомление.

Несколько нотификаций в одном пробуждении схлопываются в один снимок; в заголовок попадает группа из последней.

### D4 — Бинарный снимок с версионированным заголовком

```
offset  size  field
  0      8    magic = "NXTRA001"
  8      2    version = 1
 10      2    header_size = 96
 12      4    flags (bit0 = triggered by recovery)
 16      8    frame_count
 24      8    first_start_ns (0 при пустом ring)
 32      8    last_end_ns    (0 при пустом ring)
 40      8    ring_overwrites
 48     32    trigger_group[32] (NUL-терминированный)
 80      8    trigger_ts_ns (монотонный)
 88      8    reserved
 96    ...    frame_count × SpanFrame (384 Б)
```

`static_assert(sizeof(TraceDumpHeader) == 96)` и `alignas(8)`; поля фиксированной ширины, указателей нет; кадры — те же `SpanFrame`, что и на шине.

Почему бинарный, а не JSONL: 24 MiB против ~60–90 MiB на eMMC, запись одним последовательным `write`, и это ровно тот вход, который нужен Perfetto-рендереру (`add-perfetto-export`). Человекочитаемый рендер — отдельный аддитивный change; тесты читают формат напрямую.

### D5 — Политика сброса: freeze, temp+rename, clear, no dump on SIGTERM

- Снимок пишется в `<name>.nxtr.tmp`, затем `fsync`, закрытие и `rename` в `<name>.nxtr`: частичный файл никогда не появляется под финальным именем.
- Во время записи (один поток) дренаж не выполняется; новые спаны остаются в очереди iceoryx2 и могут быть отброшены по `DiscardData`. Это осознанно: аварийный путь важнее непрерывности, а разрывы видны по `sequence`.
- Ring очищается (`head_=count_=0`, счётчик overwrites сохраняется) только после успешного `rename`.
- `SIGTERM`/`SIGINT` завершают цикл без снимка: штатная остановка не должна затирать аварийный дамп.
- Ошибка записи логируется, инкрементирует failure-счётчик, временный файл удаляется; демон продолжает дренаж.

### D6 — Ротация дампов

Имя: `incident_trace_<YYYYMMDDTHHMMSSZ>.nxtr` (UTC, лексикографически сортируется). При коллизии в одну секунду добавляется `-<n>` перед расширением.

После успешного `rename` демон сканирует каталог через `opendir/readdir` (проект не использует `std::filesystem`), отбирает `incident_trace_*.nxtr`, сортирует по имени и удаляет самые старые, пока число файлов не станет `max_dump_files`. Каталог создаётся при старте (`mkdir -p`-семантика по компонентам).

### D7 — Lifecycle, affinity, mlockall, readiness

Порядок старта:

1. Валидация конфига.
2. `mkdir` каталога дампов, выделение ring.
3. `mlockall(MCL_CURRENT | MCL_FUTURE)` — **best-effort**; отказ (`EPERM`/`ENOMEM`) печатается в stderr, старт продолжается (в CI нет `CAP_IPC_LOCK`).
4. `Node`, `Subscriber<SpanFrame>`, `Subscriber<RecoveryNotification>`, `EventListener`, `WaitSet`, `ApplicationStateReporter`.
5. `core::apply_thread_spec_self(cfg.bg_cpu_affinity, report)`.
6. `report(AppState::Running)` — до входа в цикл.
7. Цикл `wait_and_process`; по `SIGTERM` — выход с кодом 0.

Поток один; отдельного dump-потока нет (см. D5).

### D8 — Сборка и размещение

- Новый tool `nexigen-traced` строится только при `NEXIGEN_ENABLE_COM=ON` и линкует `nexigen_com nexigen_tracing nexigen_chrono Threads::Threads`; payload `nexigen::sm::RecoveryNotification` header-only, линковка `nexigen_sm` не нужна.
- `TraceDaemonConfig` — публичный заголовок `include/nexigen/tracing/trace_daemon_config.hpp` (+ cold-path JSON в `src/tracing/`).
- `SpanRing` и `TraceDumpHeader`/writer — внутренние `include/nexigen/tracing/detail/`.
- `detail::TraceDaemonSession` — тестируемое ядро демона (порты, ring, WaitSet, `wait_and_process`, `run(stop)`, счётчики), а `tools/nexigen-traced/main.cpp` остаётся тонким CLI: аргументы, конфиг, сигналы, запуск сессии. Это позволяет e2e-тесту гонять реальный цикл в процессе, как `metrics_daemon_test` гоняет detail-хелперы.
- Тест `tests/tracing/trace_daemon_test.cpp` под COM-гейтом, отдельный executable `nexigen_trace_daemon_tests`.

### D9 — Бюджеты

- Дренаж: один `receive` + копия 384 Б на спан; при 10 000 спанов/с и 20 мс — ~200 кадров за пробуждение; аллокаций нет.
- Ring: 24 MiB pre-allocated; страницы не трогаются, пока ring не заполнится.
- Снимок: один последовательный `write` ~24 MiB + `fsync`; cold path, допустимы аллокации и системные вызовы. Занимает демон на время записи; при eMMC это сотни миллисекунд.
- Диск в штатном режиме: 0 байт/с; запись только по триггеру.

## Risks / Trade-offs

- **[Спаны теряются во время записи снимка]** → один поток и синхронная запись. Митигация: снимок ограничен 24 MiB, потери видны по `sequence`; фоновый dump можно добавить отдельным change'ем.
- **[Окно ретенции зависит от частоты]** → 65 536 записей ≈ 6.5 с при 10k/с и ~1.3 с при 50k/с. Митигация: ёмкость конфигурируемая; при устойчивых >20k/с — lean-формат или больше RAM.
- **[Бинарный снимок не читается без инструмента до A3]** → тесты читают его; Perfetto-рендер — следующий change. Митигация: заголовок self-describing, version/magic позволяют отвергать чужой формат.
- **[`mlockall` может быть запрещён]** → best-effort, старт не падает; в лог/stderr попадает предупреждение.
- **[Имена recovery-каналов разъедутся с PHM/SM]** → имена вынесены в конфиг с текущими значениями по умолчанию; e2e-тест публикует в сконфигурированные имена.
- **[Pruning удалит полезный старый дамп]** → лимит конфигурируемый (по умолчанию 8), удаляется самый старый по UTC-имени.
- **[Коллизия имён в одну секунду]** → суффикс `-<n>` перед расширением; сортировка остаётся по времени.

## Migration Plan

- Аддитивно: новый tool, новый конфиг, новые detail-заголовки, новый тест. Существующие бинарники и API не меняются.
- Порядок: config + ring + dump writer → daemon loop → recovery trigger → pruning/lifecycle → CMake/tool → тесты/доки.
- Откат: удалить tool/заголовки/тест; на диске остаются только созданные дампы (их можно удалить вручную).
- Формат снимка версионирован (`version = 1`); до появления рендерера он считается внутренним контрактом демона и A3.

## Open Questions

- Нужен ли ручной dump-триггер через control-сервис до появления `add-trace-session-control`? Сейчас триггер только аварийный; тесты публикуют RecoveryNotification напрямую. Решается в A2b, формат снимка от этого не зависит.
- Нужно ли сжимать снимки (`zstd`) на eMMC? Отложено до измерений реального объёма; заголовок и версия позволяют добавить флаг позже.
