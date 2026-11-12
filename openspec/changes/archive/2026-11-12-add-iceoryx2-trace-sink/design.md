# Design

## Context

См. `proposal.md` — Why. Текущее состояние, которое формирует решение:

- `SpanRecord` (`include/nexigen/tracing/span_record.hpp`) — 320 Б, `alignas(64)`, `trivially_copyable`, но содержит `const char* file_name` и `const char* function_name` в program-static storage. Указатели валидны только внутри процесса-производителя, поэтому запись нельзя публиковать как есть.
- `TracerProvider` уже владеет единственным background consumer-потоком и вызывает `sink.start()`, `sink.on_span(record)`, `sink.flush()`, `sink.stop()` (concept `tracing::concepts::TraceSink`). Все in-process стоки документированы как consumer-thread-only состояние.
- Соседние транспорты кладут идентичность публикатора в payload: `LogFrame` и `MetricFrame` несут `app_id`/`pid` внутри кадра.
- `nexigen::com::Publisher` реализует `DiscardData` и счётчики потерь; custom header (`Publisher<T,H>`) существует, но используется для заголовка данных `TraceHeader`, а не для служебных конвертов трейса.
- Измеренная база проекта (лог-бенч, кадр 256 Б): `loan_uninit → write_payload → send` ~190 нс; с `notify()` ~278 нс; producer SPSC-ring ~7 нс, гейт lifecycle спана ≤20 нс.

## Goals / Non-Goals

**Goals:**

- Зафиксировать wire-контракт спана, который безопасно пересекает границу процесса и не теряет атрибуты и source-локации.
- Уложить формирование кадра в измеримый бюджет (≤400 нс медианы) и не допустить деградации из-за хэширования source-строк.
- Встроиться в существующий `TracerProvider` без изменений hot path.
- Оставить ядро `nexigen::tracing` свободным от iceoryx2.

**Non-Goals:**

- `TraceDaemon`, RAM flight recorder, on-demand сессии, управление категориями.
- Perfetto `.pftrace` и ftrace.
- Словарь `hash → исходная строка` и 64-байтный lean-формат.
- Изменение `SpanRecord`, in-process стоков и API `TracerProvider`.

## Decisions

### D1 — SpanFrame: 384 Б, identity внутри, указатели → хэши

```
offset  field                          size   примечание
  0     trace_id                        8
  8     span_id                         8
 16     parent_span_id                  8
 24     kind | status | flags | count   4      по 1 байту
 28     thread_id                       4
 32     start_ns                        8      монотонный домен
 40     end_ns                          8      монотонный домен
 48     file_hash (FNV-1a 64)           8      вместо const char*
 56     function_hash (FNV-1a 64)       8      вместо const char*
 64     line | column                   8      2 × uint32
 72     name[48]                       48      47 + NUL
120     attributes[4]                 192      4 × 48 Б
312     app_id[4]                       4      4-байтный ASCII-код
316     pid                             4      ::getpid(), кэш в start()
320     sequence                        8      монотонный per-sink
328     publish_ts_ns                   8      chrono::Timestamp::now()
336     version                         2      формат = 1
338     reserved[46]                   46      session/category/dictionary-id
= 384 Б, alignas(64), 6 кэш-линий, trivially_copyable
```

`static_assert` пинует `sizeof == 384`, `alignof == 64`, `is_trivially_copyable` и смещения каждого поля, чтобы сдвиг молча не менял формат. `com::concepts::Message` выполняется автоматически.

Почему 384, а не 352 (320 payload + 32 custom header): идентичность — часть кадра, как у `LogFrame`/`MetricFrame`; это убирает второй тип из контракта, упрощает будущий `Subscriber<SpanFrame>` в демоне и тесты. Выигрыш 32 Б (9%) не окупает расхождение с соседними транспортами. Почему не 64 + словарь — см. D2.

`version` включён сразу, чтобы демон мог отвергать неизвестный формат; `reserved` — явный задел под session id (on-demand), category hash и dictionary id, без сдвига раскладки.

Атрибуты копируются как есть, но zero-copy string-атрибут длиннее 23 Б хранит в in-process `SpanAttribute` указатель на caller storage. На worker такой атрибут клиппится в inline-арм (первые 23 Б, `text_inline = true`), чтобы через шину не уходил ни один указатель. Потеря хвоста — осознанная адаптация; расширенный attribute-кадр или словарь могут вернуть полный текст в `add-perfetto-export`.

### D2 — Альтернативы: 64 Б + словарь отложены по триггеру

