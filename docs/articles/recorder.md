# Рекордер nexigen: журнал, который не останавливает поток

Поток управления не должен ждать диск. Поэтому продюсеры не пишут в файл: они кладут фиксированный конверт в свой SPSC-шард, а единственный writer забирает его, когда дойдёт. Полный шард не блокирует — он превращается в `GapMarker` с причиной `IoBackpressure`; ротация и retention учитывают байты, а не настроение планировщика. Эта статья — о том, как запись устроена как продолжение real-time контракта: один writer, много продюсеров, честные потери вместо ожидания.

Это нарратив «почему так», а не справочник. Точный layout, список полей и CLI `nexigen-export` уже описаны в [`docs/recorder.md`](../recorder.md); здесь — причины, компромиссы и границы, которые из этих полей следуют.

## Карта данных

```text
  producers / encoders / nexigen-recd
        |
        | RecordEnvelope (фиксированный конверт)
        v
  RecordShard (SPSC, 1024 слота на продюсера)
        |
        | один writer, один поток
        v
  SessionWriter ---> .nxrec  append-only записи + CRC-32C + footer
        |            .nxidx  64-байтовые разреженные интервалы
        |            .nxvid  4 KiB-aligned видео-кадры
        |            session.json  манифест: схемы, каналы, сегменты, time base
        |
        v
  SessionReader ---> datasets() / for_each() / verify() / read_video()
        |
        v
  nexigen-export ---> JSONL / Perfetto / MCAP / elementary video
```

Две границы делят систему. Первая — между продюсером и writer: продюсер платит только за копирование в конверт и `try_push`, а всё, что связано с файлами, делает writer. Вторая — между сессией и конвертером: сессия самодостаточна, а `nexigen-export` — отдельный on-demand процесс, который читает её без SDK и без демона.

## Продюсер не пишет в файл

`SessionWriter` — единственный владелец файловых дескрипторов, порядка append, ротации, retention и разреженного индекса. Он однопоточный: `append()` не аллоцирует и не ждёт других потоков, но и не является thread-safe. Он может блокироваться на записи в файл — это блокировка writer-потока, а не продюсера. Если несколько потоков хотят писать, они не зовут `append()` напрямую — они пользуются `RecordShard`.

`RecordShard` — это SPSC-кольцо на 1024 конверта. Конверт `RecordEnvelope` фиксирован: тип записи, `channel_id`, `event_ns`, `sequence`, `publisher_id` и inline-буфер на 4096 байт. Продюсер заполняет конверт и делает `try_push`; writer позже забирает его через `try_pop`. Ни мьютекса, ни CAS-цикла, ни ожидания в кольце нет.

Конверт ограничен 4096 байтами payload. Записи большего размера не проходят через шард: для них есть прямой `SessionWriter::append()` с потолком `max_record_bytes` (по умолчанию 1 МиБ). Это осознанный размен: шард оптимизирован под частые мелкие записи телеметрии, а редкие крупные кадры или описания идут медленным путём и не раздувают кольцо.

Цена продюсера в установившемся режиме — копирование payload в конверт и один `try_push`. Дальше его не интересует ни файловая система, ни ротация, ни то, успел ли writer сбросить буфер. Именно это делает запись совместимой с real-time: продюсер не может быть заблокирован диском.

## Полный шард — это данные, а не ошибка

Когда кольцо заполнено, `try_push` возвращает `BufferFull`, а шард запоминает первый потерянный `sequence` и количество потерь. Продюсер не ждёт и не повторяет попытку. Writer, дренируя шард, видит накопленный диапазон и пишет один `GapMarker`:

```text
GapMarkerPayload:
  first_lost_sequence = <первый не принятый sequence>
  lost_count          = <сколько конвертов не влезло>
  reason              = IoBackpressure
```

Это принципиальная позиция: потеря — это факт, который надо записать, а не ошибка, которую надо скрыть. `GapMarker` — обычная запись в `.nxrec`; читатель и экспортёр видят её наравне с данными. В инвентаре `DatasetInfo.gaps` считает такие маркеры, а `DatasetInfo.records` — только реально сохранённые записи. Никакого «мы как будто записали всё» нет.

У `GapReason` пять значений: `Unknown`, `IoxRingOverflow`, `EncoderOverload`, `IoBackpressure`, `StreamDiscontinuity`. Первые два приходят из транспорта и энкодера, третий — из полного шарда, четвёртый — из разрыва последовательности источника. Один и тот же механизм покрывает и «writer не успел», и «источник потерял кадры»; различие — в причине, а не в формате.

Важно, что `GapMarker` не восстанавливает потерянное. Он фиксирует диапазон и причину, чтобы последующий анализ не принял разрыв за непрерывную запись. Это честнее, чем пытаться дописать «дыру» догадками.

## Один writer и цена этого решения

Почему writer один? Потому что у сессии должен быть один порядок append. Если несколько потоков пишут в `.nxrec` и `.nxidx`, приходится либо сериализовать их мьютексом (и нарушить real-time контракт), либо чинить порядок индекса и ротации после факта. Один writer — это единственная точка, где известны текущий сегмент, смещение, интервал индекса и состояние манифеста.

Следствие: writer — последовательный ресурс. Продюсеры параллельны, writer — нет. Это нормально, пока writer успевает; если не успевает, шарды переполняются и появляются `GapMarker`. Пропускная способность системы честно ограничена диском и одним потоком записи, и это видно в данных, а не проявляется как необъяснимые задержки.

