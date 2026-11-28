# nexigen — глоссарий (русская версия)

Платформа C++ для систем жёсткого реального времени и симуляторов. Этот язык описывает исполнение сьюта (как приложения группируются, запускаются, супервизируются и отчитываются), платформенные процессы, которые поставляют время, здоровье, состояние, телеметрию и наблюдаемость, и словарь, которым эти процессы обмениваются с приложениями.

## Язык

Одно каноническое имя на концепцию; конкурирующие имена перечислены в `_Избегать_`. `_Спорное_` отмечает имя, которое несёт более одного смысла в этой кодовой базе или в спеках — читайте примечание, прежде чем полагаться на него.

### Исполнение suite

**Suite** (сьют, набор приложений):
Набор Applications и Function Groups, описанный одним манифестом, запускаемый и супервизируемый как единое целое.
_Избегать_: run, job

**Supervisor** (супервизор):
Detached-процесс, который запускает Applications и Function Groups сьюта и отслеживает их до остановки сьюта.
_Избегать_: runner, orchestrator
_Спорное_: `phm::Supervisor` — другая сущность: per-process health state machine внутри демона PHM, а не процесс сьюта.

**Execution Manager (EM)** (менеджер исполнения, EM):
Front-end-процесс исполнения сьюта (`nexigen-exec` / `nexigen-em`), чей supervisor владеет Suite, его экземплярами и состоянием.
_Спорное_: «EM» называет и этот процесс, и компонент дочернего процесса внутри него, который запускает и собирает Applications; «EM lifecycle» в PHM и «канал PHM→EM» относятся к процессу.

**Application** (приложение):
Объявленный исполняемый файл вместе со стартовыми конфигурациями, под которыми он может запускаться.
_Избегать_: binary, program

**App ID** (идентификатор приложения):
Задаваемый манифестом 16-байтовый код, который штампуется в каждый кадр телеметрии (`CN_LOOP_1`), уникален в пределах suite, по умолчанию равен имени приложения и передаётся ребёнку как `--app-id`. Это метка атрибуции, а не учётные данные для аутентификации.
_Избегать_: tag, app tag, app name

**App Runtime** (рантайм приложения):
Фасад `app::App<Policy>`, который владеет загрузкой настроек, режимом базы времени, жизненным циклом Infrastructure, метриками, одним входом, опциональным выходом и завершением минимального приложения.
_Избегать_: app class, application framework

**Instance** (экземпляр):
Идентичность одного Application в рамках запуска, владеющая своей директорией, сгенерированными настройками, метаданными ресурсов и выхода.
_Избегать_: process, copy

**Attempt** (попытка):
Одна попытка запуска Instance со своей директорией артефактов и идентичностями потоков; рестарт начинает следующую попытку, а не дописывает предыдущую.
_Спорное_: хаб представляет attempts как директории, а suite supervisor считает их против restart budget; оба говорят об одном запуске.

**Function Group** (функциональная группа):
Именованное подмножество Applications, разделяющих один словарь состояний; состояние группы выбирает, под какой стартовой конфигурацией работает каждое привязанное Application.
_Избегать_: group, mode

**RootFG** (корневая Function Group):
Зарезервированная Function Group, состояния которой описывают Suite целиком, а не подмножество Applications.
_Избегать_: root group

**Resting state** (состояние покоя):
Неактивное состояние, в которое Function Group возвращается после сброса: всегда `Off`.
_Избегать_: initial state, idle state
_Спорное_: SM стартует каждую группу в её первом объявленном состоянии, которое не обязано быть `Off`; «resting» — это представление неактивного состояния на стороне EM, а не начальное состояние каждой группы.

**Suite-start state** (стартовое состояние suite):
Состояние, запрашиваемое для Function Group при старте Suite: объявленное начальное состояние, иначе первое не-`Off` состояние, а для `RootFG` — `Startup`.
_Избегать_: initial state

**Off**:
Каноническое неактивное состояние Function Group, в котором её привязанные Applications не запущены. Любая группа может покоиться в `Off`, объявляет она `Off` или нет.
_Избегать_: stopped, disabled
_Спорное_: EM также считает `Stopped`, `Shutdown` и `Update` off-like, поэтому `Off` — каноническое неактивное состояние, но не единственное.

**SetState**:
Запрос перевести Function Group в именованное состояние, идентифицируемый request id и подтверждаемый acknowledgement.
_Избегать_: command, transition (transition — это то, что происходит, а не запрос)

**Restart budget** (бюджет рестартов):
Число попыток рестарта, разрешённых Application до того, как Suite сообщит о его отказе.
_Избегать_: retry limit

**Impact on Suite operability** (влияние на работоспособность suite):
Декларация в манифесте о том, что аварийное завершение одного Application делает с Suite: `failure` перезапускает весь Suite, `degrade` повторяет только это Application и деградирует Suite, `not_impact` повторяет его и оставляет состояние Suite без изменений.
_Избегать_: criticality, priority

**`SuiteState`**:
Агрегированный жизненный цикл Suite, записанный в состоянии suite: `Running`, `Degraded`, `Failed`, `Stopped`, `Unknown`.
_Избегать_: suite status (это имя проекции, а не словаря)

