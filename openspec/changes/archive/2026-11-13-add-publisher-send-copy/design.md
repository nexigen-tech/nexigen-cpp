# Design

## Context

См. `proposal.md` — «Why». Здесь только то, что определяет реализацию.

Текущий путь публикации кадра:

```
sink.on_span(record)
  frame_ = map(record)                       // 384 Б scratch в объекте sink
  lost_before = publisher_->lost_samples()   // relaxed load
  loaned = publisher_->loan_uninit()         // FFI -> Rust: loan_slice_uninit(1)
  sample = loaned->write_payload(frame_)     // by-value параметр (копия 384 Б),
                                             //   placement-new в слот (копия 384 Б)
  publisher_->send(std::move(sample))        // FFI -> Rust: send + телеметрия
```

Дизассемблер Release-бинаря (`build-release-com/tests/nexigen_tracing_com_tests`, `Iceoryx2TraceSink::on_span`) фиксирует на happy path 8 вызовов `iox2_sample_mut_move`, 2 полные копии кадра (6 × `vmovdqa64` каждая), вызовы `iox2_sample_mut_payload`, `iox2_sample_mut_send` и до 6 `iox2_sample_mut_drop`. Причина — `iox2_sample_mut_t` содержит 256-байтовый union (`iceoryx2-ffi/c/src/api/sample_mut.rs:58`), а C++-обёртка передаёт `SampleMut` по значению через несколько слоёв (`include/nexigen/com/sample_mut.hpp:73`, `include/nexigen/com/publisher.hpp:131`).

Замеры на одном стенде (`build-release-com`, Release, strict latency):

| Путь | Медиана |
|---|---:|
| `tests/com/latency_test.cpp` — `(a) direct loan/send`, 256 Б | 187.6 нс |
| `tests/tracing` — полный `on_span`, 384 Б | 210.3–220.6 нс |

Ограничения, которые дизайн не имеет права нарушить:

- `-fno-exceptions`, C++23, `std::expected`-ошибки, никаких `.value()`.
- Ноль heap-аллокаций на горячем пути после `start()`.
- `Publisher`-телеметрия — четыре `std::atomic<std::uint64_t>` в `alignas(64)`-ячейке, все обращения `memory_order_relaxed`, один писатель на счётчик.
- Wire-форматы `SpanFrame` (384 Б, `alignas(64)`), `LogFrame` (256 Б), `MetricFrame` не меняются.
- Бюджет `on_span` ≤400 нс и контракт «один кадр на событие, без Event-нотификации на спан» остаются.

## Goals / Non-Goals

**Goals:**

- Убрать обёрточный FFI-чёрн с горячего пути публикации, не меняя wire-формат и наблюдаемое поведение.
- Сохранить телеметрию потерь (`lost_samples()`) и существующие latency-гейты.
- Получить измеряемое сравнение `loan/send` vs `send_copy` в том же бенчмарке, чтобы решение опиралось на цифры.

**Non-Goals:**

- Батчинг кадров, изменение `publish_ts_ns`, `ServiceType::Local`, правки upstream iceoryx2.
- Оптимизация управляющих портов (PHM, SM, request-response): они не горячие и иногда требуют записи в слот до/после проверок.
- Рефакторинг `write_payload` в perfect-forwarding как самостоятельная задача: `send_copy` снимает необходимость в этом для hot-path, а менять сигнатуру ради редких вызовов смысла нет.

## Decisions

### D1 — `Publisher::send_copy(const T&)` как тонкий проброс, а не новая абстракция

Добавляем в `nexigen::com::Publisher<T, H>` метод:

```cpp
[[nodiscard]] Status send_copy(const T& payload) noexcept;
```

Он вызывает `inner_.send_copy(payload)` и маппит ошибку через существующий `detail::map_error`. `static_assert(std::is_trivially_copyable_v<T>)` уже гарантирован `concepts::Message<T>`, поэтому ограничение `iox2::Publisher::send_copy` выполняется автоматически.

Почему не «починить» текущую цепочку:

