# examples/simulation-suite Specification

## Purpose

Комплексный исполняемый пример полного стека: генератор случайных векторов и два приёмника в Function Group `Simulation`, сгенерированные манифесты ExecutionManager, StateManager и PlatformHealthManagement, надзор над приложениями, автоматический подъём упавшего приёмника и сквозная телеметрия в три демона.

## Requirements

### Requirement: Three self-contained application executables

Проект SHALL предоставлять `ex_sim_generator`, `ex_sim_receiver` и `ex_sim_flaky_receiver` в `examples/integration/simulation_suite/`, каждый со своим `main` и без зависимости от тестового харнесса. Каждый SHALL быть зарегистрирован как CTest smoke-тест с меткой `examples` и в ограниченном режиме по умолчанию SHALL отрабатывать короткую нагрузку и завершаться с кодом 0 без пира, демона или манифеста.

#### Scenario: Default bounded mode exits zero

- **WHEN** любой из трёх бинарников запускается напрямую (или через CTest) с корректным каталогом настроек
- **THEN** он выполняет ограниченную работу, публикует свою телеметрию и завершается с кодом 0

#### Scenario: Harness-free startup

- **WHEN** бинарник запускается вне suite и вне демонов
- **THEN** он создаёт собственный `Infrastructure` из переданных настроек и использует только публичный API `nexigen`

### Requirement: Vector payload with canonical conversions

Пример SHALL определять `VectorSample` — фиксированный trivially copyable тип с монотонным номером вектора, меткой времени отправки и десятью значениями `double`. Тип SHALL предоставлять `to_string() const`, свободную функцию `to_string(const VectorSample&)`, `std::formatter<VectorSample>`, а также ADL-пару `to_json`/`from_json`, работающую под `JSON_NOEXCEPTION=1`, чтобы консольный вывод и диагностика использовали одно каноническое представление.

#### Scenario: Payload crosses the transport unchanged

- **WHEN** вектор публикуется генератором и принимается приёмником
- **THEN** номер, метка времени и все десять значений совпадают с отправленными, без указателей и владеющих полей

#### Scenario: Conversions round-trip

- **WHEN** `VectorSample` проходит через `to_string`, `std::format("{}")`, `to_json` и `from_json`
- **THEN** номер, метка времени и все десять значений восстанавливаются без изменений

#### Scenario: Hostile JSON leaves the sample unchanged

- **WHEN** `from_json` получает не-объект, отсутствующее поле или значение неверного типа
- **THEN** целевой `VectorSample` сохраняет прежние значения

### Requirement: Vector generation cadence

`ex_sim_generator` SHALL публиковать один вектор в секунду, пока работает, и SHALL заполнять его десять значений случайными числами. Номер вектора SHALL строго возрастать в пределах процесса, а период SHALL быть переопределяемым через переменную окружения для коротких прогонов.

#### Scenario: One vector per second

- **WHEN** генератор работает несколько секунд
- **THEN** число опубликованных векторов соответствует одному в секунду, а номера строго возрастают

#### Scenario: Vector carries ten random values

- **WHEN** приёмник получает вектор
- **THEN** он содержит ровно десять значений, и два соседних вектора различаются хотя бы одним значением

### Requirement: Pre-loaned publication

`ex_sim_generator` SHALL keep one chunk loaned ahead of the loop and SHALL take the next loan immediately after a send, so the measured publish path performs only `write_payload` and `send`. A `loan_cost_us` metric SHALL report the pre-loan cost. When the pool cannot lend, the generator SHALL fall back to loaning inside the publish path and still deliver the vector.

#### Scenario: Loan stays off the publish path

- **WHEN** the generator publishes a vector
- **THEN** the reported `publish_cost_us` excludes `loan_uninit`, and `loan_cost_us` carries the pre-loan cost taken after the previous send

#### Scenario: Empty pool falls back

- **WHEN** the pre-loan finds no free chunk
- **THEN** the next publish loans synchronously, delivers the vector, and logs no failure beyond the pool message

### Requirement: Per-vector trace propagation

`ex_sim_generator` SHALL начинать новый трейс для каждого вектора и прикреплять заголовок распространения с идентификаторами трейса и порождающего спана. Каждый приёмник SHALL извлекать заголовок и связывать свой спан обработки с входящим трейсом, так что один идентификатор трейса покрывает публикацию и оба приёма.

#### Scenario: Fresh trace per vector

- **WHEN** генератор публикует два последовательных вектора
- **THEN** заголовок каждого несёт отдельный ненулевой идентификатор трейса

#### Scenario: Both receivers link to the incoming trace

- **WHEN** приёмник обрабатывает полученный вектор
- **THEN** открытый им спан несёт тот же идентификатор трейса и указывает спан из заголовка как родительский

### Requirement: Receiver display contract