**Unrecoverable state** (невосстановимое состояние):
Терминальное состояние, в котором Execution Manager останавливает все управляемые процессы и запускает проектный hook; последующие записи игнорируются.
_Избегать_: fatal error

**Trusted executable** (доверенный исполняемый файл):
Application, допущенное к запуску только после совпадения объявленного SHA-256 дайджеста с исполняемым файлом, когда политика платформы требует дайджесты.
_Избегать_: verified binary

**Resource group** (группа ресурсов):
Именованная cgroup, к лимитам и бюджету которой Application присоединяется при запуске.
_Избегать_: pool, partition

### Состояние приложения

**`AppState`**:
Что Suite сообщает об Application: `Running`, `Restarting`, `Exited`, `Stopped`, `Failed`, `Unknown`.
_Избегать_: process state, status

**`ProcessState`**:
Собственный учёт жизненного цикла процесса в Execution Manager: `Idle`, `Starting`, `Running`, `Terminating`, `Terminated`.
_Избегать_: app state

**Process type** (тип процесса):
Объявленный контракт завершения Application: `daemon` работает всю привязанную фазу и обязан сообщить готовность; `oneshot` сам завершается, и код выхода 0 означает успех.
_Избегать_: process kind

**Readiness** (готовность):
Сообщение Application о том, что он достиг `Running` на своих условиях; зависимый по порядку запуск опирается на него, а не на опрос артефактов.
_Избегать_: health, heartbeat

### Runtime приложения

**Infrastructure** (инфраструктура):
Фасад runtime на стороне приложения, который владеет логгером, провайдером метрик, провайдером трассировки, привязкой к базе времени и обработкой сигналов и завершает их в фиксированном порядке.
_Избегать_: runtime, context

**Bootstrap**:
Буфер преддиагностики до появления логгера, который собирает предупреждения инициализации, а затем передаёт их.
_Избегать_: startup log

**Stop reason** (причина остановки):
Первая зафиксированная причина завершения приложения — requested, SIGINT, SIGTERM или потеря базы времени; последующие причины её не заменяют.
_Избегать_: exit code

**Run identity** (идентичность запуска):
Идентичность, которую приложение получает из окружения: run id, директория запуска, instance id и директория экземпляра.
_Избегать_: instance identity
_Спорное_: «run» здесь — это исполнение suite, к которому принадлежит приложение, а не run root хаба.

**Telemetry layout** (раскладка телеметрии):
Правило, которое определяет, куда попадает каждый поток телеметрии, из явных настроек или из run identity, чтобы обсерватория могла атрибутировать каждый поток.
_Избегать_: paths, naming

**Deterministic client** (детерминированный клиент):
Цикловой тактовый генератор без аллокаций и seeded pseudo-random sequence, которые использует приложение симулятора вместо wall time и системной энтропии.
_Избегать_: mock clock

### Язык конфигурации

**Settings** (настройки):
Сохранённая, слитая пользовательская конфигурация, которую приложение или демон читает при старте (JSON по пути настроек), включая значения, внедрённые suite.
_Избегать_: config, options
_Спорное_: `Settings` и `Config` оба встречаются для одной подсистемы; код последовательно оставляет `Settings` за сохранённым документом.

**Config** (конфигурация):
Разрешённые параметры времени конструирования, производные от Settings (таймауты, ёмкости, имена сервисов), которые компонент получает как plain value.
_Избегать_: settings
_Спорное_: некоторые компоненты называют свои разрешённые параметры `Options`, другие — `Config`; отличие от Settings последовательно, а выбор между двумя именами — нет.

**Options** (опции):
Набор настраиваемых ручек, переданных одному конструктору или командной строке (`PublishSubscribeOptions`, `LoggerOptions`, `CommandLineOptions`).
_Избегать_: settings, config
_Спорное_: пересекается с Config; предпочитайте `Options` для ручек одного компонента и `Config` для разрешённой конфигурации подсистемы.

**Manifest** (манифест):
Декларативный JSON-документ, читаемый один раз при старте, который конфигурирует топологию подсистемы.
_Спорное_: четыре разных документа называют манифестом — Suite manifest (Applications, Function Groups, states), Machine manifest (машины развёртывания и их процессы), SM manifest (state machines, transitions, priorities) и PHM health manifest (наблюдаемые процессы, checkpoints, recovery rules). Всегда уточняйте, какой именно.

**Policy** (политика):
Именованное правило, решающее один поведенческий выбор, либо как compile-time template argument, либо как startup setting.
_Спорное_: metrics policies (`access`, `concurrency`, `reset`) — compile-time свойства инструментов; synchronization policies (`start`, `late event`, `past target`) — runtime-стратегии; а `Policy` у `Infrastructure` — stateless список типов телеметрических компонентов. Три несвязанных употребления слова.

### Время

**Time base** (база времени):
Единый опубликованный авторитет времени платформы: одна каноническая монотонная шкала плюс таблица отображений доменов, публикуемая `nexigen-chronod` в shared memory и потребляемая read-only.
_Избегать_: clock, timer

