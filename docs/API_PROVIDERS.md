# HTTP-провайдеры

IModelProvider: id, discover, generate. GenerationRequest содержит сообщения, модель, инструменты, GenerationOptions и request_id. DeltaCallback отдаёт текст во время получения, отдельный thinking=true — только статус обработки, без скрытых рассуждений. GenerationResult содержит итог и tool calls.

## Ollama

По умолчанию http://127.0.0.1:11434. GET /api/tags и /api/ps, POST /api/show. POST /api/chat поддерживает stream/non-stream, options и keep_alive. NDJSON разбирается по строкам, сетевые chunks не считаются готовым JSON. Отсутствие done — ошибка. Ошибки HTTP и поля error не выдаются за ответ. Tool calls конвертируются в типы Sayra. Embeddings отдельно через /api/embed, не через chat.

## OpenAI-compatible

HTTPS base URL включает API prefix (например https://api.openai.com/v1). Реализованы GET /models и POST /chat/completions. Redirects отключены, TLS проверяется libcurl/Windows. Ключ загружается из переменной окружения, Authorization: Bearer. Изменение окружения Visual Studio требует перезапуска IDE. Ключ не сохраняется в конфиг, SQLite или диагностику; server error body не копируется в исключение.

SSE: data-строки, CRLF, comments, multiline data, фрагментированные строки, [DONE]. Tool-call аргументы собираются по index до парсинга JSON. Нужны finish_reason и окончание потока; length/filter — ошибка. Варианты API без /models, Responses-only, мультимодальные входы, provider-specific max_completion_tokens/параметры здесь не обещаются; для них нужен отдельный адаптер.

HTTP 401/403 — авторизация/права, 429 — лимит, 5xx — сервер. Retry только для временных ошибок и до видимого текста, не более config.http.retries (0..3), с ограниченной отменяемой задержкой. Повтор не гарантирует отсутствие оплаты неудачного запроса на стороне провайдера. Native tools исполняются только после завершённого корректного ответа. Ollama ошибку получает оркестратор и может выбрать fallback; отдельных автоматических повторов Ollama в провайдере нет.

Отмена — std::stop_token, проверяется при приёме chunk и через libcurl progress callback. Отмена соединения не обещает немедленного прерывания вычисления на сервере; максимум ожидания ограничен HTTP timeout. Суммарный размер HTTP ответа 16 МиБ, кадра parser 2 МиБ. Нет логирования HTTP request bodies/headers.

Документация протоколов:

- [Ollama chat](https://docs.ollama.com/api/chat)
- [Ollama embeddings](https://docs.ollama.com/api/embed)
- [Chat Completions reference](https://developers.openai.com/api/reference/resources/chat)
Без разрешённого Research-провайдера запрос актуальной информации завершается детерминированным сообщением без вызова LLM. Факты и ссылки для актуального запроса должны поступать от Research-сервиса.