- perfect-forward `write_payload(const T&)` убирает одну копию 384 Б, но оставляет 8 FFI-перемещений 256-байтного union'а из `loan_uninit`/`send`/возвратов;
- передача `SampleMut&&` и `SampleMutUninit&&` убирает 2–3 перемещения, но не все;
- `send_copy` убирает всю цепочку одним вызовом и является уже поддерживаемой операцией upstream, а не нашей импровизацией.

Альтернатива «оставить как есть и поднять гейт» отклонена: гейт 400 нс — это бюджет, а не цель; оптимизация даёт запас на медленные раннеры и на рост частоты спанов.

### D2 — Телеметрия: один общий хелпер, тот же layout и порядок памяти

`send()` и `send_copy()` обновляют счётчики одинаково. Чтобы не дублировать логику и не разъехаться в семантике, выделяем приватный `record_delivery(std::uint64_t delivered) noexcept`:

```
send_count_.fetch_add(1, relaxed)
peak = peak_recipients_.load(relaxed)
if (delivered < peak)  lost_samples_.fetch_add(peak - delivered, relaxed);
                       queued_samples_.fetch_add(1, relaxed);
else if (delivered > peak) peak_recipients_.store(delivered, relaxed);
```

`send()` передаёт в хелпер `*sent`, `send_copy()` — `*sent` из `iox2::Publisher::send_copy`. Все четыре атомика остаются `std::atomic<std::uint64_t>` в `Telemetry` с `alignas(64)` (как сейчас, `include/nexigen/com/publisher.hpp:178`), все операции — `memory_order_relaxed`: это диагностика, а не управляющий сигнал, и писатель у счётчиков один (поток-публикатор). Новых атомиков и новых полей нет, false sharing не добавляется.

`send_copy` не меняет `sequence_`: его инкрементит `map_into_frame` до попытки, поэтому пропуск номера при отброшенном кадре сохраняется.

### D3 — Слияние ошибок loan/send допустимо, потому что синки их уже не различают

`iox2::Publisher::send_copy` возвращает один `SendError` и на неудачный loan, и на неудачный send. Все три hot-path синка уже трактуют оба случая одинаково:

- `Iceoryx2TraceSink::on_span` — `++dropped_`, вернуть `Ok` (спека «Publication never blocks and drops are counted»);
- `logging::detail::Iceoryx2Sink::handle` — молча пропустить кадр и не отправлять `notify()`: будить демон без опубликованного кадра незачем (в старой схеме ошибка `send()` игнорировалась и notify всё равно уходил);
- `Iceoryx2MetricSink::on_batch` — `continue`, кадр не считается опубликованным; `published` инкрементируется только после успешного `send_copy`, поэтому notify уходит лишь когда в батче есть хотя бы один доставленный кадр.

Метод всё равно возвращает `Status`, чтобы будущий вызывающий мог увидеть сбой; менять контракт `send()` не нужно.

Порядок снимка потерь в trace sink сохраняется строго: `lost_before = publisher_->lost_samples()` **до** `send_copy`, сравнение — **после**. Иначе счётчик потерь, обновлённый внутри вызова, будет не с чем сравнить.

### D4 — Переводим только три hot-path синка

| Сайт | Решение | Причина |
|---|---|---|
| `tracing::Iceoryx2TraceSink::on_span` | `send_copy(frame_)` | главная цель, самый частый путь |
| `logging::detail::Iceoryx2Sink::handle` | `send_copy(frame_)` | тот же паттерн, 256 Б, consumer-поток логгера |
| `metrics::Iceoryx2MetricSink::on_batch` | `send_copy(frame_)` в цикле | тот же паттерн, пакетная публикация |
| `phm::Recovery`, `sm::TransitionPreparation`, request-response | оставить `loan_uninit`/`send` | редкие управляющие сообщения; выгоды нет, риск лишний |

Синки остаются worker-confined: `frame_` — scratch в объекте sink, `send_copy` не удерживает слот между вызовами, состояние sink не меняется.

