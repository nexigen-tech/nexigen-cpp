# Realtime log viewer (`nexigen-logview`)

`nexigen-logview` — Linux-only GUI-просмотрщик живого потока `LogFrame` из
iceoryx2. Он не меняет поведение приложений: это ещё один подписчик сервиса
`Nexigen/Log/nexigen`, который можно запустить и остановить в любой момент.

## Сборка

GUI-цель включается отдельно и требует COM-сборки:

```bash
cmake -S . -B build/logview -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DNEXIGEN_ENABLE_COM=ON \
  -DNEXIGEN_BUILD_LOGVIEW=ON
cmake --build build/logview -j 64 --target nexigen-logview
```

Нужны X11/OpenGL development-пакеты (`libx11-dev`, `libxrandr-dev`,
`libxinerama-dev`, `libxcursor-dev`, `libxi-dev`, `libxext-dev`,
`libxrender-dev`, `libgl1-mesa-dev`). Сам GLFW 3.4 и Dear ImGui
загружаются FetchContent по закреплённым версиям, системный `glfw3` не
требуется. Шрифт JetBrains Mono (OFL) встроен в бинарник, лицензия
устанавливается рядом с ним.

Headless-модель и тесты не линкуют OpenGL:

```bash
cmake -S . -B build/logview-headless -G Ninja \
  -DNEXIGEN_ENABLE_COM=ON -DNEXIGEN_BUILD_LOGVIEW=ON \
  -DNEXIGEN_LOGVIEW_GUI=OFF
cmake --build build/logview-headless -j 64 --target nexigen_logview_tests
./build/logview-headless/tools/nexigen-logview/nexigen_logview_tests
```

## Запуск

```bash
# живой поток
nexigen-logview

# другой сервис и бюджет истории
nexigen-logview --service Nexigen/Log/nexigen --memory-mb 384

# файл без transport
nexigen-logview --offline run/nexigen.jsonl

# 4K/High-DPI: увеличить шрифт и размеры контролов
nexigen-logview --ui-scale 2

# headless: проверить парсер/модель без дисплея
nexigen-logview --headless --offline run/nexigen.jsonl
nexigen-logview --headless --mock 50000 --max-records 50000
nexigen-logview --soak 600   # 50k msg/s model soak, headless
```

`--ui-scale` задаёт масштаб шрифта и ImGui-контролов. Без флага viewer
пытается взять content scale основного монитора; для 4K обычно подходит
`--ui-scale 2` (допустимый диапазон 0.5–4.0).

`--memory-mb` задаёт жёсткий бюджет модели. По умолчанию 256 MiB дают
524 288 записей; 384 MiB — 1 048 576. Аллокация одна (`mmap`), история не
растёт во время работы.

## Управление

| Клавиша/кнопка | Действие |
|---|---|
| `Space` | пауза/продолжить; поток при этом не останавливается |
| `F2` | поставить/снять bookmark на выбранной записи |
| `F3` / `Shift+F3` | следующий/предыдущий bookmark |
| `F4` / `Shift+F4` | следующая/предыдущая Warning/Error/Fatal запись |
| `T` | сделать выбранную запись T0 |
| `Ctrl+F` | поиск; `Enter` — следующий матч, `Shift+Enter` — предыдущий |
| `Ctrl+A` | выбрать все видимые записи |
| `Ctrl+C` | скопировать запись под курсором (или выбранную) в clipboard |
| ПКМ по строке | «Copy message» / «Copy full record» |
| `Shift+клик` | выделить диапазон в видимом порядке |
| `Ctrl+клик` | переключить выбор одной записи |
| «Return to live» | вернуться к хвосту после ручной прокрутки |
| «Reset config» | сбросить цвета, фильтры, режимы и порог; если сервис/бюджет/потоки отличаются, toolbar показывает `RESTART` до перезапуска |

Панель источников/уровней задаёт фильтр. Фильтр не удаляет данные:
изменение критерия перестраивает видимый индекс кусками по 20 000 записей за
кадр. Find не фильтрует и не меняет порядок строк.

Инспектор показывает полный payload, `thread_id`, `pid`, `dropped_counter`,
wire version и предупреждает, если payload заполнил весь кадр. Кнопка Copy
кладёт запись в clipboard.

## Потери и пауза

Потери не скрываются. Транспортный `lost_before`, переполнение hand-off
очереди и `dropped_counter` издателя превращаются в отдельные gap-записи с
причиной и количеством. Пауза замораживает только отображаемый диапазон:
если история успела провернуться, баннер сообщает, сколько показанных
записей было перезаписано.

## Экспорт и импорт

«Save selected» пишет выбранные записи в JSONL с полями `nexigen-logd`
(`timestamp`, `mono_ns`, `boot`, `app`, `level`, `thread_id`, `pid`,
`message`), CSV или TXT. JSONL корректно экранирует кавычки, обратные слэши и
управляющие символы. Запись идёт в отдельном потоке с очередью на 4096
записей; UI не блокируется.

«Open» принимает собственный JSONL, JSONL `nexigen-logd` (включая строки с
неэкранированным `message`) и plain text. Разбор идёт построчно bounded
чанками, битые строки считаются и пропускаются. Если в файле встречаются
разные boot-токены, viewer предупреждает и не показывает cross-session
дельты как сравнимые.

## ECO и бюджет

Viewer считает собственный CPU share через `CLOCK_PROCESS_CPUTIME_ID` и
входит в ECO после трёх окон выше лимита (по умолчанию 50%) или при
входящем потоке выше 100 000 msg/s. В ECO он рисует 15 FPS, просыпаясь по
событиям, и пересчитывает фильтры/статистику не чаще 250 мс. Выход — после
двух окон ниже 70% порога. Capture-поток имеет `nice 10`, export — `nice 19`;
оба принимают affinity из JSON-конфигурации.

Конфигурация лежит в `$XDG_CONFIG_HOME/nexigen/logview.json` (или
`--config`). Запись атомарная (temporary + rename), повреждённый файл даёт
одно предупреждение и дефолты. Layout dock-окон хранится рядом в
`logview-layout.ini`.
