# BagiEngine - Быстрый запуск

Этот проект включает PyQt6 лаунчер для управления сборкой и открытия IDE.

## Клонирование репозитория

Проект использует git submodules (SDL3). При клонировании используйте:

```bash
git clone --recursive https://github.com/your-repo/BagiEngine.git
```

Если уже склонировали без `--recursive`:
```bash
git submodule update --init --recursive
```

## Первоначальная настройка

### Установка uv

Проект использует [uv](https://docs.astral.sh/uv/) для управления Python-окружением. Установите uv один раз:

**Windows (PowerShell):**
```powershell
powershell -c "irm https://astral.sh/uv/install.ps1 | iex"
```

**macOS/Linux:**
```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

После установки перезапустите терминал. Альтернатива: `pip install uv`.

### Настройка переменных окружения

**Вариант 1: Автоматическое создание (рекомендуется)**

Запустите helper скрипт для создания `.env` файла:

**Windows (PowerShell):**
```powershell
.\create_env.ps1
```

**macOS/Linux:**
```bash
./create_env.sh
```

Скрипт спросит путь к LLVM и автоматически создаст `.env` файл с правильной кодировкой.

---

**Вариант 2: Ручное создание**

```bash
# Скопируйте .env.example в .env
cp .env.example .env

# Отредактируйте .env и укажите путь к LLVM (необходим для Meta Generator)
LIBCLANG_PATH=C:/Program Files/LLVM
```

**Важно:**
- Используйте **прямые слеши** (`/`) в путях, даже на Windows
- Файл должен быть в кодировке **UTF-8 без BOM**
- Если редактируете в Windows Notepad, сохраните как "UTF-8" (не "UTF-8 with BOM")

**Пример для разных платформ:**
- **Windows:** `LIBCLANG_PATH=C:/Program Files/LLVM` или `LIBCLANG_PATH=D:/Tools/LLVM/bin`
- **macOS:** `LIBCLANG_PATH=/usr/local/opt/llvm`
- **Linux:** `LIBCLANG_PATH=/usr/lib/llvm-18`

## Запуск Launcher

### Windows
Дважды кликните на файл `!start.cmd` или выполните в командной строке:
```cmd
!start.cmd
```

**Важно:** Лаунчер запускается в отдельном процессе без консольного окна (используется `pythonw.exe`). Вы можете закрыть командную строку после запуска - лаунчер продолжит работать.

### macOS/Linux
Дважды кликните на файл `!start.command` или выполните в терминале:
```bash
./!start.command
```

**Важно:** Лаунчер запускается в фоновом режиме (с `nohup`). Вы можете закрыть терминал после запуска - лаунчер продолжит работать.

**Примечание:** При первом запуске автоматически создаётся виртуальное окружение (`.venv`) через uv и устанавливаются зависимости.

## Возможности Launcher

- **CMake Configure** - настройка проекта с выбором генератора и компилятора
- **CMake Build** - сборка проекта
- **Open IDE** - открытие проекта в VS Code / Visual Studio / Xcode
- **Pipelines** - цепочки действий (например: Configure -> Build -> Open IDE)
- **Сохранение настроек** - последние настройки сохраняются между запусками

### Поддерживаемые генераторы

| Генератор | Платформы |
|-----------|-----------|
| Ninja | Windows, macOS, Linux |
| Ninja Multi-Config | Windows, macOS, Linux |
| Unix Makefiles | Windows, macOS, Linux |
| Visual Studio 17 2022 | Windows |
| Visual Studio 16 2019 | Windows |
| NMake Makefiles | Windows |
| Xcode | macOS |

### Поддерживаемые компиляторы (Windows)

- **MSVC** - Microsoft Visual C++ (по умолчанию)
- **Clang** - LLVM Clang (ClangCL для VS, или чистый Clang для Ninja)
- **GCC (MinGW)** - GNU Compiler Collection

## Альтернативный способ (все платформы)

Если у вас `code` добавлен в PATH:

**Windows:**
```cmd
code CI\BagiEngine.code-workspace
```

**macOS/Linux:**
```bash
code CI/BagiEngine.code-workspace
```

На macOS команда `code` появляется после «Shell Command: Install 'code' command in PATH» в палитре команд VS Code.

## Структура проекта

```
BagiEngine/
├── src/
│   ├── Application/     # Main entry point
│   ├── Widgets/         # Game widgets
│   └── Modules/
│       ├── BECore/      # Core engine module
│       │   └── Reflection/  # Serialization system
│       ├── Math/        # Math utilities
│       ├── Events/      # Event system
│       ├── TaskSystem/  # Task/job system
│       ├── SDL/         # SDL3 integration
│       ├── EABase/      # EASTL (vendored)
│       ├── EnTT/        # ECS framework (vendored)
│       ├── pugixml/     # XML parser (vendored)
│       ├── imgui/       # ImGui (vendored)
│       ├── RmlUi/       # RmlUi core (vendored, built with ENGINE_UI_RMLUI=ON)
│       └── fmt/         # Formatting (vendored)
├── external/
│   └── SDL/             # SDL3 (git submodule)
├── config/              # XML configuration files
├── build/Generated/     # Auto-generated reflection code
├── .claude/             # Rules, skills и планы для Claude Code
└── CI/
    ├── setup_venv.bat       # Настройка .venv через uv (Windows CMD)
    ├── setup_venv.ps1       # Настройка .venv через uv (PowerShell)
    ├── setup_venv.sh        # Настройка .venv через uv (macOS/Linux)
    ├── setup_vs.bat         # Генерация проекта VS
    ├── BagiEngine.code-workspace  # Workspace файл
    ├── launcher/            # PyQt6 приложение
    └── meta_generator/      # Генератор кода рефлексии
```

## Рефлексия и сериализация

Используйте `BE_CLASS` и `BE_REFLECT_FIELD` для автоматической сериализации.
Примитивы → XML атрибуты, классы → вложенные элементы.
Подробнее: `.claude/rules/reflection-module.md`

## Ручная сборка (без Launcher)

### CMake presets (рекомендуется)

Конфигурации описаны в `CMakePresets.json`; те же пресеты использует расширение CMake Tools в VS Code.
Каждый пресет собирается в свой каталог `build/<имя-пресета>/`.
```bash
cmake --list-presets=build            # пресеты, доступные на этой ОС
cmake --preset macos-debug            # configure (заодно генерирует reflection-код)
cmake --build --preset macos-debug    # сборка игр (BagiChess, BagiMatch3)
```

| ОС | Пресеты |
|----|---------|
| macOS | `macos-debug`, `macos-release` (Clang + Ninja) |
| Windows | `windows-msvc-{debug,release}`, `windows-clang-{debug,release}` (Ninja), `windows-vs2022-{debug,release}`, `windows-vs2022-clangcl-{debug,release}` (Visual Studio 2022) |

Настройки конкретной машины (например `VULKAN_SDK`, ccache) кладите в `CMakeUserPresets.json` — он в `.gitignore`.
Исполняемые файлы всегда лежат в `build/{Compiler}{Platform}/` (`BagiChess`, `BagiMatch3`), поэтому Debug и Release (Ninja) затирают друг друга.
Reflection-код генерируется на этапе configure: после правок `BE_CLASS` / `BE_REFLECT_FIELD` повторите `cmake --preset ...`.

### CMake с Ninja (без пресетов, старый плоский `build/`)
```bash
cmake -B build -G Ninja
cmake --build build
```

### CMake с Visual Studio (без пресетов)
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

### CMake с Clang (без пресетов)
```bash
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

## Настройка проекта в IDE

После открытия workspace:
1. IDE автоматически загрузит все настройки
2. Рекомендуемые расширения будут предложены к установке
3. IntelliSense будет настроен для C++23
4. CMake Tools работает через пресеты (`cmake.useCMakePresets: always`): выберите пресет командами
   **CMake: Select Configure Preset** и **CMake: Select Build Preset** (или в строке состояния), затем **CMake: Configure** и F7 (сборка).
   F5 запускает отладку выбранной игры (цель выбирается командой **CMake: Set Launch/Debug Target**, путь берётся из `${command:cmake.launchTargetPath}`)

### Горячие клавиши:
- **Ctrl/Cmd + Shift + B** - собрать проект
- **F5** - запустить отладку

---

Приятной разработки!