**Published base** (опубликованная база):
Блок shared memory, который ведёт демон базы времени; «attached» означает, что потребитель привязан к нему, «lost» — что требуемый домен или его heartbeat не удалось соблюсти.
_Избегать_: segment (это storage artifact, а не контракт)

**Heartbeat** (пульс):
Канонический instant последней успешной публикации; потребитель, видящий его старше своего staleness window, считает базу потерянной, хотя память ещё читается.
_Избегать_: keepalive

**Canonical** (канонический):
Хаб-шкала базы времени: monotonic-raw наносекунды, в которые отображается каждый другой домен. Это прилагательное, никогда не тип.
_Избегать_: canonical domain, hub
_Спорное_: wire format называет ту же концепцию «canonical», а ADR 0001 запрещает повторно вводить класс `Canonical` или пользовательское имя домена «canonical».

**Time domain** (домен времени):
Один источник часов, отображённый в каноническую шкалу — например monotonic raw, TAI, realtime или hardware counter.
_Избегать_: clock, source
_Спорное_: «domain» также встречается как generic DDS-style partition и как `timestamp_domain` на transport messages; в chrono это всегда clock domain.

**Domain class** (класс домена):
Тип источника домена времени: `system_clock`, `cycle_counter` или `device_clock`.
_Избегать_: domain type

**Declared domain** (объявленный домен):
Домен времени, который приложение или демон требует по имени; отсутствующий или незахваченный объявленный домен деградирует базу, а не исчезает молча.
_Избегать_: required clock

**Domain coefficient row** (строка коэффициентов домена):
Одна строка опубликованной таблицы: пара якорей, прямые и обратные Q32-масштабы, неопределённость, lock state и границы валидности, отображающие домен в каноническое время и обратно.
_Избегать_: coefficient, map entry

**Lock state** (состояние захвата):
Насколько хорошо домен отслеживает каноническую шкалу: `locked`, `holdover`, `free_run`, `unsupported`.
_Избегать_: sync state

**Holdover** (удержание):
Период, в течение которого ранее синхронизированный домен продолжает экстраполировать после потери опоры, ограниченный holdover window.
_Избегать_: fallback, drift

**Uncertainty** (неопределённость):
Опубликованная граница в наносекундах того, насколько отображение домена может отличаться от канонической шкалы.
_Избегать_: error, accuracy

**Generation** (поколение):
Слово seqlock, которое увеличивается при каждой публикации базы; потребители перечитывают таблицу доменов только когда оно меняется.
_Избегать_: version
_Спорное_: «generation» также нумерует записи time-base history и сегменты recorder; на consumer path читается только seqlock-слово.

**Time-base history** (история базы времени):
Append-only поток опубликованных изменений, который демон пишет рядом с базой и использует после факта для разрешения сохранённых instant в UTC.
_Избегать_: log, journal

**Boot token** (токен загрузки):
64-битный префикс идентичности загрузки ядра, который несёт опубликованная база, чтобы потребитель мог отличить рестарт демона от перезагрузки.
_Избегать_: boot id, boot uuid

**Service flags** (флаги сервиса):
Набор наблюдаемых возможностей платформы, публикуемых с базой (PHC present, TAI available, host counter synchronized, RT scheduling applied, memory locked, declared domains complete и т.п.).
_Избегать_: capabilities

**Sync mode** (режим синхронизации):
Объявленная поза синхронизации базы: `standalone`, `ptp_synced` или `holdover`.
_Избегать_: clock state

**`TimeBaseStatus`**:
Готовность опубликованной базы: `NotReady` (инициализация или калибровка), `Ready` (все объявленные домены захвачены), `Degraded` (каноническая шкала пригодна, но объявленный домен отсутствует, free-running или в holdover), `Lost` (нет пригодной базы).
_Избегать_: base state, clock status

**Time-base client** (клиент базы времени):
Read-only привязка на стороне приложения к опубликованной базе; кэширует требуемые строки, обновляется при смене generation и применяет политику holdover и fail-fast.
_Избегать_: clock reader

**Stamp source / Stamper** (источник штампа / stamper):
Объект на продюсера, который преобразует самые быстрые доступные часы в канонические наносекунды; chrono provider выдаёт фиксированный банк таких объектов.
_Избегать_: clock, sampler

**Provider** (провайдер):
В chrono — авторитет UTC одного процесса: выдаёт stampers, снимает смещение wall-clock относительно канонического времени и доставляет снимки смещения в привязанный sink.
_Спорное_: имя переиспользовано для несвязанных фабрик — `MetricProvider` (владеет meters) и `TracerProvider` (владеет tracers). Никогда не предполагайте, что «provider» означает одну и ту же роль в разных модулях.

**Service snapshot** (сервисный снимок):
В chrono — значение 1 Гц, которое база публикует для потребителей, которым нужны текущий штамп и смещение wall-clock за одно чтение.
_Избегать_: snapshot
_Спорное_: ещё три артефакта называют snapshot; см. Snapshot в разделе telemetry delivery. Service snapshot — единственный, что является частью опубликованной базы.

**Time-sync monitor** (монитор синхронизации времени):
Cold-path детектор нарушений realtime-часов — шагов, обратных скачков и чрезмерного slew относительно монотонной шкалы.
_Избегать_: clock watchdog

