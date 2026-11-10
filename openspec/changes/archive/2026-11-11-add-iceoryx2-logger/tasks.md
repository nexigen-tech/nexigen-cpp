# Tasks

## 1. LogFrame type

- [x] 1.1 Create
- [x] 1.2 ADL

## 2. Iceoryx2LoggerConfig

- [x] 2.1 Create

## 3. Sink + logger

- [x] 3.1 Create
- [x] 3.2 Create
- [x] 3.3 CMake: цель `nexigen_logging_com` за `NEXIGEN_ENABLE_COM` (линкует `nexigen_com`), по паттерну `nexigen_state_client`; ядро `nexigen_logging` остаётся без iceoryx2. Verify: гейт ON собирает цель, гейт OFF её не создаёт.

## 4. Tests

- [x] 4.1 Функциональный round-trip: `Iceoryx2Logger` публикует, `Subscriber<LogFrame>` получает фрейм с отрендеренным сообщением и метаданными (level/thread_id/app_id). Verify: тест проходит.
- [x] 4.2 Drop-семантика (`REQ_LOGC_007`): переполнение shard-ring → следующий фрейм несёт `dropped_counter` = числу дропов, следующий за ним — 0. Verify: тест проходит.
- [x] 4.3 Публикация с нулём подписчиков: `send`/`notify` без подписчика не падают. Verify: тест проходит.

## 5. Verification matrix

- [x] 5.1 Debug gate-ON: полный `ctest` зелёный.
- [x] 5.2 Release `-O3 -march=native -flto` + `NEXIGEN_STRICT_LATENCY_TESTS=ON`: полный `ctest` зелёный (латентность-гейты проходят).
- [x] 5.3 TSAN (`-DNEXIGEN_ENABLE_TSAN=ON`) и ASAN (`-DNEXIGEN_ENABLE_ASAN=ON`) gate-ON: полный `ctest` зелёный, нет sanitizer-отчётов.

## Workflow follow-up

- `LogDaemon` (подписка, финальное форматирование, диск/сеть/crash-ring) — отдельный change демонной волны.
- Archive the change after the project's review requirements are satisfied.
