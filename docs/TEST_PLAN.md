# План проверок

**18 октября 2026: пользователь собрал sayra-core-tests; агент с отдельного разрешения выполнил один запуск — 28/28 PASS, exit code 0.** Остальные ручные проверки этим запуском не подтверждены. Код tests/core_tests.cpp содержит 28 автономных сценариев. Сетевой транспорт FakeHttp и модель FakeProvider используются только в тестовой цели. Подлинные серверы не вызываются; математические fixture-векторы не являются измерениями BGE-M3.

Покрытие:

1. Обычный диалог, русский регистр.
2. Coding + ComplexReasoning.
3. ProjectAnalysis при выбранном проекте.
4. Актуальный веб-запрос.
5. Coding-профиль и scoring.
6. Общий fallback без специалиста.
7. Недоступный Ollama и diagnostics.
8. Нет cloud key — нет HTTP.
9. Cloud выключен — нет HTTP.
10. Политика cloud/project/cost.
11. NDJSON по одному байту, UTF-8 и CRLF.
12. SSE arbitrary chunks/multiline/comment/[DONE].
13. Поток Ollama и отделение thinking.
14. SSE tool calls с фрагментами JSON-аргументов.
15. HTTP401 без retry.
16. Разрыв потока без done.
17. Отмена во время потока.
18. Конечный предел tool loop.
19. Path traversal, секреты, ADS.
20. Бинарный файл.
21. SQLite восстановление/удаление и transcript privacy.
22. Неизвестная manual model.
23. Нулевые/неконечные/пустые векторы и устойчивость к большим числам.
24. Математические свойства cosine, порядок тестовых векторов.
25. Embedding parser BGE-M3 1024 и ошибочная размерность.
26. Partial/final transcript, дедупликация, close.
27. Overlay request IDs, финальная карточка и unverified web.
28. Валидация tool arguments и общий бюджет.

Ручной запуск пользователем:

```powershell
cmake --preset vs2026-x64
cmake --build --preset debug
ctest --preset debug
```

После unit-тестов — ручные live-проверки: /models; Привет; coding-запрос и /why-model; второй профиль для другой задачи; forced model; отключённый Ollama; отдельно настроенный cloud; проект с явно указанным .cpp; /new и история после перезапуска; /embed-test; /live-demo; новости без Research. Результаты записывать с точными моделями, HTTP error при наличии и временем. Никаких старых времён от домашнего задания не использовать как результат Core.

Отдельный ручной security checklist: symlink/junction вне проекта, hard-link, смена пути во время чтения, отказ доступа, длинные пути и кириллица; стоп до/после первого delta; 403/429/5xx и ограниченные повторы; слишком большой tool output; переполнение контекста после tool call; большой реестр моделей; expired subscribers и конкурентное закрытие GUI. Эти проверки не объявлены пройденными и не полностью автоматизированы.