# nexigen-cpp — анализ кода

Дата: 2026-11-03 · Ветка: `learning-enhancement` · HEAD: `f00e43f`
Обновлено: 2026-11-04 (метрики спецификаций; находки — по состоянию на `f00e43f`)
Uplink: [project-analysis.md](project-analysis.md) (2026-11-01). Связан с записями
в `TODO.md` (раздел «Анализ кода»).

Методика: параллельное исследование модулей (core/chrono/app/diagnostics,
logging/metrics/tracing, runner/hub/build) + верификация критических находок по
первоисточникам + прогон clang-tidy (профиль `.clang-tidy`:
bugprone/performance/portability/clang-analyzer, WarningsAsErrors) на
`rotation.cpp`, `provider.cpp`, `chrono_provider.cpp`, `runner.cpp`,
`http_server.cpp`. Анализ read-only: код не изменён.

---

## 1. Общие сведения

| Показатель | Значение |
|---|---|
| Язык / стандарт | C++23, `-fno-exceptions` + `-fno-rtti`, GCC и Clang под `-Werror` |
| Продуктивный код | ~28 500 строк (135 файлов: 99 в `include/`, 36 в `src/`) |
| Тесты | ~19 900 строк, 11 unit-бинарников + fuzz + CLI-smoke; тест/код ≈ 70% |
| Покрытие | 95.2% строк (гейты: глобальный 95%, runner/hub 90%), TSC-латенс-ворота ≤ 20 нс в Release |
| Спецификации | OpenSpec: 68 capabilities, 489 требований, 1 344 сценария (обновлено 2026-11-04) |
| TODO/FIXME | 0 |
| clang-tidy | На выборке — **0 предупреждений** |

## 2. Архитектура

Чёткая слоистая система без обратных зависимостей:

```
core (Result/Status, SPSC ring, ShardPool, vocabularies)
  ← chrono · diagnostics
  ← logging · metrics · tracing   (используют core::ShardPool как общий движок)
  ← app (Infrastructure: signalfd-жизненный цикл, настройки, CLI)
  ← runner (nexigen-run: изолированные процессы, супервизоры)
  ← hub (nexigen-hub: read-only обсерватория, epoll HTTP/SSE)
```

Связь runner↔hub — через файловую систему (`resource.json`, `exit.json`,
4 JSONL-потока, атомарная запись temp+rename), что отделяет поставку данных от
чтения. HTTP-парсер вынесен в header-only `http_parse.hpp` (160 строк, только
stdlib) — для фаззинга; libFuzzer + corpus гоняются в CI.

## 3. Сильные стороны

- **Модель ошибок — образцовая**: единый `Result`/`Status` (`core/result.hpp`),
  `[[nodiscard]]` повсеместно, ни одного `throw`, ни одного непроверенного
  `value()`. Паттерн «constexpr to_string/from_string + ADL-JSON +
  `std::formatter`» выдержан на десятках типов.
- **Ядро lock-free корректно**: `core/spsc_ring_buffer.hpp` — release/acquire
  для публикации видимости слота, кэш-индексы соседа, монотонные `size_t`
  (все `Capacity` слотов полезны), `static_assert` на lock-free и 64-байтовые
  ячейки против false sharing.
- **Дисциплина владения**: jthread/stop_token, open-before-close при ротации
  файлов, `coverage_flush()` на путях `_exit`/`execvp`, handshake на
  `O_CLOEXEC`-pipe.
- **Сборка**: раннее отсечение несовместимостей (ASan+TSan → `FATAL_ERROR`),
  pin зависимостей с SHA256, packaging consumer-тест.

## 4. Проблемы (по убыванию тяжести)

### 4.1. Критические

| # | Проблема | Место |
|---|---|---|
| 1 | **flush() нарушает инвариант SPSC.** `flush_drop_summaries()` вызывается из любого потока приложения и делает `try_push` в **чужие** шард-кольца (`pool_.ring(slot).try_push`, стр. 225), минуя правило «слот трогает только владелец». Одновременно с `emit()` владельца — два продюсера на один ring: гонка данных и порча содержимого. Триггер: любой pending-дроп (rate-limit / buffer-full / spam-throttle). | `producer.hpp:243–256, 225`; вызов из `async_logger.hpp:199` |
| 2 | **Handshake-до-exec при неудачном запуске.** Потомок пишет `setup_status = 0` в handshake-pipe **до** `execvp`; при неудаче exec → `child_exit(127)` уже после рукопожатия. Родитель публикует `resource.json` («started»), а stdout уже перенаправлен в файл — пользователь на консоли не видит ничего. | `runner.cpp:605–616` |
| 3 | **Path-escape в hub discovery.** Абсолютный `artifacts_path` из `suite.json` (записываемой раннером каталогом, т.е. потенциально компрометированным приложением) принимается как база путей чтения **без проверки принадлежности run-root**. | `discovery.cpp:388–397, 474–497` |

### 4.2. Высокие / средние

