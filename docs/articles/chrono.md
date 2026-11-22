# `chrono`: время как платформенный сервис

`nexigen::chrono` отвечает на вопрос «который сейчас час» так, чтобы ответ был пригоден для реального времени. Это не обёртка над `clock_gettime`: модуль строит единую монотонную шкалу, калибрует TSC, публикует карту доменов через shared memory и следит за тем, чтобы UTC не оказался выдуманным.

Модуль — слой L1: он зависит только от `core` и не зависит от `com`. `nexigen-chronod` собирается напрямую на `chrono`, без `app::Infrastructure`, и работает при выключенном `NEXIGEN_ENABLE_COM`.

## Ответственность

- Монотонные наносекунды (`Timestamp`), гражданское время (`CivilTime`) и UTC-смещение (`utc_offset`).
- Калиброванный hot-path источник штампа (`StampSource`): `__rdtsc()` плюс зафиксированная при калибровке пара `(tsc_anchor, mono_anchor)` и fixed-point множитель; периодическая перекалибровка, монотонный clamp и безопасный откат на `Timestamp::now()` при ошибке или на не-x86.
- `Provider<MaxSources>` — «беспоточный» авторитет времени: выдаёт stamper'ы, снимает UTC-смещение на холодном пути и доставляет снимки в sink. `Worker` — опциональная демон-обёртка для периодических снимков.
- Time base: таблица доменов `TimeBaseBlock`, строки `DomainCoefficient`, снимок `TimeSnapshot`, оценка и публикация (`TimeBaseEstimator`, `TimeBasePublisher`), клиентская сторона (`TimeBaseClient`, `TimeBaseClock`).
- `nexigen-chronod` — обязательный демон, который формирует шкалу, публикует блок в POSIX shared memory и ведёт `chrono.jsonl`.
- `TimeSyncMonitor` — обнаружение шага, обратного хода и превышения скорости системных часов.

## Ключевые типы

| Тип | Роль |
|---|---|
| `Timestamp` | наносекунды монотонного времени |
| `CivilTime`, `utc_offset` | гражданское время и преобразование монотонного штампа в UTC |
| `TimeDomainClass` | `system_clock`, `cycle_counter`, `device_clock` |
| `TimeDomain` | `monotonic_raw` (хаб), `monotonic`, `tai`, `realtime`, `host_counter`, аппаратные домены |
| `StampSource` | hot-path TSC-штамп с калибровкой и clamp |
| `Provider<MaxSources>` | фиксированный банк источников времени |
| `TimeBaseBlock` | 5120-байтовый seqlock-блок: заголовок, 32 строки, снимок |
| `DomainCoefficient` | 128-байтовая строка домена: Q32-коэффициенты, интервалы, статус, эпоха |
| `TimeSnapshot` | 896-байтовый снимок 1 Гц: значения, epoch, lock state, флаги |
| `TimeBaseEstimator` / `TimeBasePublisher` | оценка карты домена и публикация 10 Гц |
| `TimeBaseClient` / `TimeBaseClock` | клиентская привязка к опубликованной базе |
| `TimeSyncMonitor` | классификация `Ok`/`Stepped`/`GoingBackwards`/`RateExceeded` |
| `nexigen-chronod` | демон time base и `chrono.jsonl` |

## Раскладка time base

`TimeBaseBlock` — 5120 байт:

| Смещение | Размер | Поле |
|---:|---:|---|
| 0 | 24 | `magic`/`version`/`capacity`/`domain_count` |
| 24 | 40 | `generation` и `status`/флаги |
| 64 | 64 | `heartbeat_ns` (отдельная cache line) |
| 128 | 4096 | 32 × `DomainCoefficient` (128 байт) |
| 4224 | 896 | `last_snapshot` (`TimeSnapshot`) |

Хаб — `CLOCK_MONOTONIC_RAW` (`monotonic_raw`, identity). Системные строки — `monotonic`, `tai`, `realtime`; TSC — `host_counter`; аппаратные таймеры — `device_domain_base + index`. Каждая строка несёт прямые и обратные Q32-коэффициенты, неопределённость, статус блокировки, эпоху и интервалы валидности в обеих шкалах. Блок читается через seqlock без блокировки писателя; транспорт (`TimeBaseSegment`, seqlock) живёт в `chrono::detail`, потому что типизирован `TimeBaseBlock` и не может быть перенесён в `core` без обратной зависимости.

`TimeSnapshot` публикуется на 1 Гц и содержит поколение карт, boot token, `sync_mode`, `ServiceFlags`, системные инстанты, смещения TAI/UTC и epoch/value/lock каждого домена. Таблицу коэффициентов он не дублирует: читатель сопоставляет снимок с `TimeBaseBlock` по `generation`.