**PHC**:
Аппаратные часы PTP, принадлежащие сетевому интерфейсу; когда присутствуют, публикуются как обычный device domain со своей измеренной картой.
_Избегать_: hardware clock

### Транспорт

**Node** (узел):
Членство процесса в транспорте; каждый порт (publisher, subscriber, client, server, event и field endpoint) создаётся через него.
_Избегать_: context, participant

**Service** (сервис):
Именованный транспортный endpoint, идентифицируемый `ServiceName`; producers и consumers встречаются по имени, а не по адресу.
_Избегать_: topic, channel
_Спорное_: «service» также называет демон («log service») и recovery endpoint; на транспорте это всегда `ServiceName`.

**Publisher / Subscriber** (публикатор / подписчик):
Два конца publish-subscribe: publisher пишет payload на месте в shared memory, subscriber читает те же байты как read-only view.
_Избегать_: writer, reader
_Спорное_: «publisher» также используется для time-base publisher, claim publisher и state Field writer.

**Sample** (сэмпл):
Read-only view подписчика на один опубликованный payload; аналог продюсера — mutable loan, который пишется перед отправкой.
_Избегать_: message, packet
_Спорное_: «sample» также называет измерение в chrono и metrics; на транспорте это zero-copy view.

**Field**:
Одноместная ячейка shared memory, всегда хранящая последнее значение, так что поздний читатель видит текущее состояние без request-response round trip.
_Избегать_: variable, blackboard
_Спорное_: recorder datasets и metrics snapshots тоже говорят о fields, имея в виду именованные члены значения.

**Event** (событие):
Транспортное уведомление с id, доставляемое 1:N, которое будит consumers, не неся payload; hardware-originated events могут нести дополнительную метадату.
_Избегать_: signal, notification
_Спорное_: `DiagnosticEvent`, `SecurityEvent` и process-lifecycle events — структурированные отчёты, а не transport events.

**Event channel** (событийный канал):
Сконфигурированный event service, связывающий producer с его consumers, включая notification options; не создаёт transport service, пока не открыт порт.
_Избегать_: topic

**Waitset**:
Мультиплексор, который ждёт несколько transport endpoints сразу и сообщает, какие стали готовы.
_Избегать_: poller, selector

**Request-response** (запрос-ответ):
Коррелированный шаблон вызова: client отправляет request и получает один response от server, с максимум одним pending response на client.
_Избегать_: RPC, command

**Loss** (потеря):
Обнаруженный разрыв в transport sequence, сообщаемый с причиной, а не молча пропускаемый.
_Избегать_: drop

**Publish outcome** (результат публикации):
Более тонкий результат успешной отправки: доставлено с wake-up или принято без него.
_Избегать_: status

**Message contract** (контракт сообщения):
Payload фиксированного размера, trivially copyable и pointer-free — именно это делает zero-copy transport возможным.
_Избегать_: serializable payload, POD

### Доставка телеметрии

**Telemetry** (телеметрия):
Четыре потока, которые эмитит приложение — logs, metrics, traces и recorded transport events — в отличие от diagnostics, которые сообщают состояние самой телеметрической машинерии.
_Избегать_: observability
_Спорное_: «diagnostics» не является синонимом telemetry в этой платформе.

**Record** (запись):
Неизменяемое значение фиксированного размера, произведённое в источнике и удерживаемое в in-process buffer.
_Избегать_: event, entry
_Спорное_: также сохранённая строка recorder; квалификатор (`LogRecord`, `MetricRecord`, `SpanRecord`, recorder record) решает, какая именно.

**Frame** (кадр):
Кросс-процессное зеркало Record: закодированное, pointer-free и отправленное через transport демону или рекордеру.
_Избегать_: packet, message

**Snapshot** (снимок):
Неизменяемая копия состояния, снятая в один instant.
_Спорное_: слово называет четыре несвязанных артефакта — chrono 1 Hz service snapshot внутри published base, metric snapshot (один instrument на harvest), persisted trace artifact trace daemon (также dump) и tree read model хаба. Всегда уточняйте, какой именно.

**Sink** (сток):
Потребитель готовой телеметрии, живущий в эмитящем процессе (console, file, transport или composite из нескольких).
_Избегать_: target, output
_Спорное_: четыре несвязанных семейства контрактов используют имя — log sinks, metric sinks, trace sinks и chrono offset sink; они разделяют форму жизненного цикла, а не payload.

**Binding** (привязка):
Не владеющая, type-erased ссылка, соединяющая producer с sink или logger и не должна переживать его.
_Избегать_: hook, callback
_Спорное_: SM также привязывает Function Groups к applications, а time base публикует binding coefficients; это разные отношения.

**Session** (сессия):
Один ограниченный период работы, владеющий ресурсом на своё время.
_Спорное_: recorder session — артефакт (один набор файлов с коррелированным timeline); trace-daemon session — время жизни процесса демона. Никогда не предполагайте, какой именно.

**Worker** (воркер):
Фоновый поток, которым владеет компонент, чтобы дрейнть или публиковать по каденции.
_Спорное_: generic `WorkerThread`, chrono `ChronoWorker`, logging worker, metrics harvester и tracing consumer — все «workers» с разными обязанностями.