Второе следствие: writer владеет не только файлами, но и временем. `commit_mono_ns` в заголовке записи — это время, когда writer принял запись, а не когда продюсер её создал. Время события (`event_ns`) хранится отдельно, в индексе и в конверте. Поэтому в сессии всегда есть два времени: «когда произошло» и «когда записали». Для корреляции важны оба.

```cpp
nexigen::recorder::StorageSettings settings{};
settings.session_root = "/var/log/nexigen/sessions";
settings.session_id = "hil-run-42";

auto writer = nexigen::recorder::SessionWriter::create(settings);
if (!writer) {
    return 1;
}

nexigen::recorder::RecordShard shard;   // один на продюсера
nexigen::recorder::RecordEnvelope env{};
env.type = nexigen::recorder::RecordType::LogFrame;
env.channel_id = 7;
env.event_ns = 123456789;
env.sequence = 1;
// env.payload = ...
shard.try_push(env);                    // полный шард не блокирует

writer->append_from_shard(shard);       // writer вытягивает и пишет
writer->close();
```

Здесь нет ни одного `std::mutex` и ни одного `write()` на стороне продюсера. Writer может отставать, но не может заставить продюсера ждать.

## `nexigen-recd`: сбор телеметрии

Storage-слой ничего не знает о транспорте: `nexigen_recorder` линкует только `nexigen_core` и `nexigen_chrono`, без iceoryx2, logging, metrics или tracing. Данные приходят от приложений и от коллектора `nexigen-recd`, который подписывается на suite-сервисы и складывает кадры в одну сессию.

### Что подписывается

По умолчанию коллектор слушает три сервиса: `Nexigen/Log/nexigen`, `Nexigen/Metrics/nexigen` и `Nexigen/Tracing/nexigen`. Флаги `--logs`, `--metrics`, `--traces` включены по умолчанию, `--no-logs`/`--no-metrics`/`--no-traces` выключают поток. `--event-service <name>` добавляет произвольный `com`-сервис и автоматически включает события; флаг можно повторять. `--session-root` задаёт корень сессий, `--max-bytes` — лимит сегмента, `--retention-bytes` — бюджет retention. Конфигурация сериализуется в `RecorderDaemonConfig`.

### Как устроен цикл

```text
WaitSet
  +-- EventListener <log-service>Event
  +-- EventListener <metrics-service>Event
  +-- EventListener <traces-service>Event
  +-- EventListener <event-service>        (для каждого --event-service)
  +-- interval 100 ms
        |
        v
  wait_and_process -> drain notifications -> drain_streams -> sync_if_due
```

Коллектор не крутится в busy-poll: он спит в `WaitSet`, пока не придёт уведомление от транспорта или не истечёт 100-миллисекундный интервал. Уведомления нужны, потому что log/metric/trace-продюсеры публикуют в companion-сервис `<service>Event`; события уведомляют сам канал. Интервал обслуживает `TimeSync` раз в секунду и ограничивает задержку остановки. Idle-коллектор не опрашивает транспорт.

После каждого пробуждения `drain_streams` вычитывает все доступные кадры до `BufferEmpty`, а не один батч. Поэтому всплеск подписчика разгружается за один проход, а не размазывается по тикам.

### Как называются каналы

Канал — это `(<stream>, <app>)`, имя строится как `<stream>.<app>`:

- `log.<app>`, `metric.<app>`, `trace.<app>` — для платформенных кадров;
- `event.<app>` — если у события есть `app_id`;
- `event.<service>` — если `app_id` пуст; имя сервиса санитизируется.

Схемы 1/2/3 — встроенные `nexigen.log.LogFrame`, `nexigen.metrics.MetricFrame`, `nexigen.tracing.SpanFrame`. Для событий коллектор создаёт схему `nexigen.com.EventEnvelope` на каждый встреченный размер payload: 72-байтовый `EventHeader` (включая `app_id`) плюс opaque payload. Идентификаторы начинаются с 100, поэтому пользовательские схемы не конфликтуют с встроенными.

Кадры пишутся байт-в-байт: коллектор не декодирует log/metric/trace и не переупаковывает event payload. Для `LogFrame`, `MetricFrame` и `SpanFrame` он берёт `timestamp_ns`/`start_ns`, sequence и `publisher_id` из транспорта; для события — `timestamp_ns`, `sequence_id` и `producer_epoch` из `EventHeader`.

### Что происходит с потерями

Потери не маскируются. У подписчика есть `lost_before()` — сколько сэмплов транспорт потерял до этого; коллектор превращает это в `GapMarker` с `GapReason::IoxRingOverflow`. У `LogFrame` есть `dropped_counter` — потери внутри логгера; они становятся `GapMarker` с `GapReason::StreamDiscontinuity`. Для событий работает `EventGapTracker`: он помнит `producer_epoch` и последний `sequence_id`; скачок внутри эпохи даёт `GapMarker` с `IoxRingOverflow`, новая эпоха просто сбрасывает baseline.

Таким образом, `DatasetInfo.gaps` в сессии — это не «дыры, которые мы придумали», а конкретные диапазоны с причиной. Если поток порвался, в сессии будет запись об этом; если всё гладко, gaps останутся нулевыми.

### Время и жизненный цикл

При создании сессии коллектор пишет `boot_token`/`boot_uuid` из `chrono::read_boot_identity()` и `TimeSync` с текущим UTC-смещением. Дальше `TimeSync` добавляется раз в секунду, поэтому записанные instant-ы остаются разрешимыми после ротации сегментов и после остановки демона. Сессия получает id `recd-<steady_clock count>`; `session.json` сразу содержит привязку к boot и time-base epoch.

