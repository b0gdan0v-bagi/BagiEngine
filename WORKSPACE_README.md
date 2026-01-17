# BagiEngine - Быстрый запуск

Этот проект включает PyQt6 лаунчер для управления сборкой и открытия IDE.

## Запуск Launcher

### Windows
Дважды кликните на файл `start.cmd` или выполните в командной строке:
```cmd
start.cmd
```

### macOS
Дважды кликните на файл `start.command` или выполните в терминале:
```bash
./start.command
```

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

## Структура CI/

```
CI/
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