**Harvester** (харвестер):
Метрик-сайд воркер, который периодически превращает состояние инструментов в batch и передаёт его sinks.
_Избегать_: collector, aggregator

**Quiescence** (квиесценция):
Контракт логирования: логгер перестаёт принимать записи, дрейнит уже поставленное продюсерами и только затем сообщает о завершении shutdown.
_Избегать_: flush, drain (оба — шаги внутри quiescence)

**Dropped stats** (статистика потерь):
Подсчитанные и отчётные потери телеметрического потока (полные rings, отброшенные batches, подавленные строки), чтобы разрыв никогда не был молчаливым.
_Избегать_: drops, errors

**Overflow policy** (политика переполнения):
Что жертвует ограниченный logging buffer при заполнении — входящую запись или самую старую непрочитанную.
_Избегать_: drop policy
_Спорное_: transport overflow по дизайну non-blocking и сообщает loss вместо выбора жертвы; именованный `OverflowPolicy` принадлежит logging.

### Логирование

**Logger** (логгер):
Application-facing sink `LogRecord`: console, file, transport (iceoryx2), composite или no-op.
_Избегать_: log, output

**`LogRecord`**:
Одно структурированное log event в источнике: level, source location, format string и его типизированные аргументы, хранимые без аллокации.
_Избегать_: log line, message

**Log frame** (кадр лога):
Фиксированный wire envelope, несущий `LogRecord` демону логов; демон рендерит финальную строку.
_Избегать_: packet

**Log level** (уровень логирования):
Словарь важности, общий для records, filters и settings.
_Избегать_: verbosity

**Rotation schedule** (расписание ротации):
Политика, которая закрывает log file и открывает следующий: по истёкшему периоду, по размеру или обоим.
_Избегать_: rollover

**Spam protection** (защита от спама):
Подавление повторяющихся идентичных записей на вызывающего, с token budget и подсчитанной сводкой вместо потопа.
_Избегать_: rate limit, dedup

**Log daemon** (демон логов):
`nexigen-logd`, центральный процесс, который подписывается на log frames, рендерит финальную JSON-строку и владеет ротацией и disk I/O.
_Избегать_: log server

### Метрики

**Meter** (измеритель):
Handle на компонент, который создаёт instruments и несёт producer identity, используемую в каждой метрике, которую он сообщает.
_Избегать_: registry, factory

**Instrument** (инструмент):
Один измеримый объект, принадлежащий meter — counter, gauge, state gauge, explicit или exponential histogram, descriptive statistics.
_Избегать_: metric
_Спорное_: metric — это сообщаемая identity плюс значение; instrument — то, в что пишут.

**Metric identity** (идентичность метрики):
Стабильный ключ сообщаемой метрики: владеющий meter (application), name и kind.
_Избегать_: key, label set

**Metric snapshot** (снимок метрики):
Значение одного instrument на момент harvest, с kind-specific payload и политиками, с которыми instrument был создан.
_Избегать_: sample, reading

**Metric batch** (батч метрик):
Набор metric snapshots, произведённых одним harvest, переданный sinks как единый bounded view.
_Избегать_: buffer, chunk

**Access / concurrency / reset policy** (политики доступа / конкурентности / сброса):
Compile-time свойства instrument: могут ли читатели его наблюдать, пишут ли в него несколько потоков и сбрасывает ли чтение накопленное окно.
_Избегать_: mode, flags

**Metrics daemon** (демон метрик):
`nexigen-metricsd`, центральный процесс, который агрегирует последнее значение каждой метрики и экспортирует его.
_Избегать_: aggregator

### Трассировка

**Trace** (трасса):
Одно причинно связанное дерево spans, идентифицируемое trace id, который путешествует с исходящими сообщениями.
_Избегать_: transaction

**Span** (спан):
Одна измеряемая операция внутри trace, с kind, status, attributes и parent; записывается один раз при старте и завершается один раз.
_Избегать_: segment, operation

**Scoped span** (scoped-спан):
RAII-гард, который владеет активным span и завершает его ровно один раз при выходе из scope, если не завершён явно.
_Избегать_: span guard

**Trace header** (заголовок трассы):
Фиксированное значение, прикреплённое к исходящему сообщению, которое связывает его с текущей trace и активным span и несёт publisher sequence.
_Избегать_: context header

**Sampling decision** (решение о сэмплировании):
Выбор на span — отбросить его или записать и сэмплировать, сделанный один раз при старте span.
_Избегать_: filter
_Спорное_: «sampling» также называет clock sampling в chrono и metric harvesting в metrics; только tracing решает sampling decision.

**Span frame** (кадр спана):
Кросс-процессное зеркало span record: те же данные с хэшами вместо source pointers, плюс publisher identity и format version.
_Избегать_: span record (это in-process значение)

**Trace daemon** (демон трассировки):
`nexigen-traced`, центральный flight recorder, который сливает span traffic в bounded in-memory ring и сохраняет snapshot только когда health layer сообщает об инциденте.
_Избегать_: collector

**Dump** (дамп):
Сохранённый trace artifact, записанный при срабатывании демона.
_Избегать_: snapshot
_Спорное_: тот же артефакт называют «snapshot» в спецификации trace-daemon и «dump» в конфигурации и failure counters демона; это один и тот же файл.

