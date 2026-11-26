# Техническое задание: Модуль `nexigen::canopen` (Редакция 1.1)

## 1. Назначение и область применения
Модуль `nexigen::canopen` реализует стек протокола CANopen (CiA 301 / CiA 302) для hard-real-time приложений, HIL-стендов и симуляторов на базе платформы `nexigen-cpp`.

Компонент обеспечивает:
* Работу узла CANopen в жестко заданной роли (`Master` или `Slave`) на указанной шине `nexigen::hw`.
* Поддержку словаря объектов (Object Dictionary), сервисов SDO, PDO, SYNC, Heartbeat, NMT и EMCY.
* Преобразование сырых данных в физические величины (и обратно) по декларативным правилам (`scale * raw + offset` + битовые маски).
* Детерминированный внутренний C++23 API и межпроцессный доступ через `nexigen::com` (`iceoryx2`).

---

## 2. Архитектурные и нефункциональные требования

### 2.1 Ограничения среды исполнения
* **Стандарт и компиляция:** C++23, сборка с флагами `-fno-exceptions` и `-fno-rtti`. Все ошибки возвращаются исключительно через `nexigen::core::Result<T>` и `Status`. Использование `.value()` запрещено.
* **Разделение путей (Hot / Cold path):**
  * **Cold path (инициализация):** Парсинг манифестов, файлов `.dcf` и карт сигналов, валидация типов, преаллокация всей памяти. Разрешены `std::string`, `nlohmann::json` и файловый I/O.
  * **Hot path (цикл реального времени):** Обработка входящих/исходящих CAN-кадров, генерация SYNC/Heartbeat/EMCY, пересчет физических сигналов, чтение/запись в IPC. Динамические аллокации, блокирующие мьютексы и исключения строго запрещены.
* **Конкурентность:** Обмен между потоком драйвера шины, потоком стека CANopen и пользовательским потоком строится на lock-free SPSC-кольцах (`nexigen::core`) и атомарных операциях с явным `memory_order` и выравниванием `alignas(64)`.
* **Время:** Все таймеры протокола (SYNC, Heartbeat, SDO timeout, PDO inhibit/event timers) и метки времени сигналов базируются исключительно на монотонной шкале `nexigen::chrono` (`CLOCK_MONOTONIC_RAW`).
* **Метрики качества кода:** Цикломатическая сложность (CCN) любой функции ≤ 15 (гейт `check_complexity.py`). Покрытие тестами ≥ 92 %.

### 2.2 Зависимости модуля
Модуль `nexigen_canopen` располагается на верхнем уровне графа зависимостей и использует:
* `nexigen_core` (контракты ошибок, SPSC-кольца);
* `nexigen_chrono` (таймеры и канонические штампы времени);
* `nexigen_hw` (транспортный интерфейс к CAN-шине по имени);
* `nexigen_com` (обертки `iceoryx2` для межпроцессного обмена);
* `nexigen_logging`, `nexigen_metrics`, `nexigen_tracing`, `nexigen_phm` (телеметрия и контроль здоровья).

---

## 3. Функциональные требования

### 3.1 Конфигурация и поведение при старте (Манифест)
Конфигурация узла задается в манифесте сьюты/приложения (`nexigen::exec`) и включает:
1. **Имя шины CAN** (например, `can0`, `vcan0`) для подключения к `nexigen::hw`.
2. **Роль узла:** жестко `master` или `slave`.
3. **Node-ID:** идентификатор узла в сети (`1..127`).
4. **Политика старта (Startup Policy):**
   * **Для роли `slave`:**
     * `wait_for_master` (по стандарту CiA 301): отправка `Boot-up` кадра (`0x700 + NodeID`, байт `0x00`), переход в `Pre-operational` и ожидание команды NMT `Start Remote Node` от мастера.
     * `auto_operational` (автономный старт): отправка `Boot-up` кадра и немедленный самостоятельный переход в состояние `Operational` без ожидания внешнего мастера (запуск TPDO/RPDO и Heartbeat сразу после инициализации).
     * `stay_pre_operational`: переход в `Pre-operational` без запуска PDO (только SDO и Heartbeat), ожидание внешнего управления.
   * **Для роли `master`:**
     * `auto_start_network`: выполнение процедуры загрузки/проверки слейвов по списку (CiA 302) и отправка широковещательной команды `NMT Start All Nodes`.
     * `manual_start`: переход мастера в `Operational` без автоматического запуска слейвов (управление слейвами остается за прикладной логикой или IPC-командами).
