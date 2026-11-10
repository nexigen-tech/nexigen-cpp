# Design

## Context

`Iceoryx2Logger` (`add-iceoryx2-logger`) публикует `LogFrame`'ы — фреймы приходят, но принимающей стороны нет. `initial_spec` (LOGD_001–013) требует центральный демон-агрегатор. MVP — только диск-синк (JSON в `log_directory` с ротацией); сетевой стриминг и crash-ring — отдельными change'ами. См. proposal.md.

## Goals / Non-Goals

**Goals:**
- LogDaemon как отдельный инструмент `tools/nexigen-logd`, переиспользующий `nexigen::app::Infrastructure` (сигналы, свой логгер, воркер-аффинность).
- Подписка на `LogFrame`, `WaitSet`-drain без поллинга, батчевая запись на диск с ротацией.
- `LogFrame` дооснащается `pid` (4 байта), `payload` 229→225, общий размер 256 байт сохранён.
- Выходной формат — JSON-строки, совместимые с текущим `FileSink` (чтобы `nexigen-hub` читал без переделок).

**Non-Goals:**
- Сетевой стриминг (UDP/TCP, REQ_LOGD_006/011) — отдельный change.
- Crash-ring буфер в RAM (REQ_LOGD_002/007/010) — отдельный change.
- Рантайм-управление уровнями (REQ_LOGD_012) — отдельный change.

## Decisions

**D1 — LogFrame: `pid` (4 байта).** Точная раскладка обновлённого фрейма:

```
offset  field             size
0       timestamp_ns      8
8       app_id            4
12      context_id        4   (резерв = 0)
16      thread_id         4
20      pid               4   ← НОВОЕ
24      dropped_counter   4
28      payload_size      2
30      log_level         1
31      payload           225 ← было 229
        ──────────────────
        total             256
```

`pid` кэшируется в `Iceoryx2Sink`: `static_cast<std::uint32_t>(::getpid())` вызывется один раз в `start()` и сохраняется в поле `sink_pid_`. `handle()` пишет `frame_.pid = sink_pid_`.

**D2 — Архитектура демона.** `nexigen-logd` = `Infrastructure<Policy, ...>` (сигналы, свой FileLogger для телеметрии самого демона — НЕ iceoryx2, избегаем циклической зависимости) + `Subscriber<LogFrame>` + `WaitSet` + форматтер + `detail::FileSink`.

Демонский FileLogger пишет в отдельную директорию (`<log_directory>/_daemon/`), чтобы не смешиваться с агрегируемыми логами.

**D3 — Drain-цикл.** Подписчик (и, опционально, периодический таймер батчевого сброса) крепятся к `WaitSet` через `attach_notification` / `attach_interval`. Колбэк: при приходе фрейма — форматтер → буфер; при срабатывании таймера — сброс буфера. Идл-сон через ядро (zero CPU).

**D4 — Форматтер.** Каждый `LogFrame` рендерится в одну JSON-строку:

```json
{"app":"LIDR","level":"warning","timestamp":"2026-11-11T14:30:01.123456789","thread":7,"pid":42,"message":"calibration lost"}
```

Поля: `app` из `frame.app_id_text()`, `level` из `core::to_string(frame.log_level)`, `timestamp` из `chrono::format_utc_iso8601_ns(frame.timestamp_ns, offset)`, `thread`, `pid`, `message` из `frame.payload_text()`.

**D5 — Батчевый буфер и сброс.** Предварительно аллоцированный `char buffer[65536]` (64 КБ, как `REQ_LOGD_008`). Каждая строка дописывается в буфер; когда `used + line_len > capacity` или срабатывает таймер (`batch_flush_interval_ms`, по умолчанию 500 мс) — сброс через `FileSink::handle()`. `FileSink` получает готовую строку (рендеринг уже сделан), поэтому его `handle()` вызывается с уже-сформированной JSON-строкой.

**D6 — FileSink переиспользование.** `detail::FileSink` уже реализует ротацию, `fsync` и сегментацию. Демон создаёт один `FileSink` c `Settings.app = "_daemon"` (или фиксированным именем файла на основе `app_id` фрейма… но `app_id` разный для разных клиентов). 

Уточнение: разные `app_id` (LIDR, LOGD, PHM…) должны писаться в ОДИН файл (или в разные?). В initial_spec манифесте (`log_daemon_config.yaml`) все логи сливаются в общий файл с ротацией. Один `FileSink` с фиксированным `file_name = "nexigen.jsonl"` — `app_id` идёт внутрь JSON-строки, ротация по директориям (часовая). Так `nexigen-hub` сможет читать один файл и фильтровать по `app`.

**D7 — Фильтрация по уровню.** `LogDaemonConfig.min_log_level_storage` — фреймы ниже этого порога не пишутся на диск (сбрасываются).

**D8 — Конфиг.** `LogDaemonConfig`:

| Поле | Тип | Назначение |
|---|---|---|
| `service_name` | `std::string` | имя подписки (должно совпадать с `Iceoryx2LoggerConfig.service_name`) |
| `log_directory` | `std::string` | абсолютный путь корня логов |
| `max_file_size_mb` | `uint64` | макс. размер активного файла (0 = без лимита) |
| `max_archive_files` | `uint32` | макс. число архивных файлов |
| `min_log_level_storage` | `LogLevel` | минимальный уровень для записи на диск |
| `bg_cpu_affinity` | `CpuSet` | маска сервисного ядра для потока демона |
| `batch_flush_interval_ms` | `uint32` | интервал батчевого сбросса (умолчание 500) |

**D9 — EM-интеграция.** Демон запускается EM как дочерний процесс: `nexigen-logd --config <path>`. После инициализации (открытие подписки, создание директории, калибровка UTC) демон вызывает `ReportApplicationState(Running)`. При SIGTERM — сброс оставшегося буфера, `fsync`, остановка.

## Risks / Trade-offs

- [Разные app_id в одном файле] → JSON-строка содержит `app` поле; `nexigen-hub` может фильтровать. При необходимости позже можно разделить по файлам на уровне демона.
- [Демон не знает PID клиента до обработки первого фрейма] → PID в самом фрейме; демону не нужно внешнее знание.
- [Один FileSink на все app_id] → ротация по часам (директории), сегментация по размеру; имена файлов статичны (`nexigen.jsonl`). Масштабируется добавлением дополнительных FileSink'ов позже.

## Open Questions

- **Сетевой/multi-sink** — отдельный change; дизайн демона оставляет место для добавления `ConsoleSink`, `NetworkSink` и `CompositeSink`.
- **Crash-ring dump** — тоже отдельный change; потребует кольцевой буфер в RAM и триггер от PHM.