`SIGINT`/`SIGTERM` выставляют флаг остановки. Цикл выходит из `WaitSet`, делает финальный `drain_streams`, закрывает writer (`close()` с футером и `clean_close`) и возвращается. Если `WaitSet` отказал, коллектор всё равно пытается финально дослить и закрыть сессию, но возвращает ненулевой код.

### Эксплуатация

Один поток — одно место назначения. Если `nexigen-recd` и `nexigen-logd` слушают один и тот же log-сервис, телеметрия сохранится дважды: один раз в per-process JSONL, второй — в сессию. Для suite-записи оставляйте один коллектор; для per-process логов — `nexigen-logd`.

Коллектор — отдельная утилита, а не библиотечный класс. Его конфигурация и CLI описаны в [`docs/recorder.md`](../recorder.md); storage-библиотека остаётся COM-free и пригодной для встраивания в другой процесс.

## Журнал, который переживает падение

Теперь представим, что процесс убили. Что останется на диске?

Сегмент — это тройка файлов: `.nxrec` с записями, `.nxidx` с разреженным индексом и `.nxvid` с видео-кадрами. Каждая запись в `.nxrec` начинается с 48-байтового `RecordHeader`:

```text
offset  size  поле
0       4     magic "RCD1"
4       2     type (RecordType)
6       2     flags
8       4     channel_id
12      4     payload_size
16      8     commit_mono_ns
24      8     sequence
32      8     publisher_id
40      4     header_crc32c
44      4     payload_crc32c
```

`header_crc32c` покрывает все поля до себя, `payload_crc32c` — байты payload. Читатель отвергает запись, если не совпала любая из двух. Это не криптографическая защита, а проверка целостности: она ловит обрыв записи, частичную перезапись и порчу носителя, но не подмену злоумышленником.

Сегмент завершается 64-байтовым `SegmentFooter` с магией `FTR1` в начале и в конце, счётчиками записей/гэпов/байтов и CRC. Наличие валидного футера — это и есть определение «сегмент закрыт». `SessionManifest.clean_close` становится true только после того, как футер записан, а манифест обновлён.

Манифест `session.json` заменяется атомарно: writer пишет `session.json.tmp`, делает `fsync`, делает `rename`, затем `fsync` каталога. Поэтому читатель никогда не видит наполовину записанный манифест: либо старая версия, либо новая.

Процесс падения выглядит так:

1. `session.json` либо есть, либо нет. Если есть — он валиден.
2. Сегменты сканируются по футерам. Сегмент без футера — это не «битый файл», а восстановленный префикс: записи до последней успешной записи читаемы, хвост может быть обрезан.
3. `.nxidx` может отсутствовать или быть короче, чем `.nxrec`. `SessionReader::recover_index()` перестраивает индекс по `.nxrec`; отсутствие индекса не фатально.
4. `verify()` проверяет CRC каждой записи и футер, возвращая первое место поломки: `segment_seq`, `offset`, `channel_id`.

Именно поэтому `.nxidx` — вспомогательный файл, а не источник истины. Индекс можно потерять: он выводится из `.nxrec`. `.nxvid` устроен похоже: кадры лежат с выравниванием 4 КиБ, а ссылка на кадр пишется после самих байтов, поэтому необоснованный хвост можно игнорировать при восстановлении.

## Что именно остаётся после `SIGKILL`

Здесь важна точность, иначе обещание crash safety превращается в магию.

`SessionWriter::append()` не делает `fsync` на каждую запись. Payload копируется в предвыделенный буфер (по умолчанию 1 МиБ) и сбрасывается в файл, когда буфер заполняется или при `rotate()`/`close()`. Это означает:

- **Падение процесса** (`SIGKILL`, `abort`, аварийное завершение): данные, которые успели уйти в `write()`, остаются в страничном кэше ОС и переживут смерть процесса. Хвост, который ещё лежал в пользовательском буфере, потерян.
- **Потеря питания или падение ядра**: без `fsync` последние страницы могут не дойти до носителя. Гарантия — только до последнего `fsync`, который делается при финализации сегмента и при обновлении манифеста.
- **Чистое закрытие**: `close()` пишет футер, делает `fsync` дескрипторов, обновляет манифест с `clean_close = true`. Только после этого сессия считается закрытой.

Поэтому формулировка «переживает падение» точная лишь в пределах модели: **записанное в файл — читаемо; не записанное — потеряно; отсутствие футера означает восстановленный префикс, а не ошибку**. Если нужна гарантия на каждую запись, её надо строить отдельно — например, `fsync` на критических границах, — и платить за это задержкой.

Тот же принцип у индекса: он разреженный. `IndexEntry` покрывает интервал, а не каждую запись: `rec_offset`, `end_rec_offset`, `min_event_ns`, `max_event_ns`, `channel_mask`, `record_count`, CRC. Интервал закрывается по `index_stride_bytes` (256 КиБ) или `index_interval_ns` (50 мс). Поэтому запрос по времени сначала сужает диапазон по индексу, но всё равно фильтрует записи по `event_ns` — событийное время может быть неупорядоченным.

## Сессия, которую можно открыть через год

Через год вы находите каталог с сессией на машине, которой уже нет. Чтобы его открыть, не нужен ни исходный SDK, ни демон времени, ни таблица схем.

`session.json` — это самодостаточное описание: формат и версия, `session_id`, `time_domain`, `boot_token`, `boot_uuid`, `time_base_epoch`, эффективные `StorageSettings`, таблица схем, таблица каналов и каталог сегментов. Каждый сегмент несёт `closed`, счётчики записей и гэпов, байты и границы commit-времени.

