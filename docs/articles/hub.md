# `hub`: наблюдаемость без управления

`nexigen::hub` — read-only обсерватория над завершёнными и текущими запусками. Она не запускает приложения, не меняет состояния и не пишет в run-директорию; она обнаруживает артефакты, доставляет телеметрию клиентам и отдаёт HTTP/SSE API и встроенный web UI.

Модуль зависит от `core` и от `exec` только ради типов раскладки (`SuitePaths`, `InstanceLayout` и т.п.). Он ничего не запускает и не изменяет.

## Ответственность

- `Discovery` — явные run-root'ы, канонизация путей, `inotify` и согласование дерева `run → suite → app → instance/attempt`.
- `StreamManager` — ограниченные tailer'ы, replay-ring по числу записей и байтам, `GapMarker` при переполнении, подписчики.
- `HttpServer` — один epoll-цикл: listener, eventfd, inotify, клиенты, таймеры, polling потоков; HTTP/1.1 + SSE; встроенный web UI; только `GET`.
- DTO: `HubTree`, `HubRun`, `HubSuite`, `HubApp`, `HubInstance`, `HubAttempt`, `HubStream`, `StreamRecord`, `StreamKind`.

## Ключевые типы

| Тип | Роль |
|---|---|
| `Discovery` | обнаружение run-root'ов и дерева запусков |
| `StreamManager` | tailer'ы, replay-ring, подписчики |
| `HttpServer` | HTTP/1.1 + SSE, web UI |
| `HubTree` / `HubRun` / `HubSuite` | иерархия запусков |
| `HubApp` / `HubInstance` / `HubAttempt` | приложение и попытки |
| `HubStream` / `StreamRecord` / `StreamKind` | поток и запись |
| `GapMarker` | разрыв в потоке при переполнении ring'а |

## Discovery

`Discovery` принимает явные run-root'ы, канонизирует пути и строит дерево по мере появления директорий. `inotify` даёт события об изменениях, но решение о структуре принимает код: run → suite → app → instance/attempt. Никаких «угаданных» путей из cwd или hostname нет.

Дерево — это не только навигация. Оно задаёт, какие артефакты принадлежат какому запуску: `suite.json`, `suite-state.json`, `suite-exit.json`, `resource.json`, run-лог и JSONL-потоки. `hub` читает их как read-only.

## Streaming

`StreamManager` держит ограниченные tailer'ы для каждого потока. Replay-ring ограничен и по числу записей, и по байтам; при переполнении подписчик получает `GapMarker` с диапазоном, а не молчаливую дыру. Это тот же принцип, что в телеметрии и рекордере: потеря должна быть видимой.

Подписчики получают записи через SSE. Медленный клиент не должен разрастаться в бесконечную очередь: лимиты и gap-маркеры — часть контракта.

## HTTP/SSE

`HttpServer` — один epoll-цикл, который обслуживает listener, `eventfd`, `inotify`, клиентов, таймеры и polling потоков. HTTP/1.1 и SSE; встроенный web UI. Только `GET`: `hub` не принимает команды и не меняет состояние. Неизвестный путь — `404`, неверный метод — `405`, плохой запрос — `400`. Лимиты на размер запроса и число клиентов входят в контракт.

## Связи

```text
run directory: suite.json · suite-state.json · JSONL · run log
        |
        v
Discovery (inotify) -> HubTree
        |
        v
StreamManager (tailers, replay ring) -> SSE
        |
        v
HttpServer (epoll) -> HTTP/SSE / web UI
```

`hub` читает то, что создали `app`, `exec` и демоны телеметрии. Он не заменяет `nexigen-logd`/`nexigen-metricsd`/`nexigen-traced`: те агрегируют кадры и пишут файлы, `hub` их читает. `nexigen-export` работает с raw-сессиями, `hub` — с run-директориями и JSONL.

## Контракты и границы

- Read-only: только `GET`; никаких `POST`, запусков и изменений состояния.
- Явные run-root'ы; пути канонизируются; дерево строится по фактическим директориям, без догадок.
- Replay-ring ограничен по записям и байтам; переполнение даёт `GapMarker`.
- Один epoll-цикл; лимиты запроса и клиентов; `400`/`404`/`405`.
- `hub` не зависит от `com`; он работает с файлами и типами раскладки `exec`.
- SSE-клиент не влияет на producer'ов; медленный клиент получает gap, а не блокирует поток.
- `hub` не является местом хранения: он не переписывает артефакты.

## Capabilities

`discovery`, `http-api`, `streaming`, `web-ui`.

## Читать дальше

- [`docs/architecture.md`](../architecture.md), раздел 4.10 — карта типов и HTTP-контракта.
- [`platform.md`](platform.md) — место `hub` в плоскости наблюдаемости.
- [`exec.md`](exec.md) — артефакты, которые читает `hub`.
- [`logging.md`](logging.md), [`metrics.md`](metrics.md), [`tracing.md`](tracing.md) — источники JSONL/кадров.
- Спецификации: `openspec/specs/hub/*`.