| | A: 320 + header 32 | **B: 384 inline (выбран)** | C: 64 + словарь |
|---|---|---|---|
| Байт на спан | 352 | 384 | 64 (+ ~72 Б × N один раз) |
| Типов в контракте | 2 | **1** | 2+ |
| Атрибуты / source line | да | **да** | нет / частично |
| Имя спана | inline | **inline** | hash → словарь |
| Worker на спан (оценка) | 215–240 нс | **215–240 нс** | 250–280 нс без кэша имён |
| Ring 32 МБ @10k/с | ~9.5 с | **~8.7 с** | ~52 с |

C соответствует TRCC_007 буквально, но теряет атрибуты и source-локации, требует словаря (регистрация при старте, коллизии, слияние в демоне) и, по оценке, не быстрее B при memo-кэше, потому что всё равно хэширует имя каждого спана. Возврат к C — отдельный change при устойчивых `>20–50k` спанов/с или если бюджет flight-recorder ring ≤32 МБ не даёт нужного окна ретенции.

### D3 — FNV-1a 64 с memo-кэшем на worker

Хэш: offset basis `0xcbf29ce484222325`, prime `0x100000001b3`, по байтам текста без завершающего NUL (`tracing::detail::fnv1a_64`). `std::hash` запрещён: значение не обязано совпадать между процессами и сборками.

FNV-1a — последовательная цепочка `xor + imul` с латентностью ~3 такта на байт, поэтому 70–140 байт source-строк стоят 70–140 нс и дороже копии кадра. Кэш:

```
struct HashCacheEntry { const char* text; std::uint64_t hash; };
HashCacheEntry cache_[64];   // 1 КБ, worker-confined
index = (reinterpret_cast<std::uintptr_t>(ptr) >> 3) & 63
```

- Попадание: `entry.text == ptr` → вернуть готовый хэш без чтения строки.
- Промах: посчитать FNV-1a, записать `{ptr, hash}`, инкрементировать `hash_misses_`.
- `nullptr` → hash `0`, не кэшируется.
- Корректность: `std::source_location::file_name()/function_name()` указывают в storage времени жизни процесса, поэтому равенство указателей влечёт равенство текста; разные указатели на одинаковый текст дают одинаковый хэш независимо.
- Счётчики `hash_hits_`/`hash_misses_` — plain `uint64_t` на worker, выводятся в диагностике.

Альтернативы: кэш «последние 2 записи» (хуже для чередующихся call-site), полный словарь на старте (не знает все локации заранее), более быстрый не-FNV хэш (ломает SRS-именование без выигрыша при кэше).

### D4 — Дренаж по интервалу, без Event на каждый спан

Sink не создаёт event-сервис и не вызывает `notify()`. Спаны идут поштучно: `notify()` на каждый — wake-storm на сервисном ядре демона. SRS TRCD_002 предписывает пакетное чтение раз в 20–50 мс, поэтому потребитель вешает `WaitSet::attach_interval`; `sequence` в кадре даёт счёт потерь, `publish_ts_ns` — задержку.

### D5 — Переполнение: DiscardData + счётчик, on_span всегда успешен

`Publisher::create` уже использует `enable_safe_overflow(false)` (`DiscardData`). Если `loan_uninit()` не дал слот или `send()` вернул ошибку, sink инкрементирует `dropped_` и возвращает `Ok`: потеря отдельного спана не должна останавливать worker. `send()` при полной очереди получателя успешен (кадр просто не доставлен), поэтому sink сравнивает снимки `Publisher::lost_samples()` до и после отправки и тоже считает такой кадр потерянным; для этого у `Publisher` добавлен `const`-аксессор телеметрии (аддитивно, без изменения поведения). `flush()` — no-op `Ok`, потому что отправленный кадр уже в разделяемой памяти. Глубина очереди запрашивается из `subscriber_max_buffer_depth` при создании сервиса.

### D6 — Всё состояние sink — worker-confined, без атомиков

`published_`, `dropped_`, `sequence_`, `pid_`, memo-кэш и его счётчики пишутся только на consumer-потоке провайдера. `std::atomic` не нужен и добавил бы барьеры; контракт «consumer-thread-only» документируется так же, как у `JsonFileTraceSink`/`ChromeTraceSink`. `std::memory_order` в этом change'е не выбирается ни для одного объекта sink'а; внутренние атомики `com::Publisher` — его собственная телеметрия. Если позже понадобится читать счётчики из другого потока, это отдельное изменение с явными order'ами.

