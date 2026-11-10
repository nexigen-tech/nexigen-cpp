# Proposal

## Why

`Iceoryx2Logger` (`add-iceoryx2-logger`) публикует `LogFrame`'ы в сервис iceoryx2, но подписчика нет — фреймы уходят в пустоту. Для централизованного сбора, финального форматирования и долговременного хранения логов со всех процессов платформы нужен **LogDaemon** (`REQ_LOGD_001–013`) — демон-агрегатор, который подписывается на фреймы, дополняет их метаданными (дата/PID), рендерит финальную строку и пишет в файл с ротацией. Попутно `LogFrame` дооснащается полем `pid`, необходимым для криминалистического анализа (мульти-инстанс, рестарт, crash-dump).

## What Changes

- Новый инструмент `tools/nexigen-logd` — демон-агрегатор логов: `Subscriber<LogFrame>` + `WaitSet`-drain + форматтер (рендерит JSON-строку из полей фрейма) + `detail::FileSink` (дисковая запись с часовой ротацией, переиспользуя существущий код). Собственная телеметрия демона — обычный `FileLogger` (не iceoryx2), чтобы избежать циклической зависимости.
- `LogFrame` получает поле `std::uint32_t pid{}` (PID клиентского процесса), `payload` уменьшается с 229 до 225 байт — общий размер 256 байт сохраняется. `Iceoryx2Sink::handle()` заполняет `pid` через `::getpid()`.
- Конфигурация демона — `log_daemon_config.yaml` (`REQ_LOGD_013`): `log_directory`, `max_file_size_mb`, `max_archive_files`, `min_log_level_storage`, `bg_cpu_affinity`, `service_name` (имя подписки на фреймы).
- **Вне скоупа MVP**: сетевой стриминг (UDP/TCP), crash-ring буфер в RAM, рантайм-управление уровнями (`REQ_LOGD_006/007/012`) — отдельные аддитивные change'и.

## Capabilities

### New Capabilities

- `logging/log-daemon`: центральный демон-агрегатор, подписывающийся на `LogFrame`, дополняющий метаданными, рендерящий финальную JSON-строку и пишущий в файл с ротацией.

### Modified Capabilities

- `logging/iceoryx2-logger`: `LogFrame` получает поле `pid` (4 байта, `uint32_t`), `payload` — 225 байт (размер 256 байт без изменений); `Iceoryx2Sink::handle()` заполняет `pid` чере `::getpid()`.

## Impact

- **Код**: новый `tools/nexigen-logd/main.cpp` + `include/nexigen/logging/log_daemon_config.hpp`. Изменение `log_frame.hpp` (`+pid`, `payload 229→225`) и `iceoryx2_sink.hpp` (`frame_.pid = ...`). Тесты демона в `tests/logging/log_daemon_test.cpp`.
- **Производительность**: демон работает на выделенном сервисном ядре (`SCHED_OTHER`, `nice 10–15`), не влияет на горячие пути продюсеров. Форматирование и запись на диск — холодные пути (батчевый I/O, `REQ_LOGD_008`).
- **API/зависимости**: `nexigen-logd` линкует `nexigen_logging_com` (Iceoryx2Logger) + `nexigen_app` (Infrastructure). Сборка за `NEXIGEN_ENABLE_COM=ON`.
- **Breaking**: изменение раскладки `LogFrame` (pid на offset 24, dropped_counter → 28, ...) — обратная несовместимость с уже опубликованными фреймами; на ветке `learning-enhancement` код ещё не shipped, поэтому допустимо.