### Управление здоровьем (PHM)

**PHM**:
Platform Health Management — `nexigen-phmd`, демон, который супервизирует liveness процессов и применяет recovery.
_Избегать_: health monitor, watchdog

**Supervised entity** (наблюдаемая сущность):
Application-side client, через который приложение эмитит checkpoints, health faults и свою identity claim.
_Избегать_: client, agent

**Checkpoint** (контрольная точка):
Сигнал, который приложение эмитит, чтобы доказать конкретное свойство прогресса или порядка; это активация, а не сохранённое состояние.
_Избегать_: marker, save point

**Alive / logical / deadline supervision** (супервизия по живости / логике / дедлайну):
Три контракта liveness: достаточно checkpoints за окно, легальный порядок checkpoints включая fork/join branches, и завершение в дедлайн.
_Избегать_: watchdog, heartbeat

**Supervision status** (статус супервизии):
Агрегированный вердикт одного наблюдаемого процесса после цикла: `Ok`, `Failed` (в пределах допуска), `Expired` (за пределами) или `Exhausted` (попытки recovery исчерпаны).
_Избегать_: health

**Health state** (состояние здоровья):
Уровень, который приложение публикует о себе: `Ok`, `Warning`, `Error`, `Critical`.
_Избегать_: supervision status

**Health channel** (канал здоровья):
Event endpoint, на котором приложение публикует переход health state.
_Избегать_: alert channel

**Claim** (заявка идентичности):
Идентичность, которую приложение публикует о себе (name, process id, user id), чтобы демон мог проверить её по манифесту без handshake.
_Избегать_: registration, handshake

**Fault kind** (вид отказа):
Метка записанного отказа, называющая, какая проверка не прошла, чтобы оператор мог отделить aliveness gap от determinism violation.
_Избегать_: error type

**Security event** (событие безопасности):
Отчёт демона о том, что claim был отвергнут, несущий наблюдённое и заявленное.
_Избегать_: alarm

**Watchdog** (сторожевой таймер):
Аппаратный таймер, который демон кормит, пока все наблюдаемые процессы здоровы; кормление прекращается при expiry или по требованию, так что reset устройства — последнее безопасное действие.
_Избегать_: health check

**Babbling idiot** (болтун):
Процесс, который продолжает общаться после потери права на это; супервизия должна обнаружить это, а не интерпретировать как здоровье.
_Избегать_: spammer

**Recovery** (восстановление):
Эскалация после отказа супервизии: перезапустить процесс, переключить состояние Function Group или вызвать watchdog reset.
_Избегать_: remediation
_Спорное_: SM также имеет error recovery внутри своих state machines, а EM выставляет recovery handler для PHM; это принимающие шаги той же эскалации, а не отдельные концепции.

**Recovery action / reason / outcome** (действие / причина / результат восстановления):
Запрошенная эскалация (`restart process`, `switch state`, `trigger watchdog reset`), почему она запрошена и принял ли адресат, ещё работает или отверг.
_Избегать_: result, reply

**SafeState**:
Зарезервированное аварийное состояние, в которое Function Group переключается, когда локальный restart не может восстановить здоровье.
_Избегать_: emergency mode, fallback

**EM lifecycle** (жизненный цикл EM):
Представление PHM о том, работает ли Execution Manager, чтобы супервизию можно было приостановить на время намеренной остановки, а не сообщать об отказе.
_Избегать_: EM state

### Управление состояниями (SM)

**State Manager (SM)** (менеджер состояний, SM):
`nexigen-sm`, процесс, владеющий графами состояний Function Groups, арбитрирующий SetState requests и ведущий transitions через EM.
_Спорное_: аббревиатура пересекается с «state machine» — SM это процесс, LSM — машина состояний внутри него.

**Arbitration** (арбитраж):
Правило, разрешающее конфликтующие SetState requests по requester priority; проигравший request отвергается, а не ставится в очередь.
_Избегать_: conflict resolution

**Requester priority** (приоритет запрашивающего):
Сконфигурированный ранг requester, решающий арбитраж; PHM fault events выше hardware power signals, а diagnostics выше user applications.
_Избегать_: authority level

**Transition** (переход):
Исполнение одного разрешённого перемещения между двумя состояниями Function Group, включая его упорядоченные actions.
_Избегать_: switch, change

**`TransitionResult`**:
Результат попытки перехода: success, rejected by priority, rejected by update in progress, invalid graph edge, timeout или failed action.
_Избегать_: status

**Transition watchdog** (watchdog перехода):
Таймаут, который прерывает transition, не завершившийся в свой бюджет, и сообщает timeout вместо зависания.
_Избегать_: timer

**Trigger (TriggerIn)** (триггер, TriggerIn):
Внешний запрос начать transition с requester identity и priority, отличный от SetState command path.
_Избегать_: command
_Спорное_: tracing также имеет recovery trigger, который дампит spans; только SM-триггер — это TriggerIn request.

**LSM (Lightweight State Machine)** (лёгкая машина состояний):
Декларативный однопоточный интерпретатор таблицы переходов с упорядоченными actions и таблицей error recovery, по одному на Function Group.
_Избегать_: state machine, SM

