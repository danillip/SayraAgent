# Sayra Core 0.1 — архитектура

Поток: `AgentRequest → IntentClassifier → TaskPlanner → ModelRouter → Project/Knowledge/Research context → PromptBuilder → IModelProvider → controlled ToolExecutor loop → AgentResponse/AssistCard → SQLite`.

Публичные интерфейсы находятся в include/sayra. CMake собирает реализацию src в статическую библиотеку sayra-core. CLI отдельно отвечает за ввод и рабочий поток. Core не зависит от Qt, захвата звука, баз векторного поиска или веб-поисковых движков.

| Подсистема | Реализация | Ответственность |
|---|---|---|
| Конфигурация | config.cpp | JSON, safe defaults, лимиты |
| HTTP | transport.cpp | libcurl RAII, TLS, таймаут, stop_token, лимит тела |
| Потоки | StreamParser | NDJSON/SSE, произвольные границы, CRLF, comments/data |
| Провайдеры | providers.cpp | Ollama, OpenAI-compatible, model registry |
| Выбор | routing.cpp | классификация/уточнение, план и оценка кандидатов |
| Проекты | projects.cpp | зарегистрированные каталоги, allowlist, поиск, чтение |
| Инструменты | tools.cpp | schema validation, permission policy, budgets |
| Диалог | memory.cpp | SQLite, retention, беседы, ограничение истории |
| Ядро | orchestrator.cpp | конвейер, native tools, fallback, источники, события |
| Embeddings | embeddings.cpp | векторы, размерность, устойчивая cosine |
| Desktop ingress | realtime.cpp | partial/final, дедупликация, ограничение контекста |

Один экземпляр оркестратора обслуживает один запрос одновременно. CLI сериализует запросы и не меняет registry/projects/conversation во время генерации. Для будущих нескольких бесед создавайте отдельные оркестраторы и явно синхронизируйте общие registry/projects. EventBus и MemoryStore имеют собственную синхронизацию; это не делает все остальные компоненты автоматически thread-safe.

EventBus копирует shared_ptr подписчиков под mutex и вызывает их без удержания mutex. Владение подписчиками остаётся у хоста. Подписки weak_ptr не продлевают жизнь UI. Исключения одного получателя не обрывают генерацию. После unsubscribe уже скопированный callback ещё может завершаться: для закрытия UI используйте очередь и lifetime guard.

Результаты инструментов — недоверенные данные. Чтение ограничено известными текстовыми расширениями, байтами и зарегистрированным корнем. Windows: запрещены ADS, `..`, reparse/symlink/hard-link, проверяется реально открытый file handle, а не только имя до открытия. Это не заменяет ОС-изоляцию; код предназначен для выбранного пользователем локального проекта.

Никаких инструментов запуска shell и записи файлов нет. PermissionPolicy отказывает опасным инструментам Stage 1, даже если передан confirmation callback. Prompt-injection защищается границами ролей/данных и ограничениями инструментов; системные ограничения не делегируются модели.

MemoryStore хранит ограниченные текстовые беседы и метаданные. По умолчанию не более 100 бесед, 100 сообщений и 16000 байт на беседу; содержимое файлов/tool exchanges отдельно в SQLite не сохраняется. Транскрипция в пользовательском сообщении и ответ могут сохраняться — это открытый текст на локальном диске. Удаление через /history delete не является гарантированным физическим стиранием носителя.