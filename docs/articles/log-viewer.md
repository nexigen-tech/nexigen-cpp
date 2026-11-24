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
`libxrender-dev`, `libgl1-mesa-dev`) и `libpng-dev` для встроенной
startup-заставки. Сам GLFW 3.4 и Dear ImGui
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

При обычном интерактивном запуске сначала на одну секунду показывается
встроенная заставка Nexigen. Capture и приём логов уже работают во время
заставки, поэтому первые записи не теряются. Headless и scripted/smoke режимы
заставку пропускают.

## Вкладки и окна

Наблюдение строится из независимых **log views**. Каждая view — отдельное
dock-окно `Log: <источник>`; ImGui может собрать их в табы или показать рядом.
Окно `Log: All logs` открыто по умолчанию и показывает все источники. Левая
панель `Sources` — глобальный каталог приложений и лаунчер: `All logs`
открывает общую view, `Open` у приложения создаёт view, закреплённую за этим
источником.

У каждой view собственные:

- набор источников (`Sources...` доступен только у `All logs`);
- уровни и текстовый фильтр;
- видимый индекс, find, selection, bookmarks и minimap;
- пауза, позиция прокрутки и режим auto-scroll;
- timestamp/delta режимы, порог аномалии и T0;
- выбор для экспорта.

Общими остаются capture, live-история, реестр приложений, цвета, time base,
ECO, UI scale и dock layout. Поэтому две view могут показывать разные срезы
одной и той же живой истории, не дублируя подписку.

В каждой Log view есть три кнопки навигации с иконками в стиле Wireshark:

- **First message** — перейти к старейшей видимой записи и выключить auto-scroll.
- **Last message** — перейти к новейшей видимой записи, не меняя auto-scroll.
- **Auto-scroll** — включить/выключить следование за новыми записями.

Auto-scroll включён по умолчанию. Пока он включён, новые записи удерживают
таблицу внизу; после выключения или ручной прокрутки можно свободно
навигироваться по истории. Пауза приостанавливает следование, но не сбрасывает
состояние кнопки: после Resume поведение остаётся таким, каким его оставил
пользователь.

`All logs` можно закрыть и снова открыть из `Sources`; это освобождает и
занимает один LIVE-слот. Лимиты: не более **8 LIVE view** включая `All` и не
более **16 PAUSED view**. При превышении новая view не создаётся, а окно
показывает причину.

### Clone

Кнопка `Clone` в окне view открывает диалог:

- `LIVE` — новая view следует за общим live-хвостом и начинает с новых
  записей;
- `PAUSED` — новая view получает неизменяемый snapshot.

Чекбоксы копирования по умолчанию включены все: уровни, текстовый фильтр,
timestamp mode, delta mode, порог аномалии, selection, bookmarks, T0 и find
query. Источник наследуется всегда, позиция прокрутки — никогда. Selection и
bookmarks обрезаются по snapshot; T0 вне snapshot сбрасывается; find query
заново выполняется на копии.

PAUSED snapshot для source view содержит только записи этого приложения;
для `All logs` — все источники. Копируются самые новые записи, не более
1 MiB record data (около 3 900 записей), плюс gap-записи, попавшие в
выбранный диапазон `seq`. Старые записи отбрасываются. Snapshot не зависит от
текущих фильтров: скрытые уровни и текст остаются в данных, а фильтры копии
применяются к ним заново.

Если selection, bookmarks или T0 не помещаются в snapshot, clone обрезает их и
показывает это в строке состояния (`clone clipped: ...`). PAUSED view живут
только до перезапуска. После рестарта восстанавливаются
`All` и LIVE source view вместе с их источниками, уровнями, текстом,
режимами и порогом; selection, bookmarks, T0, find, прокрутка и PAUSED
snapshot — session-only.

## Управление

| Клавиша/кнопка | Действие |
|---|---|
| `Space` | пауза/продолжить focused view; поток при этом не останавливается |
| `F2` | поставить/снять bookmark на выбранной записи |
| `F3` / `Shift+F3` | следующий/предыдущий bookmark |
| `F4` / `Shift+F4` | следующая/предыдущая Warning/Error/Fatal запись |
| `T` | сделать выбранную запись T0 |
| `Ctrl+F` | поиск; `Enter` — следующий матч, `Shift+Enter` — предыдущий |
| `Ctrl+A` | выбрать все видимые записи |
| `Ctrl+C` | скопировать запись под курсором (или выбранную) в clipboard |
| `Ctrl+Home` | перейти к первой видимой записи focused view |
| `Ctrl+End` | перейти к последней видимой записи focused view |
| ПКМ по строке | «Copy message» / «Copy full record» |
| `Shift+клик` | выделить диапазон в видимом порядке |
| `Ctrl+клик` | переключить выбор одной записи |
| «Reset config» | сбросить цвета, фильтры, режимы и порог; если сервис/бюджет/потоки отличаются, toolbar показывает `RESTART` до перезапуска |

Клавиши действуют на **последнюю focused Log view**. Если ни одно Log-окно не
в фокусе (например, фокус в `Sources`), глобальные клавиши игнорируются.
Кнопки `Pause`, `Clone`, `Save selected`, уровни, фильтр, find, режимы и
навигация по bookmarks/Warning находятся внутри каждого Log-окна.

Фильтр не удаляет данные: изменение критерия перестраивает видимый индекс
кусками; активные view делят бюджет 20 000 записей за кадр. Find не фильтрует
и не меняет порядок строк.

Инспектор показывает выбранную запись focused view: полный payload,
`thread_id`, `pid`, `dropped_counter`, wire version, и предупреждает, если
payload заполнил весь кадр. Кнопка Copy кладёт запись в clipboard.

## Экспорт

`Save selected` есть в каждой view и экспортирует только её selection. Writer
один на процесс: вторая попытка во время уже идущего экспорта отклоняется.
Экспорт сначала копирует selection, поэтому закрытие исходной view не
отменяет уже начатую запись. `Clear` очищает общую live-историю и состояние
LIVE view; открытые PAUSED snapshot остаются.

## Потери и пауза

Потери не скрываются. Транспортный `lost_before`, переполнение hand-off
очереди и `dropped_counter` издателя превращаются в отдельные gap-записи с
причиной и количеством. Пауза замораживает только отображаемый диапазон:
если история успела провернуться, баннер сообщает, сколько показанных
записей было перезаписано.

## Экспорт и импорт

«Save selected» пишет выбранные записи текущей view в JSONL с полями `nexigen-logd`
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
`--config`). В ней хранится массив `views` с определениями `All` и LIVE view
(стабильный id, title, source, levels, text, modes, anomaly threshold,
hidden sources). Запись атомарная (temporary + rename), повреждённый файл даёт
одно предупреждение и дефолты. Layout dock-окон и порядок табов хранится
рядом в `logview-layout.ini`.