**LSM action** (действие LSM):
Один side effect в transition: запросить состояние Function Group через EM или опубликовать состояние на state channel.
_Избегать_: step

**Error recovery (LSM)** (восстановление после ошибки, LSM):
Правило на состояние, называющее fallback state и actions, которые выполнить при отказе action; локально для одной группы.
_Избегать_: recovery (зарезервировано за PHM-эскалацией)

**State client** (клиент состояний):
EM-side endpoint, который отвечает на SetState requests, выполняет запрошенный transition и сообщает результат обратно SM.
_Избегать_: state server

**UCM**:
Путь координации single-update: держит один активный update за раз, захватывает предыдущее состояние группы и ведёт `start`, `complete` и `cancel` через EM.
_Избегать_: update manager

**Update** (обновление):
Одно координированное изменение software или state Function Group, идентифицируемое update id и либо активное, либо отсутствующее.
_Избегать_: upgrade, deployment

**State publication** (публикация состояния):
Field на группу, на котором SM публикует текущее состояние Function Group, чтобы поздний читатель видел последнее состояние без round trip.
_Избегать_: state broadcast

**Group state payload** (payload состояния группы):
Фиксированное значение, хранимое в state Field группы: имя текущего состояния группы.
_Избегать_: state message

**Root lifecycle** (жизненный цикл RootFG):
Собственная последовательность startup и shutdown SM относительно RootFG, включая readiness report и initial-transition query.
_Избегать_: bootstrap

### Запись и наблюдаемость

**Recorder** (рекордер):
`nexigen-recd`, процесс, который записывает log, metric, trace и event streams сьюта в одну сессию.
_Избегать_: logger, collector

**Recorder session** (сессия рекордера):
Один recording artifact: набор segments, manifest и встроенные channel/schema declarations, закрываемый чисто или нет.
_Избегать_: file, capture

**Record shard** (шард записей):
Per-producer ring of framed records, который producer передаёт session writer.
_Избегать_: buffer

**Channel declaration / schema declaration** (объявление канала / схемы):
Метаданные сессии, называющие записанный stream и описывающие его payload schema, чтобы reader мог декодировать сессию без SDK продюсера.
_Избегать_: stream, schema
_Спорное_: transport services и events тоже называют channels; recorder channel — сохранённый stream, а не live endpoint.

**Time-base resolver** (резолвер базы времени):
Export-side lookup, превращающий сохранённый hub instant в UTC из записанной time-base history.
_Избегать_: converter

**Export** (экспорт):
`nexigen-export`, on-demand преобразование записанной сессии в запрошенные выходы; без daemon и follow mode.
_Избегать_: dump, replay

**Hub** (хаб):
`nexigen-hub`, локальная read-only обсерватория, которая обнаруживает run roots и отдаёт их дерево, метаданные и live streams по HTTP.
_Избегать_: dashboard, server

**Discovery** (обнаружение):
Filesystem scan хаба, который перестраивает дерево run roots, suites, instances, attempts и streams и держит его живым.
_Избегать_: indexer, scanner

**Run root** (корень запуска):
Директория, выданная suite execution и которую хаб наблюдает; содержит suite metadata и instance directories.
_Избегать_: run, job, workspace
_Спорное_: «run» в хабе означает этот artifact root, suite-глоссарий избегает «run» для Suite, а application identity использует «run id»; три разные вещи.

**Hub suite / hub instance** (suite / instance в хабе):
Read-only представление Suite или Instance в хабе, производное от тех же метаданных, которые пишет Execution Manager.
_Избегать_: mirror, copy

**Stream** (поток):
Один append-only файл, который хаб tail'ит (application stdout/stderr, metrics, traces, chrono, execution-manager log), экспонируемый со стабильным id и sequence numbers.
_Избегать_: log, feed

**Stream kind** (вид потока):
Классификация потока: `app`, `metrics`, `traces`, `chrono`, `stdout`, `stderr` или `execution_manager`.
_Избегать_: type

**Gap marker** (маркер разрыва):
Явный отчёт хаба о том, что records потеряны (слишком длинная строка отброшена или reader соскочил с bounded ring), с числом пропущенных.
_Избегать_: error, skip

**Hub tree** (дерево хаба):
Текущая read model всего, что видит Discovery, перестраиваемая при каждой reconciliation и отдаваемая одним API response.
_Избегать_: index

### Встраивание наблюдаемости

**Observability embedding** (встраивание наблюдаемости):
Паттерн, при котором runtime/service-слой владеет логгером и метром, а data-plane модули остаются свободными от телеметрии и возвращают структурированные результаты.
_Избегать_: instrumentation, telemetry layer

**Observability scenario (`OBS-*`)** (сценарий наблюдаемости):
Именованный переиспользуемый паттерн встраивания телеметрии из `docs/observability-embedding.md`; ревью ссылается на имя вместо пересказа паттерна.
_Избегать_: recipe, profile

**Error episode** (эпизод ошибки):
Открытый интервал для ключа ошибки `(domain, node, code)`: от первого появления до `recovery_hold_ms` тишины, с одной записью первого отказа, ограниченными повторами и одной записью восстановления с числом подавленных.
_Избегать_: error storm, error window