5. **Путь к файлу `.dcf`:** стандартный файл конфигурации устройства (INI-формат CiA 306), описывающий Object Dictionary, дефолтные значения и настройки PDO/SYNC/Heartbeat/EMCY.
6. **Путь к файлу карты сигналов (`*.nxmap.json`):** файл-компаньон, описывающий правила маппинга физических величин и настройки экспорта в IPC.

### 3.2 Поддержка протокола CANopen
1. **Object Dictionary (OD):**
   * Хранится в виде плоской отсортированной таблицы в преаллоцированной памяти.
   * Поддерживает стандартные типы данных CiA 301 (`BOOLEAN`, `INTEGER8..64`, `UNSIGNED8..64`, `REAL32`, `REAL64`, `OCTET_STRING` фиксированной длины).
   * Контролирует права доступа (`RO`, `WO`, `RW`, `CONST`).
2. **PDO (Process Data Objects):**
   * Поддержка настраиваемых RPDO и TPDO (COB-ID, тип передачи, маппинг объектов OD).
   * Режимы передачи: синхронный (по получению/генерации SYNC, циклический и ациклический) и асинхронный (по изменению данных с учетом `inhibit time` или по `event timer`).
3. **SDO (Service Data Objects):**
   * В режиме `Slave`: `SDO Server` (Expedited и Segmented передачи).
   * В режиме `Master`: пул преаллоцированных `SDO Client` слотов для параллельного опроса/настройки слейвов с контролем таймаутов и возвратом `SDO Abort Code`.
4. **NMT и Heartbeat:**
   * Поддержка состояний `Initializing`, `Pre-operational`, `Operational`, `Stopped`.
   * В режиме `Slave`: публикация собственного Heartbeat (`0x1017`), обработка адресных и широковещательных команд NMT (`Start`, `Stop`, `Pre-op`, `Reset Node`, `Reset Communication`).
   * В режиме `Master`: отправка команд NMT, мониторинг Heartbeat ведомых узлов по таблице `0x1016` (Consumer Heartbeat Time), отслеживание повторного `Boot-up` от перезагрузившихся слейвов.
5. **SYNC:**
   * В режиме `Master`: периодическая генерация кадров SYNC (`0x1005` / `0x1006`) по таймеру `nexigen::chrono`.
   * В режиме `Slave`: прием SYNC и тактирование синхронных PDO.
6. **EMCY (Emergency, COB-ID `0x80 + NodeID`):**
   * **В режиме `Slave`:**
     * Генерация 8-байтных EMCY-кадров (`Emergency Error Code` 2 B, `Error Register 0x1001` 1 B, `Manufacturer Specific Error Field` 5 B) при возникновении ошибок.
     * Ведение стандартного массива истории ошибок в OD (`0x1003` Pre-defined Error Field) и отправка кадра сброса ошибки (`Error Code = 0x0000`) при восстановлении.
   * **В режиме `Master`:**
     * Прием EMCY-кадров от всех слейвов на шине.
     * Сохранение в преаллоцированный кольцевой буфер аварий, публикация в IPC и немедленная эскалация критических аварий в `nexigen::phm`.

### 3.3 Извлечение и преобразование физических сигналов
Файл `*.nxmap.json` определяет список сигналов. Для каждого сигнала задаются:
* **Уникальное имя** (например, `Engine.OilPressure`) и **единица измерения** (`kPa`, `degC`).
* **Источник данных:** ссылка на объект OD (`index`, `subindex`) либо прямой сырой кадр шины (`can_id`).
* **Правила извлечения (Extraction):** `bit_offset` (`0..63`), `bit_length` (`1..64`), порядок байт (`little` / `big`), признак знаковости (`signed`: `true`/`false`).
* **Линейное преобразование (Transformation):** коэффициенты `scale` и `offset` типа `double`.
  * Чтение: $V_{phys} = V_{raw} 	imes scale + offset$.
  * Запись: $V_{raw} = 	ext{round}((V_{phys} - offset) / scale)$ с проверкой на насыщение/переполнение разрядной сетки.
* **Контроль валидности и владелец (Ownership):**
  * Пределы допустимых значений (`min`, `max`) и таймаут свежести данных (`stale_timeout_ms`).
  * Направление и владелец записи: `read_only` (от шины), `local_write` (из процесса-владельца ноды) или `ipc_write` (из внешнего процесса по IPC).

---

## 4. Программные интерфейсы (API и IPC)

