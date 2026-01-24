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

**Примечание:** При первом запуске автоматически создаётся виртуальное окружение Python и устанавливаются зависимости.

## Возможности Launcher

- **CMake Configure** - настройка проекта с выбором генератора и компилятора
- **CMake Build** - сборка проекта
- **Open IDE** - открытие проекта в Cursor / Visual Studio / Xcode
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

Если у вас `cursor` добавлен в PATH:

**Windows:**
```cmd
cursor CI\BagiEngine.code-workspace
```

**macOS/Linux:**
```bash
cursor CI/BagiEngine.code-workspace
```

## Структура проекта

```
BagiEngine/
├── src/
│   ├── Application/     # Main entry point
│   ├── Widgets/         # Game widgets
│   └── Modules/
│       ├── BECore/      # Core engine module
│       ├── Math/        # Math utilities
│       ├── Events/      # Event system
│       ├── TaskSystem/  # Task/job system
│       ├── SDL/         # SDL3 integration
│       ├── EABase/      # EASTL (vendored)
│       ├── EnTT/        # ECS framework (vendored)
│       ├── pugixml/     # XML parser (vendored)
│       ├── imgui/       # ImGui (vendored)
│       └── fmt/         # Formatting (vendored)
├── external/
│   └── SDL/             # SDL3 (git submodule)
├── config/              # XML configuration files
├── .cursorignore        # Игнорирование vendored для AI
└── CI/
    ├── setup_venv.bat       # Настройка venv (Windows CMD)
    ├── setup_venv.ps1       # Настройка venv (PowerShell)
    ├── setup_venv.sh        # Настройка venv (macOS/Linux)
    ├── setup_vs.bat         # Генерация проекта VS
    ├── BagiEngine.code-workspace  # Workspace файл
    └── launcher/            # PyQt6 приложение
```

## Ручная сборка (без Launcher)

### CMake с Ninja
```bash
cmake -B build -G Ninja
cmake --build build
```

### CMake с Visual Studio
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

### CMake с Clang
```bash
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

## Настройка проекта в IDE

После открытия workspace:
1. IDE автоматически загрузит все настройки
2. Рекомендуемые расширения будут предложены к установке
3. IntelliSense будет настроен для C++23

### Горячие клавиши:
- **Ctrl/Cmd + Shift + B** - собрать проект
- **F5** - запустить отладку

---

Приятной разработки!