Схемы встроены целиком. `SchemaDeclare` содержит `id`, `name`, `version`, `encoding`, `units`, `endianness`, полный дескриптор `data` и `data_crc32c`. Никаких путей, URL или digest-only ссылок: `config_loader` при старте читает файл схемы, ограничивает его 256 КиБ, проверяет объявленный SHA-256 и сохраняет **полные байты** дескриптора. В сессии не остаётся внешних ссылок. `load_recorder_config()` возвращает `RecorderConfig`: эффективные `StorageSettings`, встроенные схемы и объявления каналов.

Каналы ссылаются на схемы по `schema_id`. `ChannelDeclare` содержит `id`, `topic`, `kind` (`Telemetry`/`Opaque`/`Video`), `message_encoding`, необязательный JSON `metadata`, `native_domain`, `domain_id`/`domain_epoch`, `domain_class` и `access_mode`. Канал объявляется до первой записи, которая на него ссылается; смена схемы создаёт новый канал, а не переиспользует старый id. `SessionWriter::declare_schema()` и `declare_channel()` пишут декларацию в активный сегмент и зеркалят её в `session.json`; поэтому сегмент остаётся самодостаточным даже без манифеста.

Лимиты — часть контракта: до 64 каналов и 64 схем, схема до 256 КиБ, payload до `max_record_bytes` (по умолчанию 1 МиБ), видео-extradata до 1024 байт. Это не «мягкие рекомендации»: превышение отклоняется валидацией до создания файла.

Отдельно стоят встроенные схемы платформенных кадров: `nexigen.log.LogFrame`, `nexigen.metrics.MetricFrame`, `nexigen.tracing.SpanFrame` и метаданные видео. Они лежат в `builtin_schemas()` как обычные `SchemaDeclare`, поэтому рекордер не линкует logging/metrics/tracing. Тесты пиннят раскладку этих дескрипторов к реальным C++-структурам — если кадр изменится, сборка тестов это заметит.

## Время как данные

В рекордере время — не предполагаемая константа, а записанный поток.

Есть два разных словаря. `recorder::TimeDomain` — тег манифеста: `MonotonicRaw` или `Utc`; он говорит, в каком домене лежат штампы сессии. `chrono::TimeDomain` — платформенный словарь доменов: системные часы, счётчики, устройства. Оба называют хаб `MonotonicRaw`, но манифест лишь объявляет семейство, а конкретные домены живут в объявлениях каналов и в истории time base.

`TimeSync` — простая калибровка: `mono_ns` и `utc_offset_ns = utc_epoch_ns - mono_ns`. Она позволяет конвертеру вывести UTC, не читая локальные часы.

`TimeBasePayload` — полная история: 4224 байта, 32 строки `DomainCoefficient`, `generation`, `time_base_epoch`, `boot_token`, полный `boot_uuid`, `status`, `sync_mode`, `gap`, `service_flags`, TAI/UTC-смещения с неопределённостью. Строки переиспользуют `chrono::DomainCoefficient` целиком, включая интервалы валидности. `layout` с магией `TBP2` отличает текущий контракт от устаревшего: старый payload того же размера не переинтерпретируется, а помечается как legacy.

`TimeSnapshot` — 896 байт: снимок сервиса раз в секунду — значения каждого домена, epoch, lock state, флаги. Он не дублирует таблицу коэффициентов; читатель сопоставляет snapshot с `TimeBasePayload` по `generation`.

`TimeBaseSessionBridge` — адаптер, который связывает опубликованный блок time base с writer. Каждая генерация записывается один раз; `Lost`-блок становится явным gap-entry без строк; встроенный 1 Hz snapshot добавляется, когда его генерация сдвинулась. Мост ничего не аллоцирует во время записи: boot UUID копируется один раз при создании.

`TimeBaseResolver` отвечает на вопросы пост-фактум. Он принимает все записанные `TimeBasePayload` и `TimeSnapshot`, хранит их в порядке записи и умеет:

- `canonical_from_native(domain_id, epoch, native)` — из native-домена в хаб `CLOCK_MONOTONIC_RAW`;
- `native_from_canonical(domain_id, epoch, canonical)` — обратно;
- `convert(...)` — из любого домена в любой через хаб.

Правила жёсткие: instant ищется по строке, которая покрывает его интервалом; если покрытия нет, соседняя строка не подставляется; gap-entry не используется как карта; непокрытый instant остаётся `unresolved`. Поэтому «время как данные» — это не «мы примерно знаем, сколько было времени», а «мы можем доказать, какая строка покрывала этот instant, или честно сказать, что не можем».

С этим связаны ещё две вещи:

- **Commit time vs event time.** `RecordHeader::commit_mono_ns` — время writer-потока; `event_ns` — время события, которое кладёт продюсер. Они могут расходиться, и это нормально: writer мог дренировать шард позже. Индекс хранит оба.
- **Publisher epochs.** `publisher_epochs()` разбивает записи канала на эпохи: новый `publisher_id` или убывающий `sequence` начинает новую эпоху. Внутри эпохи разрывы представлены `GapMarker`, а не догадками о пропущенных последовательностях.

## Формат на диске

Ниже — сводка wire-формата. Поля и размеры зафиксированы `static_assert`, порядок байтов — little-endian, зарезервированные поля пишутся нулями.

### Каталог сессии

```text
<session_root>/<session_id>/
  session.json
  <session_id>_000000.nxrec
  <session_id>_000000.nxidx
  <session_id>_000000.nxvid
  <session_id>_000001.nxrec
  ...
```

