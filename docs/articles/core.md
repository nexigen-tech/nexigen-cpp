# `core`: фундамент платформы

`nexigen::core` — слой L0, на котором стоят все остальные модули. Здесь нет доменных понятий вроде «приложение», «сессия» или «Function Group»; есть только примитивы, которые нужны каждому: модель ошибок, сериализация, словари перечислений, SPSC-очереди, управление потоками, общая инфраструктура sinks и настроек, integrity-примитивы и структурные концепты.

Модуль собирается как `INTERFACE`-цель `nexigen_core`: он состоит из шаблонов и inline-кода, и линковать нечего. Это же объясняет его границу: `core` не имеет права зависеть от `chrono`, `com` или телеметрии, иначе направление зависимостей платформы сломается.

## Ответственность

- **Ошибки без исключений.** `Result<T, E = ErrorCode>` и `Status` построены над `std::expected`. Публичные fallible-функции возвращают их, а вызывающий ветвится по `has_value()`/`.error()`. Вызов `.value()` запрещён: при `-fno-exceptions` он приводит к `std::abort`.
- **Сериализация как мандат.** `serialization_mandate`, `EnumTraits` и `FixedWriter` задают единый контракт `to_string`/`to_json`/`from_json`/`std::formatter`. Тела сериализации живут в `src/*/*_format.cpp`, чтобы не раздувать TU потребителей.
- **Конкурентность.** `SpscRingBuffer<T, Capacity>` — wait-free очередь с одним писателем и одним читателем; `ShardPool` — пул per-thread shard'ов для многопроизводительного режима. Индексы и ячейки выровнены по cache line.
- **Потоки.** `CpuSet` адресует до 1024 CPU; `ThreadSpec` задаёт имя, политику (`Other`/`FIFO`/`RR`), приоритет и режим применения (`Required`/best-effort); `ThreadApplyReport` фиксирует, что именно применилось. `apply_thread_spec_self` применяет спецификацию к текущему потоку; `WorkerThread`/`WorkerTuning` задают поведение фоновых потребителей.
- **Sinks и настройки.** `sink_pack::Fanout` — fan-out без виртуальных вызовов; `sink_io` — общие `open_output`/`write_all`/`fsync`; `settings_io`/`settings_paths`/`settings_source`/`settings_diagnostic` — холодная загрузка JSON и диагностика. `SinkBinding<Event>` связывает sink с событием без знания о модуле-владельце.
- **Integrity.** CRC-32C, CRC-64/XZ, SHA-256, сравнение дайджестов за постоянное время и `read_verified(path, sha256)`.
- **Концепты.** `Message`, `LevelLogger`, `Logger<T, Format>`, `Sink`, `LifecycleSink`, `CounterLike`, `GaugeLike`, `StateGaugeLike`, `DescriptiveStatisticsLike`, `HistogramLike`, `MeterLike` живут здесь, чтобы модули могли принимать компоненты, не завися от чужих реализаций. Доменные концепты остаются в своих модулях.

## Ключевые типы

| Тип | Роль |
|---|---|
| `Result<T, E>` / `Status` | модель ошибок над `std::expected` |
| `ErrorCode` | `Ok`, `BufferFull`, `BufferEmpty`, `InvalidArgument`, `SystemError`, `NotInitialized`, `RateLimited`, `Conflict`, `ResourceExhausted` |
| `SpscRingBuffer<T, Capacity>` | wait-free SPSC-очередь, фиксированная ёмкость, индексы на отдельных cache line |
| `ShardPool` | per-thread shard'ы для многопроизводительной записи |
| `CpuSet` / `ThreadSpec` / `ThreadApplyReport` | CPU-маска, атрибуты потока и отчёт применения |
| `WorkerThread` / `WorkerTuning` | фоновый потребитель и политика idle-сна |
| `FixedWriter` | bounds-checked JSON-писатель без аллокаций |
| `EnumTraits` | таблица канонических имён enum'ов |
| `SinkBinding<Event>` / `sink_pack::Fanout` | связывание и fan-out sink'ов |
| `settings_*` | загрузка global/local JSON и диагностика |
| `integrity` | CRC-32C, CRC-64/XZ, SHA-256, constant-time compare |
| `concepts/*` | структурные контракты для развязки модулей |

## Горячий и холодный путь

Горячий путь в `core` — это SPSC-очередь, `enum`-форматирование, `Result`-проверки и cache-line-выровненные атомики. Здесь нет аллокаций, блокировок и JSON. Холодный путь — парсинг настроек, JSON, файловые операции и диагностические строки; он может аллоцировать и использовать `std::string`.

Граница проходит по типу операции, а не по модулю: `Result` используется и на горячем, и на холодном пути, но его создание не аллоцирует; `to_string`/`to_json` всегда холодные.

## Связи

```text
             +-----------------------------+
             |            core             |
             | Result · SPSC · threads     |
             | concepts · integrity        |
             +--------------+--------------+
                            |
      +---------------------+---------------------+
      |                     |                     |
   chrono                 com            logging/metrics/tracing/
      |                     |            diagnostics/recorder
      +---------------------+---------------------+
                            |
                     app / exec / hub / phm / sm / synchro
```

`core` не знает ни одного доменного модуля. Все остальные зависят от него; обратной зависимости нет. Именно поэтому `Logger`-концепт принимает шаблон формата параметром: `core` не называет `logging::FormatWithLocation`, но может проверить реальную сигнатуру логгера.

## Контракты и границы

- `core` зависит только от стандартной библиотеки и `nlohmann/json`.
- Глобальные `-fno-exceptions -fno-rtti`; единственное исключение — cold-path RTTI в `com` для iceoryx2.
- `.value()` на `Result`/`Status` запрещён; используйте `has_value()`, `*`, `->`, `.error()`.
- Каждый публичный тип имеет `to_string` (член и свободную функцию), `to_json`/`from_json`, `std::formatter`; каждое перечисление — `constexpr to_string`/`from_string`.
- `core` не содержит доменных типов и не зависит от `chrono`/`com`; если примитиву нужен доменный тип, он остаётся в модуле-владельце.
- `core` не является «свалкой утилит»: сюда попадает только то, что действительно общее для нескольких модулей и не тянет зависимость вверх.

## Capabilities

`affinity`, `integrity`, `log-level`, `result`, `serialization`, `settings-vocabulary`, `sink-plumbing`, `spsc-ring-buffer`, `thread-management`, `worker-thread`, `worker-tuning`.

## Читать дальше

- [`docs/architecture.md`](../architecture.md), раздел 4.1 — карта типов и зависимостей.
- [`platform.md`](platform.md) — место `core` в трёх плоскостях.
- [`chrono.md`](chrono.md), [`com.md`](com.md), [`app.md`](app.md) — первые потребители примитивов.
- Спецификации: `openspec/specs/core/*`.
