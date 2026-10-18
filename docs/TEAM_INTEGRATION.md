# Разделение работы команды

Модуль Core владеет src провайдерами, transport, routing, projects, tools, memory, orchestrator и публичными core/model types. Изменения контрактов сначала согласуются со всеми.

Модуль Knowledge создаёт отдельную цель sayra-knowledge в своём каталоге. Реализует IKnowledgeRetriever/IResearchService, использует IEmbeddingProvider. Не изменяет HTTP внутри Core и не пересылает приватные данные без флагов. Qdrant/Neo4j/Web Research отсутствуют намеренно. Подключение — shared_ptr в composition root.

Модуль Desktop создаёт sayra-desktop отдельно, зависит только от sayra-core и согласованных integration/events типов. Audio/STT/Qt UI принадлежат Desktop. События копируются в очередь GUI; методы Qt из рабочего callback не вызываются. Передаёт TranscriptSegment через pipeline, запросы — через run на своём scheduler. Не разбирает NDJSON/SSE.

Контракты version 1 в include/sayra/integration, events/events.hpp и core/types.hpp. Не включайте заголовки curl/sqlite в модуль Desktop/Knowledge. Публичный Json применяется только для схем/аргументов инструментов и конфигурации; текст и источники интеграции типизированы.

Composition root сейчас apps/sayra-cli/main.cpp. Desktop заменит CLI-цикл своим вводом и очередью событий, не копируя оркестратор. Для своих источников добавляйте новые .cpp под собственным модулем; CMake корня меняйте малыми согласованными правками. База истории, конфиг и сборочные каталоги не входят в Git.