## Горячий и холодный путь

Горячий путь — `StampSource::sample()`: чтение TSC, fixed-point преобразование, clamp. Ни аллокаций, ни системных вызовов, ни общих атомиков на установившемся пути. Калибровка — холодная: она делает паузы и фиксирует пару якорей.

Публикация time base — тоже холодный путь: `nexigen-chronod` снимает домены на 10 Гц, оценивает карты и публикует изменения; снимок — 1 Гц. Приложения не создают `Provider` и не снимают UTC-смещение самостоятельно: они прикрепляются к опубликованной базе.

## Синхронизация и режимы

`nexigen-chronod` поддерживает три режима:

- `Standalone` — мастер локальный осциллятор/RTC;
- `PtpSynced` — внешний IEEE 1588 GM через PHC сетевой карты; `ptp4l`/`phc2sys` остаются внешними службами, демон их не запускает и не ищет;
- `Holdover` — lock потерян, используется последняя модель дрейфа до истечения окна.

`ServiceFlags` отражают наблюдаемое состояние (`phc_present`, `tai_available/synchronized`, `host_counter_*`, `ptp_stack_ready`, `step_detected`, `slew_active`, `rt_sched_applied`, `mlock_applied`, `declared_domains_complete`), а не наличие процессов. При потере обязательного домена приложение уходит в holdover не дольше двух секунд, затем в `time_base_lost`; самостоятельных ретраев нет.

Аппаратные домены:

- `phc_cross_timestamp` — `PTP_SYS_OFFSET_PRECISE` через `/dev/ptpN`;
- `register_pair` — контракт D1–D3: двойное чтение `pair_sequence`, CRC-32C по D1+D2, отбрасывание torn/stale пар; при `simulate: true` поднимается simulated-устройство;
- `manual_pair` — счётчик и `CLOCK_MONOTONIC_RAW` читаются программно, окно чтения становится неопределённостью.

У каждого объявленного домена свой backend, оценщик, эпоха, строка и история. Отсутствующий объявленный домен переводит статус в `Degraded`.

## Связи

```text
nexigen-chronod
  Provider -> UTC offset -> TimeBasePublisher -> TimeBaseBlock (shared memory)
                                                   |
                    +------------------------------+----------------------+
                    |                              |                      |
             app::Infrastructure            logging/file sink       recorder
             TimeBaseClient/Clock           UTC from base           TimeBaseSessionBridge
```

- `app` прикрепляет time base до создания логгера, метрик и трейсинга; без неё инициализация падает.
- Файловые sinks берут UTC из `TimeBaseClock`, а не из локальных часов; если калибровка не покрывает instant, поле `timestamp` равно `null`, а `mono_ns`/`boot` остаются достоверными.
- `recorder` пишет `TimeBase`/`TimeSnapshot` через `TimeBaseSessionBridge`; `nexigen-export` разрешает instant-ы без демона.
- `synchro` использует `CLOCK_MONOTONIC_RAW` для абсолютных дедлайнов фазы.

## Контракты и границы

- 32 домена в блоке; каждая строка 128 байт; снимок 896 байт.
- Хаб — `monotonic_raw`; любое преобразование идёт через него.
- `TimeBasePayload` в сессии несёт layout-магию `TBP2`; payload без неё считается legacy и не переинтерпретируется.
- UTC не выдумывается: при отсутствии покрытия `timestamp`/`utc` могут быть `null`.
- `nexigen-chronod` обязателен для приложений; `NEXIGEN_ENABLE_COM=OFF` его не отключает.
- `TimeSyncMonitor` по умолчанию: шаг 1 мс, скорость 500 ppm, окно 100 мс.
- `Provider` имеет фиксированный банк источников; исчерпание — `ResourceExhausted`.
- Демон не ищет процессы `ptp4l`/`phc2sys` и не управляет ими; он наблюдает состояние часов и флаги.

## Capabilities

`chrono-settings`, `chrono-sink`, `chrono-snapshot`, `provider`, `stamp-source`, `time-base`, `time-domain`, `time-format`, `timestamp`, `time-sync`, `worker`.

## Читать дальше

- [`docs/architecture.md`](../architecture.md), раздел 4.2 — полная карта типов и режимов.
- [`platform.md`](platform.md) — место времени в плоскости данных.
- [`app.md`](app.md) — порядок привязки time base.
- [`recorder.md`](recorder.md) — как история времени попадает в сессию.
- Спецификации: `openspec/specs/chrono/*`.
