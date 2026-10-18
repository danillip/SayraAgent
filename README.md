# Sayra Core — Stage 1

C++20-библиотека `sayra-core` и консольное приложение `sayra-cli` для Windows 10/11 x64, MSVC и Visual Studio 2026. Конфигурация сборки — CMake/vcpkg. Старый `Sayra.cpp` сохранён как `sayra-model-tests`. `Sayra.slnx` включает новые проекты Core, CLI, автономных тестов и прежнюю диагностику.

**Статус: пользователь собрал автономные тесты в Visual Studio; разрешённый однократный запуск 18 октября 2026 — 28/28 PASS, код завершения 0.** Агент не выполнял сборку. Работа CLI с настоящими Ollama/cloud API этим запуском не проверялась. Подробности: [docs/IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md).

## Открытие и ручная сборка

Требуются Visual Studio 2026 с Desktop development with C++, MSVC v145, Windows SDK, CMake; Git; vcpkg; Ollama для локальной генерации. Зависимости: libcurl с Windows TLS (Schannel), nlohmann/json, SQLite3. Python, Docker, WSL и Qt не используются.

1. Откройте **Sayra.slnx** в Visual Studio 2026. Если решение уже открыто, закройте и откройте его заново.
2. Выберите **Debug | x64**; в обозревателе решений нажмите правой кнопкой на **sayra-cli → Назначить запускаемым проектом**.
3. При необходимости скопируйте config/sayra.example.json в sayra.json в корне репозитория. Без него действуют безопасные локальные настройки.
4. Нажмите **Локальный отладчик Windows** (F5). При первой вашей сборке MSBuild/vcpkg автоматически восстановит библиотеки, а sayra-core соберётся как зависимость CLI. Автономные тесты не запускаются автоматически.

Альтернатива в Developer PowerShell for VS 2026 — **выполняет пользователь**:

```powershell
# Только если tools/vcpkg отсутствует:
git clone https://github.com/microsoft/vcpkg.git tools/vcpkg
# Подготовка vcpkg:
.\tools\vcpkg\bootstrap-vcpkg.bat -disableMetrics
# Отдельная установка библиотек необязательна: manifest восстанавливается CMake.
# При желании установить заранее:
.\tools\vcpkg\vcpkg.exe install --triplet x64-windows
Copy-Item config\sayra.example.json sayra.json
cmake --preset vs2026-x64
cmake --build --preset debug
.\build\core\Debug\sayra-cli.exe .\sayra.json
# Автономные тесты запускаются отдельно:
ctest --preset debug
```

Запускайте CLI из корня репозитория или передайте абсолютный путь к конфигу. `data_dir` относительно текущего рабочего каталога, а не каталога exe. SQLite-история хранится открытым текстом в `data/history.sqlite3`; не регистрируйте этот каталог как исходный проект. Конфиг и история исключены из Git.

## Первый локальный запуск

Запустите Ollama. Sayra ничего не скачивает и не устанавливает из AI-моделей. Нужна хотя бы одна установленная модель с возможностью `completion`. Имена узнаются через `/api/tags` и `/api/show`, загруженность — `/api/ps`. Никакая конкретная chat-модель не обязательна. Если сервер не сообщает capabilities, задайте достоверные `chat`/`tools` в профиле.

Для `/embed-test` нужен BGE-M3. Пользователь при необходимости сам выполняет `ollama pull bge-m3`. Для демонстрации переключения моделей нужны минимум две chat-модели; coding-модель необязательна, без неё используется общий допустимый кандидат.

```text
/models
Привет
Создай класс на C++20 для управления задачами
/why-model
/model ТОЧНОЕ_ИМЯ_ИЗ_MODELS
Объясни RAII
/model auto
```

Чтобы отличался выбор, настройте `models` в `sayra.json`, перезапустите CLI. Имена — точные из `/models`, роли — ваши настройки, а не неподтверждённые оценки по названию:

```json
{"name":"ТОЧНОЕ_ИМЯ", "provider":"ollama", "chat":true,
 "roles":["coding"], "context":8192, "priority":10}
```

Допустимые роли: `fast_chat`, `general_qa`, `coding`, `reasoning`, `research_synthesis`, `live_assistance`, `embedding`. Профили применяются только к обнаруженным моделям. Скорость/качество задаются отдельно; готовых вымышленных рейтингов нет.

## Команды

`/help`, `/models` (обновляет список), `/model auto|<name>`, `/mode local|cloud|hybrid|auto`, `/status`, `/doctor`, `/why-model`, `/new`, `/history`, `/history list`, `/history load <id>`, `/history delete <id>`, `/project list`, `/project add <path>`, `/project use <id>`, `/project info`, `/embed-test`, `/live-demo`, `/cancel`, `/exit`.

