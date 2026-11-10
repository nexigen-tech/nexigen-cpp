# Proposal

## Why

Платформа переводит наблюдаемость на центральные демоны поверх iceoryx2 (`initial_spec` — источник истины), но прямой `loan/send` из продюсера стоил бы горячему пути ~190 нс вместо текущих ~20 нс. Измеренный микробенч (`bench-log-iceoryx2-latency`) показал, что вариант worker-publish сохраняет hot path: продюсер остаётся на SPSC-ring (~7 нс), а ~278 нс транспорта уезжают в фоновый воркер, который и так привязан к не-RT ядру. Нужен логгер, который публикует записи в iceoryx2, переиспользуя существующий движок `Producer` + `AsyncWorker` без рефакторинга.

## What Changes

- Новый `nexigen::logging::Iceoryx2Logger` — асинхронный wait-free логгер, который вместо записи в файл публикует записи в iceoryx2 (паттерн Publish-Subscribe + Event для пробуждения подписчика).
- Новый `detail::Iceoryx2Sink` — подменяемый sink с тем же duck-typed контрактом (`start/handle/synchronize/stop`), что у `FileSink`/`ConsoleSink`: `handle()` рендерит сообщение и публикует `LogFrame`.
- Новый `Iceoryx2LoggerConfig` — имя сервиса, `max_subscribers`, `history_size`, аффинность воркера, потолок idle-сна воркера, начальный уровень, throttle.
- Тип-фрейм `LogFrame` (256 Б, тривиально-копируемый) по `REQ_LOGC_004`: `timestamp_ns`, `app_id`, `context_id`, `thread_id`, `log_level`, `payload_size`, `dropped_counter`, `payload[232]`.
- Горячий путь (`Producer::emit`, drop-счётчики, flush-барьер, аффинность воркера) переиспользуется без изменений.

## Capabilities

### New Capabilities

- `logging/iceoryx2-logger`: асинхронный логгер, публикующий `LogFrame` в iceoryx2 через worker-publish (off-hot-path транспорт поверх существующего движка).

### Modified Capabilities

Нет.

## Impact

- **Код**: новые `logging/iceoryx2_logger.hpp`, `logging/iceoryx2_logger_config.hpp`, `logging/detail/iceoryx2_sink.hpp`, `logging/log_frame.hpp` (+ тесты). Существующие логгеры не меняются.
- **Производительность**: продюсерский hot path не меняется (~20 нс); транспорт `loan/send/notify` (~278 нс) выполняется воркером на не-RT ядре. `notify` — отдельный Event-сервис (два сервиса на запись, `REQ_LOGC_005`).
- **Память**: измеряемое окно — ноль аллокаций (бенч подтвердил `heap-delta 0 B` в Debug/Release/TSan/ASan).
- **API/зависимости**: `Iceoryx2Logger` требует `nexigen::com` — живёт за гейтом `NEXIGEN_ENABLE_COM`, как остальные com-зависимые цели.
- **Вне скоупа**: сам `LogDaemon` (подписка, финальное форматирование, диск/сеть/crash-ring, ротация) — отдельный change демонной волны; клиент публикует и с нулём подписчиков.
