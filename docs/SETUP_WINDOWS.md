# Настройка Windows

Откройте Visual Studio Installer, проверьте C++ Desktop workload, MSVC v145, Windows SDK и CMake.

В Developer PowerShell for VS 2026 из корня Sayra:

```powershell
.\tools\vcpkg\bootstrap-vcpkg.bat -disableMetrics
cmake --preset vs2026-x64
cmake --build --preset debug
Copy-Item config\sayra.example.json sayra.json
.\build\core\Debug\sayra-cli.exe .\sayra.json
```

Если vcpkg нет: `git clone https://github.com/microsoft/vcpkg.git tools/vcpkg`. CMake toolchain использует локальный tools/vcpkg; при другом расположении передайте -DCMAKE_TOOLCHAIN_FILE=полный_путь. Зависимости восстанавливаются из vcpkg.json: curl[ssl] (Windows Schannel), nlohmann-json, sqlite3. AI-модели не скачиваются ни CMake, ни Sayra.

В IDE откройте Sayra.slnx, выберите Debug | x64, назначьте sayra-cli запускаемым проектом и нажмите «Локальный отладчик Windows». При первой сборке vcpkg автоматически загрузит свой исполняемый файл и зависимости. Core соберётся по ProjectReference. В решении также видны sayra-core-tests (запускаются вручную) и sayra-model-tests (прежнее меню). Рабочий каталог отладчика — корень Sayra, поэтому sayra.json и data находятся там. Native-вывод: out/vs/x64/Debug или Release. Альтернатива — «Открыть папку», preset vs2026-x64 и CMake-цель sayra-cli. В Windows Terminal используйте UTF-8 и кириллический шрифт.

Откройте приложение Ollama. /models покажет только реально обнаруженные модели. На GTX 1080 Ti используйте умеренный контекст (пример — 8192, при нехватке памяти 4096) и keep_alive=5m. Registry учитывает loaded, но не считает размер файла расходом VRAM. Ошибка нехватки памяти остаётся ошибкой провайдера; модели автоматически не выгружаются и не скачиваются.

При C1083 сначала проверьте журнал конфигурации/установки vcpkg и выбранную CMake-цель. NOMINMAX задан на уровне target, чтобы Windows min/max не ломали std::min и numeric_limits::max. При ошибках cloud убедитесь, что ключ присутствует в окружении именно процесса IDE, URL с /v1, профиль содержит chat и реальную цену, enabled=true. Подписка ChatGPT не предоставляет автоматически API key.