Каждый приёмник SHALL печатать в консоль номер полученного вектора, его десять значений и измеренную задержку между отправкой и приёмом. Задержка SHALL вычисляться как разница между моментом приёма и меткой времени отправки, взятой из вектора, и SHALL выводиться в микросекундах. Оба приёмника SHALL использовать один и тот же формат вывода.

#### Scenario: Vector is displayed with its index

- **WHEN** приёмник получает вектор с номером `N`
- **THEN** строка вывода содержит номер `N` и все десять значений

#### Scenario: Latency is measured from the send timestamp

- **WHEN** приёмник получает вектор с меткой времени отправки
- **THEN** напечатанная задержка равна разнице между моментом приёма и этой меткой и неотрицательна

### Requirement: Event-driven receive

`ex_sim_generator` SHALL ring `Nexigen/Simulation/VectorsEvent` after every successful publish, and both receivers SHALL block on a WaitSet attached to that event instead of polling the subscription. A fixed poll period SHALL be used only as the WaitSet timeout for the state check and as the fallback when the event channel cannot be opened. An opt-in spin mode SHALL busy-wait on the subscription instead; it SHALL be off by default and selected only by `NEXIGEN_SIM_SPIN=1`.

#### Scenario: Receiver wakes on the publish event

- **WHEN** the generator publishes a vector
- **THEN** the receiver is woken by the event, processes the queued vector, and the printed latency reflects transport and scheduling rather than a poll interval

#### Scenario: Idle receiver does not poll

- **WHEN** no vector is published
- **THEN** the receiver stays blocked in the kernel until the event or the state timeout fires

#### Scenario: Coalesced notifications still drain

- **WHEN** several publishes happen before the receiver wakes
- **THEN** one wake drains every queued vector, because the notification is a counter and the samples stay queued

#### Scenario: Spin mode trades a core for latency

- **WHEN** the receiver runs with `NEXIGEN_SIM_SPIN=1`
- **THEN** it busy-waits on the subscription, reports a fraction of the blocking-mode latency, and occupies a full CPU core

#### Scenario: Realtime policy is opt-in and degrades gracefully

- **WHEN** `NEXIGEN_SIM_RT_PRIORITY` is set and the process may use realtime scheduling
- **THEN** the receiver loop runs on `SCHED_FIFO` at that priority

- **WHEN** the host refuses the policy
- **THEN** the receiver logs a warning naming the failure and keeps running on the default policy

#### Scenario: Missing event channel falls back to polling

- **WHEN** the event listener cannot be opened
- **THEN** the receiver polls at the fallback period and still prints vectors

### Requirement: Flaky receiver terminates and the platform raises it

`ex_sim_flaky_receiver` SHALL вести себя как исправный приёмник и дополнительно SHALL завершаться каждые 15 секунд с ненулевым кодом возврата. Платформа SHALL обнаруживать завершение и запускать его снова в пределах настроенного бюджета перезапусков, а после каждого подъёма приёмник SHALL продолжать печатать принимаемые векторы. Период завершения SHALL быть переопределяемым через переменную окружения.

#### Scenario: Flaky receiver exits on schedule

- **WHEN** `ex_sim_flaky_receiver` работает дольше настроенного периода
- **THEN** он завершается с ненулевым кодом возврата

#### Scenario: ExecutionManager relaunches it

- **WHEN** платформа зафиксировала аварийное завершение приёмника
- **THEN** она запускает его снова в пределах бюджета перезапусков, и он снова принимает и печатает векторы

#### Scenario: Supervision tolerates the restart window

- **WHEN** приёмник отсутствует в течение окна перезапуска
- **THEN** его супервизия не переходит в `Expired`, потому что окно живости шире задержки перезапуска

### Requirement: Simulation Function Group with three applications

Манифесты SHALL объявлять Function Group `Simulation` и три приложения в ней: `vector_generator`, `vector_receiver` и `flaky_receiver`. Каждое приложение SHALL читать текущее состояние этой группы, опубликованное StateManager, и SHALL использовать его для собственного решения о работе; состояние SHALL быть доступно без блокировки и без опроса.

#### Scenario: Group carries all three applications

- **WHEN** suite-манифест загружается
- **THEN** все три приложения объявлены в Function Group `Simulation`

#### Scenario: Applications read the published group state

- **WHEN** StateManager публикует состояние `Simulation`
- **THEN** каждое приложение читает его неблокирующе и получает текущее значение

### Requirement: Generated manifests and daemon configuration

Скрипт-драйвер SHALL генерировать в demo-root четыре набора конфигурации: suite-манифест ExecutionManager, манифест StateManager, health-манифест PlatformHealthManagement и конфиги трёх телеметрических демонов. Сгенерированные файлы SHALL ссылаться на реальные пути сборки и demo-root, а в репозитории SHALL NOT храниться абсолютных путей конкретной машины.