### 4.1 Внутренний C++23 API (внутри процесса)
* **Инициализация (Cold path):**
  * `CANOpenNode::create(const NodeConfig& cfg)` — парсинг конфигураций, аллокация ресурсов и подключение к шине.
  * `resolve_signal(std::string_view name) -> Result<SignalHandle>` — получение числового дескриптора сигнала.
  * `resolve_od_entry(uint16_t index, uint8_t subindex) -> Result<OdHandle>` — получение прямого дескриптора объекта OD.
* **Работа в цикле (Hot path):**
  * `step()` / `poll()` — тик машины состояний (вычитывание RX-колец, обработка таймеров SYNC/Heartbeat/SDO, отправка TX PDO).
  * `read_signal(SignalHandle h) -> Result<SignalSample>` — чтение физического значения (`double value`, `uint64_t timestamp_ns`, `SignalQuality quality`).
  * `write_signal(SignalHandle h, double phys_value) -> Status` — запись физического значения с обратным пересчетом и взведением флага обновления TPDO.
  * `read_od<T>(OdHandle h) -> Result<T>` / `write_od<T>(OdHandle h, T val) -> Status` — прямой типобезопасный доступ к словарю объектов.
  * `raise_emcy(uint16_t error_code, uint8_t error_reg, std::span<const uint8_t, 5> mfg_data) -> Status` (для `Slave`) — отправка аварийного сообщения на шину.
  * `clear_emcy() -> Status` (для `Slave`) — отправка кадра сброса аварии (`0x0000`).
  * `sdo_read_async(...)` / `sdo_write_async(...)` (для `Master`) — постановка запроса в SDO-клиент.

### 4.2 Межпроцессное взаимодействие (`nexigen::com` / `iceoryx2`)
Все структуры сообщений являются `trivially_copyable`, фиксированного размера и не содержат указателей:

1. **Публикация физических сигналов (Blackboard / `Field`):**
   * Сервис: `Nexigen/CANopen/<bus_name>/Signal/<signal_name>`.
   * Сообщение `SignalFrame` (`alignas(64)`): `double value`, `uint64_t timestamp_ns`, `uint32_t sequence`, `SignalQuality quality` (`Valid`, `Stale`, `OutOfRange`, `NotInitialized`).
2. **Запись управляющих сигналов извне (`Pub-Sub`):**
   * Сервис: `Nexigen/CANopen/<bus_name>/Command/<signal_name>` (создается только для сигналов с владельцем `ipc_write`).
3. **Удаленный доступ к SDO и NMT (`Request-Response`):**
   * Сервис `Nexigen/CANopen/<bus_name>/SDO`: выполнение SDO Upload/Download через слоты SDO-клиента мастера.
   * Сервис `Nexigen/CANopen/<bus_name>/NMT/Command`: отправка NMT-команд слейвам из внешних утилит/GUI.
4. **Состояние сети и аварии (`Field` и `Event`):**
   * `Nexigen/CANopen/<bus_name>/NMT/State` (`Field`) — текущее NMT-состояние узла и карта состояний слейвов.
   * `Nexigen/CANopen/<bus_name>/EMCY` (`Event` / `Pub-Sub`) — структура `EmcyFrame` (`node_id`, `error_code`, `error_register`, `mfg_data`, `timestamp_ns`).

---

## 5. Интеграция с инфраструктурой `nexigen`

1. **Интеграция с `nexigen-phmd` (Health Management):**
   * Узел регистрируется как `SupervisedEntity`.
   * Получение EMCY с шины (в режиме `Master`), таймауты Heartbeat критичных слейвов или срыв джиттера SYNC транслируются в PHM для перевода системы в `SafeState` через `nexigen-sm`.
2. **Интеграция с `nexigen-recd` (Recorder):**
   * Запись сырых CAN-кадров, EMCY-событий и декодированных `SignalFrame` в сессию `.nxrec` с единой шкалой времени `nexigen-chronod`.
3. **Интеграция с `nexigen-exec` / `nexigen-em`:**
   * Возможность запуска узла как самостоятельного инстанса сьюты (`nexigen-canopend`), конфигурируемого через манифест, либо встраивания библиотеки `nexigen_canopen` непосредственно в прикладной процесс реального времени.
4. **Тестирование и верификация:**
   * Юнит-тесты парсеров `.dcf` и `*.nxmap.json`, математики преобразований и битовых масок.
   * Интеграционные e2e-тесты на виртуальной шине (`nexigen-canvbus`): проверка автономного старта слейва (`auto_operational`), работы пары `Master + Slaves`, генерации/приема EMCY и SDO-запросов через `iceoryx2`.