`session_layout` по умолчанию `{session_id}`; токен `{label}` подставляется тем же значением. Имена сегментов — `<session_id>_<seq>` с шестизначным `seq`. Пути в манифесте относительны каталога сессии.

### `session.json`

```json
{
  "format": "Raw",
  "version": 2,
  "session_id": "example",
  "time_domain": "MonotonicRaw",
  "boot_token": "0123456789abcdef",
  "boot_uuid": "....",
  "time_base_epoch": 0,
  "settings": { "session_root": "...", "session_id": "example", "...": "..." },
  "schemas": [
    { "id": 1, "name": "nexigen.log.LogFrame", "version": 1,
      "encoding": "jsonschema", "units": "", "endianness": "little",
      "data": [1, 2, 3], "data_crc32c": 123456 }
  ],
  "channels": [
    { "id": 1, "schema_id": 1, "topic": "log.EXMP", "kind": "Telemetry",
      "message_encoding": "nexigen.pod", "native_domain": "monotonic_raw",
      "domain_id": 0, "domain_epoch": 0,
      "domain_class": "system_clock", "access_mode": "register_pair" }
  ],
  "segments": [
    { "seq": 0, "rec_path": "example_000000.nxrec",
      "idx_path": "example_000000.nxidx", "vid_path": "example_000000.nxvid",
      "closed": true, "records": 10, "gaps": 0,
      "rec_bytes": 5401, "vid_bytes": 0,
      "start_commit_ns": 0, "end_commit_ns": 0 }
  ],
  "clean_close": true
}
```

`settings` — полный снимок `StorageSettings`, поэтому сессия несёт лимиты, с которыми была записана. `schemas` и `channels` — полные таблицы, а не ссылки. `segments` — каталог с `closed`, счётчиками и байтами. `clean_close` true только после футера последнего сегмента.

### `.nxrec`: записи

Заголовок записи — те же 48 байт, что разобраны выше (`RCD1`, тип, канал, payload, commit time, sequence, publisher, два CRC). Дальше идёт payload, тип которого задаёт `RecordType`:

| Type | Значение | Payload | Замечание |
|---|---|---|---|
| `ChannelDeclare` | 1 | JSON `ChannelDeclare` | канал объявляется до первой записи |
| `SchemaDeclare` | 2 | JSON `SchemaDeclare` | полный дескриптор схемы |
| `LogFrame` | 3 | `nexigen.logging::LogFrame` | байт-в-байт, без декодирования |
| `MetricFrame` | 4 | `nexigen::metrics::MetricFrame` | байт-в-байт |
| `SpanFrame` | 5 | `nexigen::tracing::SpanFrame` | байт-в-байт |
| `ComPayload` | 6 | opaque | событие или произвольный payload |
| `VideoFrameRef` | 7 | `VideoFrameRef` (64 Б) | ссылка в `.nxvid` |
| `VideoStreamConfig` | 8 | `VideoStreamConfig` (1056 Б) | codec, размеры, extradata |
| `GapMarker` | 9 | `GapMarkerPayload` (24 Б) | диапазон потерь |
| `TimeSync` | 10 | `TimeSyncPayload` (16 Б) | монотонное → UTC |
| `TimeBase` | 11 | `TimeBasePayload` (4224 Б) | история доменов |
| `TimeSnapshot` | 12 | `chrono::TimeSnapshot` (896 Б) | 1 Hz значения |

Записи `ChannelDeclare` и `SchemaDeclare` не попадают в разреженный индекс (`track_index = false`), но считаются в `SegmentInfo.records`. Payload копируется в буфер writer-а; в `.nxrec` нет выравнивания между записями, кроме естественного заголовка.

### `.nxidx`: разреженный индекс

`IndexEntry` — 64 байта:

| Offset | Size | Поле | Смысл |
|---|---|---|---|
| 0 | 8 | `commit_ns` | commit time первой записи интервала |
| 8 | 8 | `rec_offset` | начало интервала в `.nxrec` |
| 16 | 8 | `min_event_ns` | минимальное event-время |
| 24 | 8 | `max_event_ns` | максимальное event-время |
| 32 | 8 | `channel_mask` | бит `id` для каналов 1..63 |
| 40 | 8 | `end_rec_offset` | конец интервала |
| 48 | 4 | `record_count` | число записей |
| 52 | 4 | `flags` | зарезервировано, 0 |
| 56 | 4 | `entry_crc32c` | CRC по байтам до этого поля |
| 60 | 4 | `reserved` | 0 |

Интервал закрывается по `index_stride_bytes` (256 КиБ) или `index_interval_ns` (50 мс). Индекс не является источником истины: `recover_index()` перестраивает его по `.nxrec`, а отсутствие `.nxidx` не делает сессию нечитаемой.

### `.nxvid`: видео

Файл начинается с 4 KiB-выравнивания; каждый кадр лежит по 4 KiB-выровненному смещению, между кадрами — нулевой padding. Кадр не парсится и не перекодируется.

`VideoFrameRef` — 64 байта:

| Offset | Size | Поле |
|---|---|---|
| 0 | 8 | `vid_offset` |
| 8 | 8 | `capture_mono_ns` |
| 16 | 8 | `source_sequence` |
| 24 | 8 | `config_epoch` |
| 32 | 4 | `vid_size` |
| 36 | 4 | `vid_crc32c` (0 при `Off`) |
| 40 | 4 | `width` |
| 44 | 4 | `height` |
| 48 | 2 | `codec` |
| 50 | 2 | `bitstream` |
| 52 | 4 | `vid_segment_seq` |
| 56 | 8 | `reserved` |