#### Scenario: All four configuration sets are produced

- **WHEN** драйвер запускается
- **THEN** suite-манифест, манифест SM, health-манифест PHM и конфиги демонов существуют в demo-root и валидны для своих потребителей

#### Scenario: No machine-specific paths are committed

- **WHEN** репозиторий проверяется
- **THEN** ни один файл примера не содержит абсолютного пути конкретной машины

### Requirement: Platform integration of every application

Каждое приложение SHALL сообщать о готовности ExecutionManager, SHALL публиковать checkpoint'ы в PlatformHealthManagement на собственном сервисе, объявленном в health-манифесте, и SHALL быть покрыто его супервизией. Health-манифест SHALL задавать окно живости, допуск и правила восстановления для всех трёх приложений.

#### Scenario: Readiness reaches the ExecutionManager

- **WHEN** приложение завершило инициализацию
- **THEN** ExecutionManager получает сообщение о готовности и публикует событие жизненного цикла для PHM

#### Scenario: Checkpoints keep supervision green

- **WHEN** исправное приложение работает и отчитывается checkpoint'ами
- **THEN** PlatformHealthManagement держит его статус в `Ok`

#### Scenario: Health manifest matches the services

- **WHEN** health-манифест загружается
- **THEN** каждое из трёх приложений имеет запись с сервисом checkpoint'ов, совпадающим с тем, который публикует приложение

### Requirement: Telemetry reaches the three daemons

Все три приложения SHALL публиковать логи, метрики и спаны в три демона через iceoryx2-синки, используя имена сервисов, которые эти демоны слушают. Метрики SHALL включать счётчики отправленных и принятых векторов и измеренную задержку, а логи SHALL содержать идентификатор трейса обрабатываемого вектора.

#### Scenario: Logs, metrics, and traces reach the daemons

- **WHEN** демоны и приложения работают вместе
- **THEN** лог-демон записывает записи приложений, метрик-демон агрегирует их счётчики, а трейс-демон получает их спаны

#### Scenario: Shared trace id is observable

- **WHEN** полный прогон завершён
- **THEN** вывод лог-демона содержит записи генератора и приёмника с одним и тем же идентификатором трейса

### Requirement: Driver and cleanup scripts

Проект SHALL предоставлять скрипт-драйвер и скрипт очистки в `examples/integration/simulation_suite/`. Драйвер SHALL при необходимости собирать цели, генерировать конфигурацию, поднимать демоны, ExecutionManager, StateManager и PlatformHealthManagement в порядке зависимостей и запускать suite. Скрипт очистки SHALL останавливать все процессы и идемпотентно удалять demo-root.

#### Scenario: Driver brings the stack up in dependency order

- **WHEN** драйвер запускается на собранном дереве
- **THEN** демоны, ExecutionManager, StateManager и PlatformHealthManagement стартуют в порядке, при котором каждый следующий находит уже опубликованные сервисы предыдущего

#### Scenario: Receivers start before the generator

- **WHEN** suite запускается по сгенерированному манифесту
- **THEN** оба приёмника готовы раньше, чем генератор начнёт публикацию

#### Scenario: Cleanup is idempotent

- **WHEN** скрипт очистки запускается после прогона или когда demo-root уже отсутствует
- **THEN** все процессы остановлены, demo-root удалён, и скрипт завершается успешно в обоих случаях

### Requirement: Simulation suite declares manifest application identifiers

The generated ExecutionManager suite manifest SHALL declare an explicit, valid, suite-unique
`app_id` for each application: `SIM_GEN` for `vector_generator`, `SIM_RECV` for `vector_receiver`,
and `SIM_FLKY` for `flaky_receiver`. Logs, metrics, and spans produced in suite mode SHALL carry
that identifier; the compiled four-character tags (`GEN0`, `RECV`, `FLKY`) SHALL remain only the
standalone fallback used when no `--app-id` is supplied.

#### Scenario: Generated manifest carries explicit identifiers

- **WHEN** `run_simulation_suite.sh` writes the generated `suite.json`
- **THEN** the three application entries declare `SIM_GEN`, `SIM_RECV`, and `SIM_FLKY`, all within
  the 1..16 byte identifier contract and unique within the suite

#### Scenario: Suite telemetry uses the manifest identifier

- **WHEN** the ExecutionManager launches an application from the generated manifest
- **THEN** its `LogFrame`, `MetricFrame`, and `SpanFrame` carry the manifest `app_id`, not the
  compiled tag and not the long application name

#### Scenario: Standalone run keeps the compiled fallback

- **WHEN** an example starts without `--app-id` and without the ExecutionManager
- **THEN** its telemetry falls back to the compiled tag (`GEN0`, `RECV`, or `FLKY`) and still starts
  successfully