Обычный текст — запрос. Генерация работает в `std::jthread`; во время неё CLI принимает `/cancel` и `/exit`. Остальные команды ждут освобождения ядра. На старте и при `/models` диагностика провайдеров выполняется синхронно с таймаутом. Ввод одной строкой, русский UTF-8 поддерживается.

## Проектные файлы

```text
/project add "C:\Projects\Sayra"
/project info
Объясни файл src/routing.cpp из выбранного проекта
/history
```

Добавление каталога — разрешение чтения его допустимых текстовых файлов. Переход на другой проект начинает отдельную беседу. Явно названный относительный путь читается детерминированно даже без native tools. В остальных случаях модель с tools может вызвать поиск/чтение; без tools показывается список и требуется уточнение пути. Большие файлы/контекст ограничены: при ошибке контекста задайте меньший файл или увеличьте `generation.context` в пределах возможностей модели.

## Облачный API

В `cloud` укажите `enabled:true`, HTTPS `base_url` **с `/v1`**, переменную `key_env`. Ключ — только в окружении процесса Visual Studio/CLI (`SAYRA_CLOUD_API_KEY`), не в JSON. Назначьте профилю реально обнаруженной `/models` облачной модели `provider:"cloud"`, `chat:true`, достоверный `tools`, роли и `cost_per_1k_tokens`. Укажите разрешённый потолок `max_cost_per_1k_tokens`; без цены облачная модель исключается.

После перезапуска `/mode cloud` и текстовый вопрос проверят облачную генерацию. API должен поддерживать `/models` и Chat Completions; provider-specific API, Responses-only и подписка ChatGPT не заменяют этот контракт. API обычно оплачивается отдельно. Обнаружение не вызывает платную генерацию.

Проект и транскрипция по умолчанию в облако не отправляются. `allow_project_data` и `allow_transcripts` — отдельное явное согласие пользователя в конфиге. Политика учитывается и при fallback. Фразы с признаками секретов и беседы, помеченные sensitive, остаются локальными. Локальный режим работает независимо от ключа. Подробнее: [API_PROVIDERS](docs/API_PROVIDERS.md), [MODEL_ROUTING](docs/MODEL_ROUTING.md).

## Демонстрации

`/embed-test`: шесть троек предложений, реальные векторы через `/api/embed`, конечность/ненулевость, размерность BGE-M3 1024, cosine AB/AC, разница и PASS/FAIL. Синтетические значения используются только в автономных unit-тестах, не в CLI.

`/live-demo`: имитация предварительного и финального текстового сегмента о `std::mutex`. Предварительный сегмент помечен как preliminary, в LLM поступает только финальный. Ответ и карточка создаются реальной моделью. Микрофон не используется.

Запросите последние новости: без подключённого Research-модуля ядро явно помечает ответ непроверенным и сообщает, что нужен веб-поиск. Оно не создаёт проверенных источников из текста LLM.

## Передача команде

- Developer 2: `include/sayra/integration/knowledge.hpp`, `include/sayra/embeddings/embeddings.hpp`.
- Developer 3: `include/sayra/integration/realtime.hpp`, `include/sayra/events/events.hpp`, `include/sayra/core/types.hpp`.
- Обязательные условия владения/потоков/отмены: [INTEGRATION_CONTRACTS](docs/INTEGRATION_CONTRACTS.md).
- Архитектура: [ARCHITECTURE](docs/ARCHITECTURE.md), распределение работ: [TEAM_INTEGRATION](docs/TEAM_INTEGRATION.md).
- Проверки: [TEST_PLAN](docs/TEST_PLAN.md), настройка Windows: [SETUP_WINDOWS](docs/SETUP_WINDOWS.md).
Без разрешённого Research-провайдера запрос актуальной информации завершается детерминированным сообщением без вызова LLM. Это предотвращает выдачу выдуманных актуальных фактов и ссылок даже при непослушном ответе модели.

Также доступен CMake: откройте папку, выберите preset vs2026-x64 и цель sayra-cli. Native solution использует общие настройки build-support/Sayra.Common.props, а CMake — CMakeLists.txt; оба варианта включают один и тот же набор исходников Core. Вывод native-сборки — out/vs/x64/Debug или Release. Для выполнения автономных тестов пользователь отдельно назначает sayra-core-tests запускаемым проектом.

Перед публикацией прочитайте [правила безопасной публикации](docs/PUBLISHING.md).
