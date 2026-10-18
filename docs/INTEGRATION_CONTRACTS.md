# Контракты интеграции — версия 1

`contract_version == 1`, заголовки include/sayra. Модули собираются одним MSVC toolset/C++ runtime; бинарная ABI-совместимость между разными компиляторами не обещается. Добавлять необязательные поля можно совместимо на уровне исходников, переименование полей и изменение сигнатур требует новой версии контрактов и согласования команды. Ошибки передаются исключениями: ProviderError содержит HTTP status/retryable/cancelled, остальные std::exception — ошибки валидации/хранилища. Ключи и полный удалённый error body в исключения не включаются.

## Developer 2 — знания, исследования, embeddings

Реализует IKnowledgeRetriever и IResearchService в своём модуле. Объекты создаёт composition root приложения, передаёт ядру shared_ptr через set_knowledge/set_research; ядро сохраняет совместное владение. Не вызывайте setters во время run.

- `IKnowledgeRetriever::retrieve(KnowledgeQuery, stop_token) → KnowledgeResult`. Запрос: text, project_id, max_context_bytes, allow_cloud. Результат: ограниченный context, sources, gaps. Не индексируйте весь проект в этом методе без согласования. Запрос ядра сейчас всегда allow_cloud=false.
- `IResearchService::research(ResearchRequest, stop_token) → ResearchResult`. Запрос: text, freshness, context, max_sources, allow_internet. Результат: claims, sources, gaps, verification. Без согласия на сеть не делайте внешних запросов. В Stage 1 вызов разрешён только при AgentRequest.allow_internet=true, без sensitive/project/transcript-контекста.
- `IEmbeddingProvider::embed(EmbeddingRequest, stop_token) → EmbeddingResult`. OllamaEmbeddingProvider предоставлен Core. Тексты и точная модель — на входе; vectors/model/seconds — на выходе. Общая размерность не фиксируется для всех моделей; BGE-M3 отдельно проверяется на 1024.
- `SourceReference`: id, title, uri, published_at, evidence, verification. Неизвестную дату оставляйте пустой; не придумывайте URL/источники. Verified присваивает только исследовательский модуль после проверки evidence, не LLM. У локального файла Verified означает подтверждение чтения, а не истинности любых утверждений в нём.

Вызовы блокирующие и выполняются на рабочем потоке. Проверяйте stop_token до и во время I/O, ограничения результата и разрешения. При отмене бросайте ProviderError с cancelled=true. UI не разбирает HTTP ни одного провайдера. SourceAdded и AgentResponse содержат готовые ссылки.

## Developer 3 — Desktop/voice/overlay

Реализует ITranscriptSource и IOverlayEventSink. Хост создаёт EventBus, подписчика, pipeline и orchestrator. Subscribe принимает shared_ptr, возвращает числовой id; сохраняйте сильную ссылку на sink. Unsubscribe не дожидается уже начатого callback. При закрытии UI используйте weak/lifetime guard и drain очереди после остановки worker.

`ITranscriptSource::start(callback)` передаёт TranscriptSegment: session_id, стабильный segment_id, timestamp UTC, text, source, is_final, optional speaker. Один segment_id используется при уточнении partial → final. Не приписывайте неизвестного speaker. stop должен дождаться завершения callbacks. Захват аудио создаёт Desktop, Core его не включает.

`RealtimeSession`: id, project_id, mode, allow_cloud, proactive_consent. По умолчанию OnDemand. `accept(segment)` быстро накапливает текст, не вызывает LLM. Частичные сегменты обновляются в памяти и помечаются предварительными событиями; модель их не получает. Окончательные сегменты дедуплицируются и ограничиваются байтовым окном. OnDemand/ContextAware требуют request_help. Proactive требует явного согласия и соблюдает interval_seconds.

При получении optional AgentRequest от pipeline хост:

1. Получает pipeline.generation_token() — новый prepare отменяет токен прошлого запроса.
2. Отменяет/дожидается старого worker, не вызывает один orchestrator параллельно.
3. Запускает run(request, conversation, token) в worker; при наличии отдельной UI-отмены объединяет два сигнала через stop_callback и stop_source.
4. Фильтрует устаревшие события по request_id/session_id.

`close()` очищает контекст и отменяет generation_token. Не закрывайте pipeline до выполнения запроса, использующего его токен. CLI-демонстрация копирует запрос в отдельный worker с собственным токеном: это разовая OnDemand-демонстрация.

`SayraOrchestrator::run(AgentRequest, Conversation&, stop_token) → AgentResponse` блокирует только worker. Host заполняет conversation_id/project_id, при необходимости forced_model, mode и отдельные разрешения cloud/project/transcript/internet. Политика конфига дополнительно ограничивает запрос: запрос модели или Desktop не может расширить глобальные права.

`AgentResponse`: request_id, text, model, provider, project_id, sources, verification, metrics. События CoreEvent: RequestStarted, IntentDetected, ModelSelected, ThinkingStatus (статус без внутреннего reasoning), ToolStarted/Completed, TextDelta, SourceAdded, AssistCardReady, RequestCompleted/Failed/Cancelled.

`AssistCard` содержит короткий ответ, подробный текст, task_type, модель/провайдера, проект, источники, verification, timestamp, is_final. Предварительная карточка транскрипции: is_final=false и идентификатор `transcript-...`; финальный ответ: is_final=true и request_id генерации. В Stage 1 нет промежуточных сгенерированных карточек — текст приходит TextDelta.

Callback EventBus исполняется на публикующем потоке. **Нельзя менять QWidget/QML из callback**. Адаптер Desktop копирует событие в потокобезопасную очередь и доставляет в GUI event loop; SourceReference/AssistCard уже готовы к отображению. Короткие callbacks не должны блокировать генерацию. Сырые API JSON наружу не передаются, кроме schema-аргументов инструментов внутри Core.

Транскрипция в облако требует одновременно session.allow_cloud, AgentRequest.allow_transcript_cloud и config.cloud.allow_transcripts плюс cloud.enabled и request.allow_cloud. Для истории транскрипции сохраняется privacy-флаг. В /live-demo session.allow_cloud=false.
Контекст IKnowledgeRetriever в Stage 1 считается локальным: DocumentQuestion с подключённым knowledge помечается private_data и не маршрутизируется в cloud. Изменение этого поведения требует отдельного контракта разрешения экспорта retrieved data.
