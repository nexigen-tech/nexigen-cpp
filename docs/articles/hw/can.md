# `hw::can`: CAN ISO-11898 через iceoryx2

`nexigen::hw::can` — hardware-слой CAN: единая модель сообщений, контракт
драйвера, мосты между драйвером и iceoryx2, прикладной многоканальный клиент и
виртуальное железо для отладки без стенда.

## Слои

```text
+----------------------+        +----------------------+        +-------------------+
| CAN hardware /       |        | driver-side bridge   |        | application side  |
| nexigen-canvbus      |<------>| nexigen-canrx        |<------>| nexigen-canmon    |
|                      |        | nexigen-cantx        |        | user CanClient    |
+----------------------+        +----------------------+        +-------------------+
```

Драйвер отдаёт уже готовый поток: кадры, error/overload, bus-idle heartbeat,
аппаратные timestamp/sequence и флаги ошибок. Слой не пересчитывает и не
подменяет эти поля.

## Модель `CanMessage`

| Поле | Значение |
|---|---|
| `kind` | `ClassicData`, `FdData`, `Remote`, `Error`, `Overload`, `BusIdle` |
| `channel_id` | числовая привязка к каналу |
| `can_id` / `extended` | 11- или 29-битный идентификатор |
| `dlc` / `data[64]` | classic 0..8, CAN FD 0..64 |
| `hardware_timestamp_ns` | аппаратная метка в нс |
| `hardware_sequence` | аппаратный sequence, не транспортный |
| `error_flags` | bit/stuff/CRC/form/ack/bus-off/warning/passive/... |
| `origin` | `LocalTransmit`, `ExternalDevice`, `Unknown` |
| `frame_flags` | BRS/ESI и наличие timestamp/sequence |
| `idle_duration_ns` | длительность тишины для `BusIdle` |

`EventHeader.sequence_id` — транспортный sequence iceoryx2; он всегда
отличается от `hardware_sequence`.

## Сервисы

```text
nexigen/hw/can/<channel>/rx        полный поток драйвера
nexigen/hw/can/<channel>/tx        запросы на передачу
nexigen/hw/can/<channel>/status    CanChannelStatus
nexigen/hw/can/<channel>/vbus/rx   виртуальный драйвер -> canrx
nexigen/hw/can/<channel>/vbus/tx   cantx -> виртуальная шина
```

`rx` несёт data/remote/error/overload/bus-idle; потребитель фильтрует по
`kind`, если ему нужны только кадры.

## Запуск

Все инструменты требуют `--app-id` и минимум один канал:

```bash
nexigen-canrx --app-id CANRX --channel can0=can0
nexigen-cantx --app-id CANTX --channel can0=can0
nexigen-canmon --app-id CANMON --channel can0=can0
```

Для запуска без `nexigen-chronod` добавьте `--embedded`. Конфигурация каналов
может быть вынесена в JSON:

```json
{
  "channels": [
    {
      "name": "can0",
      "channel_id": 1,
      "driver_channel": "can0",
      "bit_rate": 500000,
      "fd": false
    }
  ]
}
```

```bash
nexigen-canrx --app-id CANRX --config can.json
```

## Монитор

```bash
nexigen-canmon --app-id CANMON --channel can0=can0
nexigen-canmon --app-id CANMON --channel can0=can0 --json
nexigen-canmon --app-id CANMON --channel can0=can0 --errors-only
nexigen-canmon --app-id CANMON --channel can0=can0 --filter-id 0x123
nexigen-canmon --app-id CANMON --channel can0=can0 --filter-kind FdData
nexigen-canmon --app-id CANMON --channel can0=can0 --filter-origin ExternalDevice
```

Монитор подписывается на `rx` и `status`, складывает сообщения в bounded
display queue и печатает их отдельным render-потоком. JSONL-режим сохраняет все
поля `CanMessage`/`CanChannelStatus`.

## Виртуальное железо

`nexigen-canvbus` изображает шину и драйвер. Штатные `canrx`/`cantx` запускаются
с `--backend virtual`, поэтому прикладная сторона и сервисы `rx`/`tx`/`status`
не меняются.

```bash
nexigen-canvbus --app-id VBUS --channel can0=can0 --embedded
nexigen-canrx --app-id CANRX --channel can0=can0 --backend virtual --embedded
nexigen-cantx --app-id CANTX --channel can0=can0 --backend virtual --embedded
nexigen-canmon --app-id CANMON --channel can0=can0 --embedded --json
```

Виртуальная шина замыкает `tx` на `rx` с origin `LocalTransmit`, присваивает
simulated timestamp/sequence и публикует `BusIdle`, когда трафик стихает.

Отладочные режимы:

```bash
nexigen-canvbus --app-id VBUS --channel can0=can0 --embedded \
  --seed 42 --duration-ms 10000 --latency-ns 20000 --jitter-ns 5000 \
  --external --inject-errors --inject-overload --inject-drop
```

- `--seed` делает последовательность ошибок/дропов воспроизводимой;
- `--duration-ms` завершает шину по `SIGTERM` через заданное время;
- `--latency-ns`/`--jitter-ns` задают задержку loopback;
- `--external` добавляет периодические кадры с origin `ExternalDevice`;
- `--inject-errors`, `--inject-overload`, `--inject-bus-off`,
  `--inject-warning`, `--inject-passive` добавляют соответствующие сообщения;
- `--inject-drop` и `--inject-duplicate` имитируют потерю и дублирование;
- `--inject-reorder` меняет порядок парных кадров, `--inject-burst` выдаёт
  серию из трёх кадров.

## Прикладной клиент

`nexigen::hw::can::CanClient<Policy>` строится на `App<Policy>` и позволяет
одному процессу одновременно подписываться на `rx` нескольких каналов и
публиковать в `tx` нескольких каналов. Callback'и выполняются на потоке
`App::run()`; долгую работу нужно выносить в собственные очереди.

## Аппаратный адаптер

`--backend hardware` требует адаптер `CanDriver`, который поставляет
интерфейсный модуль. В репозитории есть заготовки:

```bash
nexigen-canrx-hw --app-id CANRX --channel can0=can0
nexigen-cantx-hw --app-id CANTX --channel can0=can0
```

Обе используют `VendorCanDriver` (`include/nexigen/hw/can/com/vendor_can_driver.hpp`).
Заполните `open_channel`, `submit` и `close_channel` вызовами SDK; остальной
тракт (`CanReceiver`/`CanTransmitter`, сервисы и монитор) менять не нужно.
Пока адаптер не заполнен, приложение завершается с диагностикой
`NotInitialized`.

## Границы

- Конкретный аппаратный адаптер `CanDriver` поставляется интерфейсным модулем;
  в этом change реализован контракт, `VirtualCanDriver` и две заготовки
  `nexigen-canrx-hw`/`nexigen-cantx-hw`.
- `CanMessage` — 112 байт, trivially copyable, без динамической памяти.
- Приёмный путь: driver callback -> bounded SPSC ring -> `EventProducer`.
  Аллокаций, блокировок и форматирования в hot path нет.