`VideoStreamConfig` — 1056 байт: `codec`/`bitstream`/`width`/`height` (12 байт), `timebase_num`/`timebase_den` (8), `extradata_size`/`reserved0` (4), `config_epoch` (8) и `extradata[1024]`. Он пишется перед первым кадром канала или при смене epoch.

### Футер и версия

`SegmentFooter` — 64 байта:

| Offset | Size | Поле |
|---|---|---|
| 0 | 4 | `magic` = `FTR1` |
| 4 | 4 | `version` = 2 |
| 8 | 8 | `total_records` |
| 16 | 8 | `total_gaps` |
| 24 | 8 | `rec_bytes` |
| 32 | 8 | `vid_bytes` |
| 40 | 8 | `index_entries` |
| 48 | 8 | `end_commit_ns` |
| 56 | 4 | `footer_crc32c` |
| 60 | 4 | `magic_end` = `FTR1` |

`kFormatVersion = 2`; магия записи `RCD1`, футера `FTR1`. `TimeBasePayload` несёт layout-магию `TBP2`: payload без неё считается legacy и не переинтерпретируется. Все wire-размеры закреплены `static_assert` (`wire_types.hpp`; `TimeSnapshot` — в `chrono/time_snapshot.hpp`); изменение раскладки — это изменение формата и версии, а не локальная правка структуры.

## Читатель, инвентарь и проверка

`SessionReader` — холодная, read-only сторона. Он открывает `session.json`, валидирует манифест и каталог сегментов, умеет открыть и одиночный `.nxrec` без манифеста (`open_segment`), вывести имена `.nxidx`/`.nxvid` из имени записи и восстановить индекс на лету.

Его основные операции:

- `datasets()` — инвентарь по объявленным каналам. Читает заголовки записей, но не payload и не видео-байты. Возвращает `DatasetInfo`: канал, вид, имя, схему, encoding, native-домен, codec/размеры/extradata, `records`, `gaps`, `payload_bytes`, границы event-времени, `closed`, `config_epochs`, поля схемы и разбивку по сегментам.
- `for_each(channel, has_channel, start, end, has_range, visitor, context)` — обход записей с фильтром по каналу и event-времени. Индекс сужает скан, но записи всё равно проверяются по `event_ns`, потому что событийное время не обязано быть монотонным.
- `verify(records, gaps, failure)` — проверка CRC записей и футеров. При поломке отдаёт `segment_seq`, `offset`, `channel_id`.
- `read_video(ref)` — читает кадр из `.nxvid` по `vid_segment_seq` и проверяет необязательный CRC.
- `recover_index(segment_seq)` — перестраивает индекс по `.nxrec`, если `.nxidx` потерян или обрезан.
- `publisher_epochs()` — эпохи публикации для анализа разрывов.

`DatasetInfo` — это не «сырые записи», а инвентарь. `list` и `info` в `nexigen-export` рендерят его без чтения payload: можно понять, что в сессии есть, сколько это занимает и где лежит, не декодируя ни одного сообщения.

## Экспорт на выходе

`nexigen-export` — on-demand конвертер. Один запуск, один результат, выход. Ни демона, ни `--follow`, ни `--watch`, ни фоновой конвертации. Это не ограничение реализации, а контракт: сессия уже завершена или закрыта, и читать её можно сколько угодно раз.

Он умеет `list`, `inspect`, `info`, `extract`, `verify`. `extract` выбирает датасеты по id/имени и каналы, применяет event-окно и пишет JSONL, Perfetto, MCAP или elementary video. Выбор описывает `ExportSelection`: основной датасет, дополнительные каналы и инклюзивные границы `start_event_ns`/`end_event_ns` с флагами `has_start`/`has_end`. `--output` обязателен; неизвестные аргументы и неизвестные форматы — ошибка использования. MP4/MKV зарезервированы и возвращают `UnsupportedFormat` до появления `nexigen_media`.

Декодирование идёт только по встроенному описанию:

- `jsonschema`/`nexigen.pod.v1` с field layout — fixed-offset POD: little-endian скаляры, массивы, `char[N]`;
- `jsonschema`/`json` без layout — payload парсится как JSON;
- `protobuf` — `FileDescriptorSet` валидируется структурно, payload идёт как opaque base64;
- `flatbuffer` — `.bfbs` валидируется, payload идёт как opaque;
- `ros2msg` — текст схемы сохраняется, payload идёт как opaque;
- неизвестный encoding — `UnsupportedFormat`, без угадывания.

MCAP собирается как JSON-записи с генерируемой JSON-схемой; канал сохраняет объявленный schema/message encoding. JSONL и Perfetto объединяют выбранные записи, elementary video требует ровно один датасет и пишет таймстампы в sidecar. Для полного списка команд и полей — [`docs/recorder.md`](../recorder.md).

## Полный пример: сессия за один `main`

Полный исходник — `examples/recorder/ex_recorder_session.cpp`. Он собирается и проходит CTest, использует только публичный API рекордера и не требует COM, MCAP или медиа-бэкенда.

```bash
cmake --build build-debug -j 64 --target ex_recorder_session
./build-debug/examples/recorder/ex_recorder_session
```

Что делает пример:

1. **Пишет** сессию: `StorageSettings` с принудительной ротацией (`max_segment_records = 10`), boot token/uuid, встроенная схема `nexigen.log.LogFrame` и пользовательская POD-схема, три канала (log, telemetry, video).
2. **Проходит оба producer-пути**: три `LogFrame` через прямой `append()`, три конверта через `RecordShard`/`append_from_shard()`.
3. **Добавляет время и видео**: `append_time_sync()` с текущим UTC-смещением и один RGB24-кадр через `append_video()` с `VideoCrcMode::PerFrame`.
4. **Фиксирует потерю**: один `GapMarker` с `IoBackpressure`, чтобы inventory показал gap.
5. **Читает обратно**: `SessionReader::open()`, `manifest()`, `datasets()`, `for_each()`, `recover_index()`, `publisher_epochs()`, `read_video()`, `verify()`.

Фактический вывод на этой машине:

```text
session=/tmp/nexigen-example-recorder/example segments=3 schemas=2 channels=3 clean=true
  segment 0 closed=true records=10 gaps=0 rec=5401 vid=0
  segment 1 closed=true records=10 gaps=1 rec=5789 vid=12
  segment 2 closed=true records=5 gaps=0 rec=4385 vid=0
  dataset 1 log.EXMP             kind=Telemetry records=4 gaps=1 fields=9 bytes=792
  dataset 2 example/telemetry    kind=Telemetry records=3 gaps=0 fields=1 bytes=12
  dataset 3 example/video        kind=Video records=1 gaps=0 fields=1 bytes=12
for_each records=25 video_ref=true
recovered_index segment=0 entries=2
  epoch channel=1 publisher=1 first=0 last=10 records=4
  epoch channel=2 publisher=7 first=0 last=2 records=3
  epoch channel=3 publisher=0 first=0 last=1 records=2
video bytes=12 first=0x11
verify records=25 gaps=1 OK
```

Ключевой каркас (полный код — в файле):

```cpp
auto writer = recorder::SessionWriter::create(settings);
writer->declare_schema(log_frame_schema);
auto log_ch = writer->declare_channel("log.EXMP", 1, recorder::DatasetKind::Telemetry,
                                      "nexigen.pod");
writer->append(recorder::RecordType::LogFrame, *log_ch, frame_bytes, event_ns, seq, publisher);
writer->append_from_shard(shard);          // SPSC producer path
writer->append_time_sync(utc_offset);
writer->append_video(*video_ch, config, ref, frame_bytes);
writer->close();

auto reader = recorder::SessionReader::open(session_dir);
reader->datasets();                        // inventory, no payload reads
reader->for_each(0, false, 0, 0, false, &visitor, &state);
reader->recover_index(0);                  // rebuild .nxidx from .nxrec
reader->publisher_epochs();
reader->read_video(ref);                   // verify optional per-frame CRC
reader->verify(records, gaps, &failure);
```

Три сегмента в выводе — это не украшение: пример действительно пересекает границу ротации, а `verify` считает записи уже по закрытым сегментам. Видео-кадр лежит в сегменте 1 (`vid=12`), и `read_video` находит его по `vid_segment_seq`, а не по текущему сегменту. `recovered_index` перестраивает индекс первого сегмента независимо от того, что `.nxidx` уже существует.

## Почему не MCAP

Справедливый вопрос: зачем свой формат, если есть MCAP?

Потому что у рекордера и у формата обмена разные задачи. MCAP — это контейнер для обмена и инструментов; рекордер — это write path реального времени, который обязан не блокировать продюсера, переживать падение и сохранять видео байт-в-байт. Эти требования тянут в разные стороны.

Свой raw-формат даёт:

- **фиксированные wire-структуры**, пиннутые `static_assert`: `RecordHeader` 48, `IndexEntry` 64, `VideoFrameRef` 64, `SegmentFooter` 64. Перестановка поля ломает сборку, а не файл;
- **предвыделенный write path**: буферы создаются в `create()`, `append()` не растёт и не аллоцирует;
- **CRC-32C на каждой записи** и на футере, без зависимости от внешней библиотеки;
- **4 KiB-выравнивание видео** и ссылки, которые переживают recovery;
- **отсутствие COM и producer SDK** в storage-слое: рекордер не линкует iceoryx2, logging, metrics или tracing;
- **разреженный индекс**, который можно потерять и перестроить.

Цена: сессию нельзя открыть «любым MCAP-инструментом». Для этого есть `nexigen-export`: конвертация в стандартные форматы происходит на выходе, on demand, а не на горячем пути. Это тот же приём, что и в [статье про логирование](logging.md): на входе — минимальная и предсказуемая запись, на выходе — богатые представления.

MCAP при этом не отвергается. Он поддерживается как выход, опционально (`-DNEXIGEN_ENABLE_MCAP=ON`), с сохранением схем и encoding. Но он не становится форматом хранения по умолчанию, потому что цена записи и зависимость от библиотеки важнее экосистемности на этом слое.

## Видео: байты, а не декодер

Видео в рекордере — это blob-хранилище, а не медиа-пайплайн.

`.nxvid` начинается с 4 KiB-выравнивания, и каждый кадр лежит по 4 KiB-выровненному смещению. Рекордер не декодирует, не перекодирует и не парсит битстрим: он принимает уже сжатые байты и записывает их как есть.

`VideoFrameRef` — 64 байта: `vid_offset`, `capture_mono_ns`, `source_sequence`, `config_epoch`, `vid_size`, `vid_crc32c`, `width`, `height`, `codec`, `bitstream`, `vid_segment_seq`. `source_sequence` сохраняется дословно, поэтому можно отличить потерю кадра от переупаковки. `config_epoch` связывает кадр с конфигурацией энкодера.

