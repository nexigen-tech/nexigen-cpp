# `diagnostics`: измерение интервалов

`nexigen::diagnostics` — самый маленький модуль платформы. Его задача — дешёвое измерение длительности операций и бюджетов реального времени, без аллокаций, атомиков и блокировок.

## Ответственность

- `Stopwatch` — один cache line, trivially copyable; источник штампа — `Timestamp::now()` или внедрённый `StampSource`.
- `ScopedStopwatch` — RAII-гард, который измеряет область и отдаёт результат в `OutRefSink`.
- `StopwatchState` — состояние измерения.
- `LapSnapshot` — снимок интервала/круга.

## Ключевые типы

| Тип | Роль |
|---|---|
| `Stopwatch` | дешёвый таймер на один cache line |
| `ScopedStopwatch` | RAII-измерение области |
| `OutRefSink` | приёмник результата без владения |
| `StopwatchState` | состояние измерения |
| `LapSnapshot` | снимок интервала |

## Горячий и холодный путь

Горячий путь — старт/стоп: чтение `Timestamp` или `StampSource`, вычитание, запись в `StopwatchState`. Ни аллокаций, ни блокировок, ни атомиков. Холодный путь — рендеринг `LapSnapshot`, агрегация, запись в sink. `Stopwatch` не логирует сам: он отдаёт число, а решение, куда его положить, принимает вызывающий.

## Связи

```text
Stopwatch / ScopedStopwatch
        |
        +-- Timestamp::now() или StampSource (chrono)
        +-- OutRefSink -> метрика / лог / собственный обработчик
```

`diagnostics` зависит от `core` и `chrono`. Он не зависит от `logging`/`metrics`: sink результата — это `OutRefSink`, а не жёсткая привязка к конкретному модулю. Поэтому один и тот же замер можно отправить в счётчик, в лог или в тест.

## Контракты и границы

- `Stopwatch` — один cache line, trivially copyable; без атомиков, блокировок и кучи.
- Источник штампа можно внедрить (`StampSource`), чтобы измерять в калиброванном домене, а не только через `Timestamp::now()`.
- `ScopedStopwatch` не аллоцирует; результат передаётся по ссылке/через `OutRefSink`.
- Модуль измеряет, но не решает, что делать с числом; политика — у вызывающего.
- `diagnostics` не является заменой метрикам: это точечный замер, а не реестр инструментов.
- Снимки (`LapSnapshot`) — cold-path значения; на горячем пути их не форматируют.

## Capabilities

`examples`, `scoped-stopwatch`, `stopwatch`, `stopwatch-state`.

## Читать дальше

- [`docs/architecture.md`](../architecture.md), раздел 4.7 — краткая карта.
- [`platform.md`](platform.md) — место `diagnostics` в бюджете наносекунд.
- [`chrono.md`](chrono.md) — источник штампа.
- [`metrics.md`](metrics.md) — куда обычно уходит результат.
- Спецификации: `openspec/specs/diagnostics/*`.