| # | Проблема | Место |
|---|---|---|
| 4 | `bump()` — не-атомарный RMW `store(load+1)`; параллельный сброс счётчиков из flush-потока — потерянные апдейты | `producer.hpp:235–237, 227–229` |
| 5 | **PID-recycling**: `stop`/`status` доверяют pid из `resource.json`, проверяя только живость (`kill(pid,0)`); `start_time_mono_ns` хранится, но в identity-проверке не участвует → SIGKILL чужой группе после рециклинга PID | `runner.cpp:854–925, 946` |
| 6 | **ECHILD → устаревший статус**: в SIGKILL-ветке `waitpid` при ECHILD выходит как «успех» со старым status → `exit.json` может записать `exit_code=0` чужеродного происхождения | `runner.cpp:287–293, 260–268` |
| 7 | **Гонка в dead-code `SpscShardRing`** (`OverwriteOldest`): продюсер двигает `tail_`, который обычно обновляет консьюмер; seq-счётчики из design/OpenSpec не реализованы. Код не подключён к проду (заглушка «wiring in the follow-up change») — «дубль» ради logging::detail | `spsc_shard_ring.hpp:51–81`; `producer.hpp:273` |
| 8 | `MetricProvider::get_meter` вне `harvest_mutex_` при том, что publication под ним — check-then-act по арене метров при параллельных `get_meter`/сбросе | `provider.cpp:174–223` |
| 9 | `SpamProtection::set_config` — не-атомарная запись из flush-потока (анонс «wait-free reconfiguration»): data race с продюсерами | `spam_protection.hpp:41`; `producer.hpp:306–309` |
| 10 | **TLS-кэш 4 пулов → тотальный дроп**: при >4 регистраций пулов на поток каждый `emit()` сжигает глобальный `next_slot`; после исчерпания **все записи потока дропаются навсегда** (`return -1`); комментарий «keeps producing» вводит в заблуждение | `shard_pool.hpp:140–181` |
| 11 | Трассировка усекает номер слота int32→**int8** без `static_assert(MaxThreads<=127)` | `tracer_provider.hpp:271–273` |
| 12 | Двухфазный `ChronoProvider::start(Logger&)` — нет guard на повторный вызов: воркер пишет/читает `log_binding_` на фоне основного потока | `chrono_provider.cpp` |
| 13 | IPv6: bind только `AF_INET` (`::1` невозможен); CLI-парсер по `rfind(':')` усекает `::1` до `::` — предупреждение о non-loopback не работает | `tools/nexigen-hub/main.cpp:115–124` |
| 14 | Transient zombie при неуспешном старте (один `WNOHANG`-waitpid); env-валидация асимметрична: CLI-путь не проверяет NUL в *значениях*, suite-манифест — проверяет | `runner.cpp:806–810, 103–109`; `app_suite.cpp:1427–1431` |

### 4.3. Низкие

| # | Проблема | Место |
|---|---|---|
| 15 | **LTO — no-op**: `INTERPROCEDURAL_OPTIMIZATION` ставится на INTERFACE-цель `nexigen_options`, которая ничего не компилирует, при выводе «🚀 LTO enabled» и обещании в README | `Optimizations.cmake:35–42` |
| 16 | **«0x» + десятичное число** в конфиг-строках (`core_mask=0x10` для маски 0xA) — в трёх модулях; тест не ловит | `chrono_provider.cpp:85–86`; `snapshot_format.cpp:462`; `record_format.cpp:470` |
| 17 | **Тихий дроп батчей** при переполнении JSON-скратча (16 КБ метрики / 8 КБ трейс) без счётчика потерь | `json_file_metric_sink.hpp:192–195`; `sink_format.cpp:207–211` |
| 18 | **Дрейф Options↔Engine**: поля `LoggerOptions` (`max_producer_threads`, `spsc_ring_buffer_capacity`, …) не подключены к ConsoleLogger/FileLogger; dead-поля конфига spam-протекции; `to_string` не печатает `flush_cpu_affinity` | `console_logger.hpp:172–173`; `file_logger.hpp:335–340`; `logger_options_format.cpp:119–130` |
| 19 | `Gauge<MultiThreaded>::set`/`StateGauge` — check-then-act (seqlock-retry вместо заявленного wait-free); `StateGauge::read()` возвращает string_view на thread_local-буфер | `gauge.hpp:74–79`; `state_gauge.hpp` |
| 20 | Configure ломается при `tests=ON + tools=OFF` (генераторные выражения на цели tools) | `tests/CMakeLists.txt:105–108, 116–119` |
| 21 | Асимметрия `CompositeLogger::flush` (последняя ошибка) vs `CompositeTraceSink::flush` (первая) при одинаковых API | `composite_logger.hpp:187–198`; `composite_trace_sink.hpp:154–158` |
| 22 | `SignalWatcher::start` без guard на повторный вызов (fd leak; из API недостижимо); повторяющаяся `fail`-идиома ×4 в `Infrastructure::initialize` | `infrastructure.hpp:97–118, 232–274` |
| 23 | SIGPIPE на handshake/report-pipe — единственный IPC-канал без `MSG_NOSIGNAL` (узкое окно гонки) | `runner.cpp:605–608, 621–623` |

## 5. Код-смеллы