**Windowed summary** (оконная сводка):
Периодическая cold-path запись на домен, агрегирующая ошибки, подавленные occurrence'ы и восстановления с прошлой сводки; выводится только после активности и затем сбрасывает окно.
_Избегать_: error rollup, periodic log

**`OBS-BOOT`**:
Bootstrap-диагностика до появления логгера: ограниченные строки в `stderr` плюс структурированная ошибка.
_Избегать_: early logging

**`OBS-COLD`**:
Подробный журнал жизненного цикла, конфигурации и загрузки EDS/DCF; диагностика парсера ограничена `max_cold_diagnostics` с трейлером числа пропущенных.
_Избегать_: verbose logging

**`OBS-EDGE`**:
Одна запись на значимый переход: смена состояния, первая ошибка эпизода, смена кода ошибки или восстановление. Никогда одна запись на каждое наблюдение.
_Избегать_: event logging

**`OBS-HOT-ERR`**:
Ограниченное hot-path событие ошибки: сначала пишется метрика, затем порог логгера и фиксированный error gate решают, будет ли запись.
_Избегать_: error log, hot logging

**`OBS-HOT-MET`**:
Metrics-first hot path: кэшированные указатели `MultiThreaded` инструментов обновляются покадрово без поиска имён, аллокаций, форматирования и блокировок.
_Избегать_: counters, stats

**`OBS-SUM`**:
Оконная cold-path сводка: одна периодическая запись на активный домен/скоуп, агрегирующая подавленное на hot path.
_Избегать_: rollup, report

**`OBS-SNAP`**:
Pull-based диагностический снимок: on-demand cold-path значение или harvest-вызов, фиксирующий текущее состояние для диагностического endpoint или теста.
_Избегать_: dump, probe

**`OBS-SPAN`**:
Низкочастотный span транзакции: один `ScopedSpan` вокруг транзакции (загрузка EDS, SDO-транзакция, сэмплированный PDO-цикл), никогда не на каждый кадр.
_Избегать_: per-frame trace

### Диагностика

**Stopwatch** (секундомер):
Cold-path handle измерения, который записывает именованные laps и выводит count, min/max/mean и текущее run state.
_Избегать_: timer, profiler

**Lap** (круг):
Один именованный интервал, записанный stopwatch, с start, duration и description.
_Избегать_: split, mark

**Scoped stopwatch** (scoped-секундомер):
RAII-форма stopwatch, измеряющая ровно один scope.
_Избегать_: timer guard

**Diagnostic event** (диагностическое событие):
Структурированный pointer-free отчёт телеметрической подсистемы о своём жизненном цикле или runtime state (initialized, degraded, conflict), доставляемый через diagnostic binding.
_Избегать_: log record, telemetry event

**Diagnostic binding** (диагностическая привязка):
Type-erased logger reference, которую provider связывает один раз в configuration mode, чтобы получать diagnostic events.
_Избегать_: callback, hook

**Diagnostic source / kind** (источник / вид диагностики):
Какая подсистема произвела diagnostic event и какой класс события это.
_Избегать_: origin, category

### Синхронизация (synchro)

**Sync generator / consumer** (генератор / потребитель синхронизации):
Два конца phase-aligned periodic activation: generator публикует одно event на grid cycle без накопленного drift, consumer валидирует и диспетчеризует его.
_Избегать_: publisher, subscriber
_Спорное_: «sync» здесь означает синхронизацию расписания, а не mutex или atomic.

**Schedule (grid)** (расписание, сетка):
Детерминированная раскладка циклов — period, phase и alignment — определяющая, когда каждая activation должна произойти.
_Избегать_: timer, clock

**Start mode** (режим старта):
Как выбирается первый цикл: `NextCycle` ждёт следующей grid boundary, `AtTarget` соблюдает явную цель с учётом past-target policy.
_Избегать_: start policy

**Past-target policy** (политика прошлой цели):
Что делать, когда явная start target лежит в прошлом: отвергнуть старт или молча realign на следующий цикл.
_Избегать_: catch-up

**Late-event policy** (политика опоздавших событий):
Что consumer делает с event, пришедшим после дедлайна: отбросить и посчитать или доставить с флагом late.
_Избегать_: drop policy

**`SyncState`**:
Жизненный цикл endpoint: `Stopped`, `WaitingForStart`, `Running`, `Faulted`.
_Избегать_: status

### Словарь ошибок и статусов

**Result** (результат):
Fallible return value платформы: либо значение, либо `ErrorCode`, всегда проверяется ветвлением, никогда вызовом `value()`.
_Избегать_: exception, error

**Status** (статус):
`Result`, не несущий значения — успех или `ErrorCode`.
_Избегать_: state
_Спорное_: «status» также означает сообщаемое состояние (`TimeBaseStatus`, `SupervisionStatus`, suite status, application status); неквалифицированный `core::Status` — всегда результат ошибки.

**Error code** (код ошибки):
Фиксированный словарь причин отказа, общий для всех модулей; значения append-only для ABI-совместимости.
_Избегать_: error kind, error type