### D7 — Отдельная INTERFACE-цель nexigen_tracing_com

По образцу `nexigen_logging_com`/`nexigen_metrics_com`: `nexigen_tracing_com` линкует `nexigen_tracing` + `nexigen_com`. Ядро `nexigen_tracing` не получает зависимость от iceoryx2 и собирается при `NEXIGEN_ENABLE_COM=OFF`. Тестовый target `nexigen_tracing_com_tests` регистрируется внутри того же гейта.

### D8 — Публикация один кадр на спан

`on_span` получает ровно один `SpanRecord` (concept не батчевый), поэтому sink публикует один `SpanFrame` на вызов. Батчинг потребовал бы собственного буфера и таймера, дублируя очередь iceoryx2; при необходимости его логичнее добавить в транспорт отдельным изменением.

### D9 — Latency-гейт: медиана on_span ≤ 400 нс

Харнес копирует `tests/{tracing,logging}/latency_test.cpp`: калибровка ns/tick (50 мс), WARMUP, N = 10 000, `__rdtsc`-пары, вычитание overhead, медиана и p99/p99.9/max, пининг producer/worker на разные разрешённые ядра, best-of-3 в strict-конфигурации.

Что измеряется: полный `sink.on_span(record)` — маппинг, memo-хэши с учётом промахов, `Timestamp::now()`, `loan/write_payload/send`. Подписчик в измеряемом окне не создаётся (как в `bench-log-iceoryx2-latency`): это tripwire на стоимость формирования, а не end-to-end доставка; корректность доставки проверяется отдельным round-trip тестом.

Порог: **400 нс медианы** (ожидание ~215–240 нс, запас на промахи кэша и шум). Гейт компилируется только под `NEXIGEN_STRICT_LATENCY_TESTS` и `Release/RelWithDebInfo` x86-64; Debug/TSan/ASan печатают числа и проверяют только корректность и отсутствие аллокаций.

## Risks / Trade-offs

- **[384 Б дороже 64 Б в 6 раз по объёму]** → у flight recorder прямо зависит окно ретенции: 32 МБ ≈ 87k спанов ≈ 8.7 с при 10k/с и ~1.7 с при 50k/с. Митигация: ring 48–64 МБ, sampling категорий, а при устойчивых >20–50k/с — отдельный lean-change (D2).
- **[FNV-1a на каждый промах кэша стоит 70–140 нс]** → гейт 400 нс может сработать на «холодном» профиле. Митигация: 64-записный кэш покрывает типовой набор call-site; промахи видны в `hash_misses_`; гейт отдельно печатает p99.
- **[Хэши source-локаций нечитаемы без словаря]** → в первом дампе нет имён файлов/функций. Митигация: `name` спана остаётся inline; словарь — отдельный change; формат не меняется.
- **[Latency-гейт шумит на shared CI]** → строгий ассерт только в Release x86-64 при `NEXIGEN_STRICT_LATENCY_TESTS`; на общих раннерах гейт выключен, числа печатаются.
- **[Нет потребителя в этом change'е]** → кадры уходят в очередь и перезаписываются. Митигация: прецедент `add-iceoryx2-logger`; round-trip тест использует собственного subscriber'а; `add-trace-daemon` — следующий change.
- **[Memo-кэш по указателю может «залипнуть» на освобождённом адресе]** → `source_location` возвращает static storage времени жизни процесса, выгрузки нет; `nullptr` не кэшируется. Риск отсутствует в рамках контракта.
- **[`Timestamp::now()` может вернуть ошибку]** → `publish_ts_ns = 0` означает «время недоступно»; публикация не прерывается.
- **[Коллизия FNV-1a]** → 64-бит при десятках тысяч строк даёт пренебрежимую вероятность; будущий словарь сможет разрешать коллизии по паре (hash, text).

## Migration Plan

- Изменение аддитивное: новые заголовки, новая INTERFACE-цель, новый тест. Сборки `NEXIGEN_ENABLE_COM=OFF` не затрагиваются.
- Порядок: wire-тип + FNV → memo-кэш → config → sink → CMake-гейт → latency-харнес → тесты.
- Откат: удалить новые заголовки/цель/тест; существующие типы и файлы не меняются, миграция данных не нужна.
- Совместимость wire-формата: `version = 1` в кадре; до появления потребителя формат считается нестабильным, как `LogFrame` до `add-log-daemon`.

## Open Questions

- Нужен ли демону отдельный event для немедленного сброса при аварии, помимо интервала? Решается в `add-trace-daemon`; wire-формат от этого не зависит.