### D5 — Требование `tracing/iceoryx2-trace-sink` не переписываем

Формулировка требования говорит «loan a shared-memory slot, write the frame in place, and send». После изменения эти действия выполняет транспорт внутри одного `send_copy`: слот всё равно берётся из shared-memory пула, кадр всё равно попадает в него одной копией, доставка остаётся zero-copy (между publisher и subscriber копий нет). Наблюдаемый контракт — один `SpanFrame` на `on_span`, отсутствие аллокаций, счётчик дропов, последовательность, бюджет 400 нс — не меняется. Поэтому спека трассировки остаётся как есть, а изменение фиксируется только в `com/publish-subscribe`.

### D6 — Бенчмарк: отдельная ветка `(a2)`, а не замена `(a)`

В `tests/com/latency_test.cpp` добавляется второй прямой путь рядом с существующим:

```
(a)  loan_uninit -> write_payload -> send
(a2) send_copy(frame)
```

Оба меряются в одном прогоне, на одном кадре 256 Б, с той же калибровкой `ns/tick`, прогревом, вычитанием overhead и best-of-3 в strict-режиме. Гейт `(a+event) ≤ 2 мкс` не трогаем; новый ассерт не вводим — сравнение печатается, решение о пороге принимается после цифр. Тест нулевых аллокаций (`mallinfo2`) покрывает оба пути.

Latency-гейт трассировки остаётся прежним: он меряет полный `on_span` и автоматически покажет эффект.

### D7 — Память и конкурентность

- Новых полей в `Publisher` нет: `send_copy` — нестатическая функция, состояние не добавляется.
- `Telemetry` сохраняет `alignas(64)` и четыре relaxed-атомика; меняется только код, который их обновляет (общий хелпер).
- Per-thread shard sizing трассировки (`TracerProvider` SPSC-кольца) не затрагивается: меняется только consumer-поток, который вызывает `on_span`.
- `send_copy` делает один loan и один `memcpy` `sizeof(T)` в разделяемую память; для 384 Б это те же 6 кэш-линий, что и раньше, но без промежуточной копии в параметр и без 256-байтных перемещений union'а.

## Risks / Trade-offs

- **[`send_copy` окажется не быстрее: Rust-часть остаётся]** → мерить веткой `(a2)` на том же стенде; если выигрыш <10 нс, откатить правки сайтов, оставив метод и бенчмарк как диагностический инструмент.
- **[Слияние loan/send-ошибок скрывает причину дропа]** → сейчас ни один вызывающий причину не различает, а `dropped_` уже один счётчик; если понадобится, отдельные счётчики добавятся позже без изменения wire-формата.
- **[`lost_samples()` перестанет ловить потери, если снимок взять после вызова]** → порядок зафиксирован в D3 и покрывается существующим `test_overflow_and_gap`.
- **[Rust `copy_nonoverlapping` медленнее нашего zmm-цикла]** → 384 Б укладываются в несколько векторных инструкций, разница в пределах шума; проверяется замером `on_span`.
- **[Расхождение с буквальной формулировкой спеки трассировки]** → D5 фиксирует, что изменился только способ вызова транспорта, а наблюдаемый контракт и бюджет те же; при желании формулировку можно уточнить отдельным change'ем.
- **[Шум на общих CI-раннерах]** → строгие ассерты по-прежнему только в strict Release x86-64; в остальных конфигурациях числа печатаются.

## Migration Plan

1. Добавить `send_copy` и общий телеметрийный хелпер; существующие вызовы не трогать.
2. Перевести trace sink, прогнать его тесты и latency-гейт; зафиксировать цифру.
3. Перевести logging и metrics; прогнать их com-тесты.
4. Добавить ветку `(a2)` в `tests/com/latency_test.cpp`, снять сравнение.
5. Полная матрица Debug/Release/TSan/ASan/Clang (см. `tasks.md`).

Откат — по одному сайту: возврат на `loan_uninit`/`write_payload`/`send` не требует изменений в других модулях, метод `send_copy` можно оставить неиспользуемым.
