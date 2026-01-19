# BagiEngine Launcher

PyQt6-based launcher for BagiEngine project with build configuration management and IDE integration.

## Features

- **Multi-configuration builds** - manage multiple compiler/generator combinations
- **Pipeline system** - create custom build workflows
- **Real-time output** - stream CMake output during configuration and build
- **IDE integration** - open project in Cursor, Visual Studio, or Xcode
- **Environment configuration** - load settings from `.env` file

## Environment Configuration

The launcher loads environment variables from `.env` file in the project root.

### Setup

1. Copy `.env.example` to `.env`:
   ```bash
   cp .env.example .env
   ```

2. Edit `.env` and set your LLVM path:
   ```
   LIBCLANG_PATH=C:/Program Files/LLVM
   ```

**Important:**
- Use **forward slashes** (`/`) in paths, even on Windows
- Save file as **UTF-8 without BOM** encoding
- In Windows Notepad, save as "UTF-8" (not "UTF-8 with BOM")
- If using PowerShell to create `.env`, use UTF8 without BOM encoding

### Why `.env`?

- **No system pollution** - keeps LLVM path local to the project
- **Version control friendly** - `.env` is gitignored, `.env.example` is tracked
- **Easy team setup** - team members just copy and customize `.env.example`
- **Platform-independent** - same setup process on Windows/macOS/Linux

### LIBCLANG_PATH

This variable is required for Meta Generator (reflection code generation). The launcher will:
- Load `LIBCLANG_PATH` from `.env` if it exists
- Fall back to system environment variable if `.env` is missing
- Display the source of LIBCLANG_PATH in the status bar

## Running the Launcher

### From Project Root (Recommended)

**Windows:**
```cmd
!start.cmd
```

The launcher will start in a separate process without a console window (using `pythonw.exe`). You can close the command prompt after the launcher starts - it will continue running independently.

**macOS/Linux:**
```bash
./!start.command
```

The launcher will start in the background (using `nohup`). You can close the terminal after the launcher starts - it will continue running independently.

### Manually (with Console Window)

If you want to see debug output or run from an existing terminal:

**Windows:**
```cmd
.venv\Scripts\python.exe -m CI.launcher.main
```

**macOS/Linux:**
```bash
.venv/bin/python -m CI.launcher.main
```

### First-Time Setup

```bash
# Create virtual environment (first time only)
python -m venv .venv

# Activate virtual environment
source .venv/bin/activate  # macOS/Linux
.venv\Scripts\activate     # Windows

# Install dependencies
pip install -r requirements.txt

# Run launcher
python -m CI.launcher.main
```

## Build Configurations

The launcher supports multiple build configurations with different:
- **Compilers:** MSVC, Clang, GCC (MinGW)
- **Generators:** Ninja, Visual Studio, Xcode, Makefiles
- **Build types:** Debug, Release, RelWithDebInfo, MinSizeRel

### Configuration Management

1. Open **Settings** (button in Quick Actions)
2. Add/edit/remove configurations
3. Set one as **Active** (used for Quick Actions)
4. Enable multiple for **Batch Builds** (checkbox)

## Pipeline Builder

Create custom build workflows by checking actions in execution order:

- **Close Visual Studio** - terminate VS instances before clean
- **Clean Build** - remove build artifacts (per-config)
- **CMake Configure** - configure project (per-config)
- **CMake Build** - compile project (per-config)
- **Open Cursor/VS/Xcode** - launch IDE

### Example Pipelines

**Full Clean Build:**
1. Close Visual Studio
2. Clean Build (all configs)
3. CMake Configure (all configs)
4. CMake Build (all configs)
5. Open Cursor

**Quick Configure:**
1. CMake Configure (active config)
2. Open Cursor

## Quick Actions

Single-click shortcuts for common tasks:
- **CMake Configure** - configure with active configuration
- **CMake Build** - build with active configuration
- **Clean** - remove build artifacts (confirmation required)
- **Open Cursor** - launch Cursor IDE with workspace
- **Open Visual Studio** - open .sln file (if using VS generator)
- **Meta Generator** - configure LLVM path and generate reflection code
- **Settings** - manage build configurations

## Meta Generator Integration

The **Meta Generator** button launches the reflection code generator GUI:
- Automatically loads `LIBCLANG_PATH` from `.env` or system
- Generates reflection metadata for enums and classes
- Updates generated headers in `src/` directories

## Status Bar

Displays current environment status:
- **LIBCLANG_PATH:** Shows path and source (`.env` or system)
- Green = configured, Red = not set

## Dependencies

- **PyQt6** >= 6.5.0 - GUI framework
- **python-dotenv** >= 1.0.0 - `.env` file loading
- **pytest** >= 7.0.0 - testing (optional)

## Configuration Files

- **launcher_config.json** - user-specific settings (gitignored)
- **meta_generator_settings.json** - Meta Generator settings (gitignored)
- **.env** - environment variables (gitignored)
- **.env.example** - template for `.env` (tracked in git)

## Project Structure

```
CI/launcher/
├── __init__.py
├── main.py              # Entry point, loads .env
├── main_window.py       # Main UI, shows LIBCLANG_PATH status
├── config.py            # Configuration management
├── actions.py           # Build actions (uses os.environ)
└── pipeline.py          # Pipeline execution
```

## Troubleshooting

### LIBCLANG_PATH not found

Create `.env` file in project root:
```bash
LIBCLANG_PATH=C:/Program Files/LLVM
```

Restart the launcher.

### Virtual environment issues

Delete `.venv` folder and re-run `!start.cmd` or `!start.command`.

### CMake configure fails

- **MSVC:** Install Visual Studio with C++ tools
- **Clang:** Install LLVM and add to PATH
- **GCC:** Install MinGW-w64 and add to PATH

### Visual Studio not closing

The launcher uses `taskkill /F /IM devenv.exe` to forcefully terminate VS instances. This is safe and allows clean rebuilds.

## Development

### Running tests

```bash
pytest CI/launcher/
```

### Code style

- Follow PEP 8
- Use type hints
- Document public APIs

## License

Part of BagiEngine project.