- **Тройная реализация JSON-писателя** (logging/metrics/tracing —
  FixedWriter/append/escape почти идентичны) и **×3** `read_settings_file` /
  `parse_u64` / `add_diagnostic`; **×4** affinity-хелперы (осознанный trade-off,
  но 4 места рассинхронизации). Единый `<nexigen/core/fixed_writer.hpp>` закрыл
  бы и «0x»-класс багов.
- **Мандатный шаблон** (member to_string + свободный + `std::formatter`) на
  десятках типов — кандидат на CRTP-базу.
- **IIFE-magic числа** валидны, но разбросаны: спины 64 / 200 µs, escape-лимит
  50 000, скратчи 4096/8192/16384, срез шарда 16, 129-байтовый `safe_id`,
  реанкоринг 2^42, сон калибровки 2 мс.
- Спецификация расходится с кодом: `openspec/specs/logging/spsc-shard-ring/spec.md`
  обещает seq-счётчики, которых нет; в комментариях «invariants live in exactly
  one place» при фактическом дубле SPSC-машинерии.

## 6. Тесты: пробелы, коррелирующие с багами

- нет многопоточного `flush()` (нашёл бы #1) и стресс-теста
  «деструкция-во-время-работы» (shutdown-drain);
- нет concurrency-теста `get_meter` (#8);
- нет тестов PID-recycling (#5) и path-containment discovery (#3);
- `SpscShardRing/Pool` покрыты поверхностно — гонка tail_ (#7) и
  нереализованный рецикл не вскрыты;
- TLS >4 пулов на поток (#10) — не покрыто;
- фаззинг — только `hub::http_parse`; JSON-парсеры настроек не фаззятся.

## 7. Гигиена репозитория

`build-asan/`, `build-tsan/`, `build-release/`, `build-coverage/` — незатреканы и
**не добавлены в `.gitignore`** (в отличие от `build/`, `html/`,
`cmake/__pycache__`); строка `build/` не ловит суффиксные имена.

## 8. Общая оценка и приоритетные рекомендации

Зрелая, дисциплинированная база с редким уровнем тестов и проверяемых
контрактов; критические риски сосредоточены в трёх точках: flush/logging-движок,
порядок handshake/exec у runner'а, граница доверия hub↔runner.

1. **`flush_drop_summaries`** (#1, #4, #9): убрать push из flush-потока —
   хранить отложенные дропы в состояниях шардов и отдавать их владельцам/воркеру
   или синхронизировать через воркер; заодно atomic RMW в `bump()` и атомарный
   конфиг spam-протекции.
2. **Runner handshake** (#2): писать `setup_status = 0` **после** успешного
   `execvp` (или докладывать ошибку через pipe до сброса stdout в файл).
3. **Hub containment** (#3): realpath-проверка, что `artifacts_path` и
   производные пути лежат под run-root.
4. **`SpscShardRing/Pool`** (#7): довести до спецификации (seq-счётчики, рецикл)
   или удалить — «дубль + заглушка» хуже отсутствия.
5. **Identity супервизора** (#5, #6, #14): проверка `start_time_mono_ns` перед
   `kill`, корректная обработка ECHILD, повторный waitpid против zombie.
6. **Согласованность метрик/трассировки** (#8, #10, #11, #12): мьютекс в
   `get_meter`, семантика при исчерпании TLS-слотов, `static_assert` на
   MaxThreads, guard повторного `start()`.
7. **Гигиена** (#15–#23 + см. §7): LTO на реальные цели, hex-вывод `.gitignore`,
   счётчики дропов, сведение JSON-писателей/загрузчиков настроек.

---

## Приложение A. Оценка по модулям

| Модуль | Архитектура | Ключевые точки |
|---|---|---|
| core | ★★★★★ Result/Status упитан, SPSC с кэш-индексами | pop-рекурсия, tls[4] молчаливый клэмп, нет MT claim-теста |
| chrono | ★★★★☆ TSC-калибровка, reanchor, синки | hex-метки, двухфазный старт, stale reanchor |
| app | ★★★★☆ CAS-владение сигналами, кооперативный watcher | SignalWatcher guard, fail-идиома ×4 |
| diagnostics | ★★★★★ RAII-точный, без исключений | — |
| logging | ★★★★☆ шардированный producer, батчи, антиспам | flush-гонка #1, dead-code #7, дрейф Options #18 |
| metrics | ★★★★☆ слаб/арена, seqlock, Welford | get_meter вне мьютекса #8, дроп батчей #17 |
| tracing | ★★★★☆ ShardPool + span-стек | int8 усечение #11, zero-copy атрибуты (контракт-ловушка) |
| runner | ★★★★☆ setsid-супервизоры, политики рестартов | handshake #2, ECHILD #6, PID-recycling #5 |
| hub | ★★★★☆ epoll/SSE, чистый парсер, фаззинг | path-escape #3, IPv6 #13 |

## Приложение B. Инструменты проверки, запущенные при анализе

- clang-tidy (5 TU, профиль `.clang-tidy`) — 0 предупреждений;
- `git ls-files` / `wc -l` — количественные метрики;
- точечная верификация находок #1–#3, #15 по первоисточникам.