`VideoStreamConfig` — 1056 байт: codec, bitstream, размеры, timebase, `config_epoch` и до 1024 байт extradata. Он пишется перед первым кадром канала или при смене epoch. Этого достаточно, чтобы распаковать или перемуксовать поток без исходного энкодера.

CRC кадра опционален. `VideoCrcMode::Off` — дефолт и быстрый путь; `PerFrame` покрывает точные байты кадра и проверяется в `read_video()`. Футер сегмента всё равно покрывает метаданные.

Потери видео фиксируются теми же `GapMarker`, но с причинами `EncoderOverload` или `StreamDiscontinuity`. `lost_count` и `first_lost_sequence` позволяют отличить «энкодер не успел» от «источник прыгнул». Экспортёр отдаёт элементарный поток и sidecar с таймстампами; MP4/MKV — следующий шаг после `nexigen_media`.

## Ротация и retention

Сегмент — это единица ротации и удаления. `StorageSettings` задаёт бюджет:

| Поле | По умолчанию | Смысл |
|---|---|---|
| `max_segment_bytes` | 1 ГиБ | `.nxrec` + `.nxvid`; триггер ротации, не жёсткий лимит записи |
| `max_segment_duration_ns` | 60 с | ротация по wall-clock |
| `max_segment_records` | 0 | 0 = без лимита по числу записей |
| `index_stride_bytes` | 256 КиБ | шаг разреженного индекса |
| `index_interval_ns` | 50 мс | второй триггер индекса |
| `write_buffer_bytes` | 1 МиБ | предвыделенный буфер записей |
| `index_buffer_bytes` | 64 КиБ | буфер индекса |
| `video_frame_buffer_bytes` | 4 МиБ | максимальный принимаемый кадр |
| `max_record_bytes` | 1 МиБ | максимальный payload записи |
| `retention_max_bytes` | 0 | 0 = без ограничения |
| `min_free_bytes` | 10 ГиБ | порог свободного места для нового сегмента |
| `retention` | `Circular` | удалять старые закрытые тройки или остановиться |
| `video_crc` | `Off` | CRC на кадр |

Байтовый лимит — триггер, а не жёсткая граница: одна большая запись может перевалить за `max_segment_bytes` и не разбивается на части. Это лучше, чем рвать запись на два сегмента и ломать её целостность.

При ротации writer сначала финализирует текущий сегмент (футер, индекс, `fsync`), затем применяет retention, проверяет свободное место и только потом открывает следующий. Порядок важен: сначала закрыть, потом удалять, потом открывать. `Circular` удаляет только закрытые тройки; активный сегмент не трогается. `Stop` финализирует и останавливает приём, а не удаляет данные. Если свободного места меньше `min_free_bytes`, новый сегмент не открывается, writer переходит в halted-состояние и помечает сессию закрытой.

## Границы

- **CRC-32C — целостность, не аутентификация.** Она ловит порчу и обрыв, но не подмену.
- **Нет `fsync` на каждую запись.** Гарантия — до последнего сброса; падение питания может потерять хвост.
- **Один writer — последовательный ресурс.** Пропускная способность ограничена им и диском; переполнение шардов видно как `GapMarker`.
- **Шард ограничен 4096 байтами payload.** Крупные записи идут через прямой `append()` и не должны быть горячим путём.
- **Лимиты фиксированы:** 64 канала, 64 схемы, схема 256 КиБ, extradata 1024 байта, 32 строки time base; payload ограничен `max_record_bytes` (по умолчанию 1 МиБ).
- **Индекс разреженный.** Запрос по времени сужает скан, но не гарантирует отсутствие per-record фильтра.
- **Legacy time-base не читается.** Payload без `TBP2` помечается как legacy; конвертера старых сессий нет.
- **Нет live-tail.** `nexigen-export` — один запуск, без `--follow`/`--watch`.
- **Нет MP4/MKV.** Только elementary video + sidecar до появления `nexigen_media`.
- **Storage-слой COM-free.** `nexigen-recd` — отдельный коллектор, который подписывается на сервисы и кормит writer; сама библиотека записи от транспорта не зависит.
- **`clean_close = false` — это восстановленный префикс, а не «сессия сломана».**
- **Пути безопасны:** `session_root` абсолютный, `session_layout` относительный, без `..`, только токены `{session_id}`/`{label}`.

## Сводка

```text
  продюсеры
     |
     | RecordEnvelope (4096 Б), SPSC 1024
     v
  RecordShard ---- переполнение ---> GapMarker(IoBackpressure)
     |
     v
  SessionWriter (один поток) ---> .nxrec + .nxidx + .nxvid + session.json
     |                                   |
     |                                   +-- схемы и каналы встроены
     |                                   +-- time base записан как данные
     |                                   +-- футер = чистое закрытие
     v
  SessionReader ---> datasets / for_each / verify / read_video
     |
     v
  nexigen-export ---> JSONL / Perfetto / MCAP / elementary
```

Рекордер держится на трёх решениях. Продюсер не пишет в файл — он платит за конверт и `try_push`, а полный шард превращается в честный `GapMarker`. Writer один — он владеет порядком, ротацией, retention и индексом, и именно поэтому запись остаётся последовательной и восстановимой. Сессия самодостаточна — схемы, каналы и история времени лежат внутри, поэтому её можно открыть без SDK и без демона, а стандартные форматы получить на выходе, on demand.

Цена этой схемы — фиксированные лимиты, один последовательный writer, отсутствие per-record `fsync` и собственный формат вместо готового MCAP. Взамен продюсер никогда не ждёт диск, падение процесса оставляет читаемый префикс, а артефакт переживает машину, на которой был записан.
