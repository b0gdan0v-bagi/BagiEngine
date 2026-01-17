"""Action definitions for the launcher."""

import subprocess
import sys
import os
import time
from pathlib import Path
from dataclasses import dataclass
from typing import Callable, Optional
from enum import Enum


class Platform(Enum):
    WINDOWS = "windows"
    MACOS = "macos"
    LINUX = "linux"


def get_current_platform() -> Platform:
    """Detect current platform."""
    if sys.platform == "win32":
        return Platform.WINDOWS
    elif sys.platform == "darwin":
        return Platform.MACOS
    else:
        return Platform.LINUX


def find_visual_studio() -> Optional[Path]:
    """Find Visual Studio installation directory using vswhere.
    
    Returns:
        Path to Visual Studio installation, or None if not found.
    """
    if get_current_platform() != Platform.WINDOWS:
        return None
    
    # vswhere is installed with Visual Studio in this location
    vswhere_paths = [
        Path(os.environ.get("PROGRAMFILES(X86)", "")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe",
        Path(os.environ.get("PROGRAMFILES", "")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe",
    ]
    
    vswhere = None
    for path in vswhere_paths:
        if path.exists():
            vswhere = path
            break
    
    if not vswhere:
        return None
    
    try:
        result = subprocess.run(
            [str(vswhere), "-latest", "-property", "installationPath", "-requires", 
             "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"],
            capture_output=True,
            text=True,
            timeout=10
        )
        if result.returncode == 0 and result.stdout.strip():
            return Path(result.stdout.strip())
    except (subprocess.TimeoutExpired, Exception):
        pass
    
    return None


def get_msvc_environment(arch: str = "x64") -> Optional[dict[str, str]]:
    """Get environment variables for MSVC compiler.
    
    Runs vcvarsall.bat and captures the resulting environment.
    
    Args:
        arch: Target architecture (x64, x86, arm64, etc.)
        
    Returns:
        Dictionary of environment variables, or None if setup failed.
    """
    vs_path = find_visual_studio()
    if not vs_path:
        return None
    
    vcvarsall = vs_path / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
    if not vcvarsall.exists():
        return None
    
    try:
        # Run vcvarsall.bat and print environment
        # Use 'set' command to dump all environment variables after setup
        cmd = f'"{vcvarsall}" {arch} && set'
        result = subprocess.run(
            cmd,
            shell=True,
            capture_output=True,
            text=True,
            timeout=30
        )
        
        if result.returncode != 0:
            return None
        
        # Parse the environment variables from output
        env = os.environ.copy()
        for line in result.stdout.splitlines():
            if '=' in line:
                key, _, value = line.partition('=')
                env[key] = value
        
        return env
        
    except (subprocess.TimeoutExpired, Exception):
        return None


def get_build_dir_name(compiler: str, platform: Optional[Platform] = None, with_solution_suffix: bool = True) -> str:
    """Get build directory name based on compiler and platform.
    
    Args:
        compiler: Compiler name (MSVC, Clang, GCC)
        platform: Target platform (defaults to current platform)
        with_solution_suffix: If True, adds "Solution" suffix for CMake build dir
        
    Returns:
        Build directory name like "MSVCWindowsSolution" or "MSVCWindows"
    """
    if platform is None:
        platform = get_current_platform()
    
    platform_suffix = {
        Platform.WINDOWS: "Windows",
        Platform.MACOS: "macOS",
        Platform.LINUX: "Linux"
    }
    
    # Normalize compiler name (remove spaces and special chars)
    compiler_name = compiler.replace(" ", "").replace("(", "").replace(")", "")
    # Handle GCC (MinGW) -> GCC
    if "MinGW" in compiler:
        compiler_name = "GCC"
    
    base_name = f"{compiler_name}{platform_suffix[platform]}"
    if with_solution_suffix:
        return f"{base_name}Solution"
    return base_name


@dataclass
class ActionResult:
    """Result of an action execution."""
    success: bool
    output: str
    error: str = ""


@dataclass
class Action:
    """Represents a single action that can be executed."""
    id: str
    name: str
    description: str
    platforms: list[Platform]
    
    def is_available(self) -> bool:
        """Check if action is available on current platform."""
        return get_current_platform() in self.platforms


class ActionExecutor:
    """Executes actions with project context."""
    
    def __init__(self, project_root: Path, compiler: str = "MSVC", build_type: str = "Debug"):
        self.project_root = project_root
        self._compiler = compiler
        self._build_type = build_type
        self.workspace_file = project_root / "CI" / "BagiEngine.code-workspace"
    
    @property
    def build_dir(self) -> Path:
        """Get build directory based on current compiler and build type.
        
        Returns path like: build/MSVCDebugWindowsSolution
        """
        build_dir_name = get_build_dir_name(self._compiler, with_solution_suffix=True)
        return self.project_root / "build" / build_dir_name
    
    def set_compiler(self, compiler: str) -> None:
        """Set the current compiler for build directory resolution."""
        self._compiler = compiler
    
    def set_build_type(self, build_type: str) -> None:
        """Set the current build type for build directory resolution."""
        self._build_type = build_type
    
    def _run_streaming_command(
        self, 
        cmd: list[str], 
        env: Optional[dict[str, str]], 
        on_output: Optional[Callable[[str], None]],
        timeout: int = 300
    ) -> ActionResult:
        """Run a command with real-time output streaming.
        
        Args:
            cmd: Command and arguments to run
            env: Environment variables
            on_output: Callback for each line of output
            timeout: Timeout in seconds
            
        Returns:
            ActionResult with combined output
        """
        import threading
        
        try:
            # Use Popen for real-time output streaming
            process = subprocess.Popen(
                cmd,
                cwd=self.project_root,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,  # Merge stderr into stdout
                text=True,
                bufsize=1,  # Line buffered
                env=env,
                # On Windows, prevent console window from appearing
                creationflags=subprocess.CREATE_NO_WINDOW if get_current_platform() == Platform.WINDOWS else 0
            )
            
            output_lines: list[str] = []
            
            def read_output():
                """Read output from process in a separate thread."""
                try:
                    for line in iter(process.stdout.readline, ''):
                        if line:
                            line = line.rstrip('\r\n')
                            output_lines.append(line)
                            if on_output:
                                on_output(line)
                except Exception:
                    pass
                finally:
                    if process.stdout:
                        process.stdout.close()
            
            # Start output reading thread
            output_thread = threading.Thread(target=read_output, daemon=True)
            output_thread.start()
            
            # Wait for process to complete with timeout
            try:
                process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                output_thread.join(timeout=1)
                return ActionResult(False, "\n".join(output_lines), "Command timed out")
            
            # Wait for output thread to finish
            output_thread.join(timeout=5)
            
            return ActionResult(
                success=process.returncode == 0,
                output="\n".join(output_lines),
                error="" if process.returncode == 0 else f"Command failed with exit code {process.returncode}"
            )
            
        except Exception as e:
            return ActionResult(False, "", str(e))
    
    def clean_build(self, compiler: str = "MSVC", build_type: str = "Debug") -> ActionResult:
        """Clean build directory using git clean (permanent deletion, no recycle bin).
        
        Uses 'git clean -fdx' to forcefully remove all untracked files and directories
        in the build folder. This is a permanent deletion.
        """
        self.set_compiler(compiler)
        self.set_build_type(build_type)
        
        if not self.build_dir.exists():
            return ActionResult(True, f"Build directory {self.build_dir} does not exist. Nothing to clean.")
        
        # Use git clean -fdx in the build directory
        # -f: force
        # -d: remove directories
        # -x: remove ignored files too
        cmd = ["git", "clean", "-fdx", str(self.build_dir)]
        
        try:
            result = subprocess.run(
                cmd,
                cwd=self.project_root,
                capture_output=True,
                text=True,
                timeout=120
            )
            
            build_dir_name = get_build_dir_name(compiler, with_solution_suffix=True)
            if result.returncode == 0:
                cleaned_files = result.stdout.strip()
                if cleaned_files:
                    return ActionResult(
                        True, 
                        f"Cleaned build directory: build/{build_dir_name}\n{cleaned_files}"
                    )
                else:
                    return ActionResult(True, f"Build directory already clean: build/{build_dir_name}")
            else:
                return ActionResult(False, result.stdout, result.stderr)
                
        except subprocess.TimeoutExpired:
            return ActionResult(False, "", "Clean operation timed out")
        except Exception as e:
            return ActionResult(False, "", str(e))
    
    def clean_all_builds(self) -> ActionResult:
        """Clean all build directories using git clean (permanent deletion).
        
        Removes the entire build/ directory.
        """
        build_root = self.project_root / "build"
        
        if not build_root.exists():
            return ActionResult(True, "Build directory does not exist. Nothing to clean.")
        
        cmd = ["git", "clean", "-fdx", str(build_root)]
        
        try:
            result = subprocess.run(
                cmd,
                cwd=self.project_root,
                capture_output=True,
                text=True,
                timeout=120
            )
            
            if result.returncode == 0:
                cleaned_files = result.stdout.strip()
                if cleaned_files:
                    return ActionResult(True, f"Cleaned all build directories\n{cleaned_files}")
                else:
                    return ActionResult(True, "Build directories already clean")
            else:
                return ActionResult(False, result.stdout, result.stderr)
                
        except subprocess.TimeoutExpired:
            return ActionResult(False, "", "Clean operation timed out")
        except Exception as e:
            return ActionResult(False, "", str(e))
    
    def cmake_configure(self, build_type: str = "Debug", generator: str = "Ninja", 
                        compiler: str = "MSVC",
                        on_output: Optional[Callable[[str], None]] = None) -> ActionResult:
        """Run CMake configuration with real-time output streaming."""
        self.set_compiler(compiler)
        self.set_build_type(build_type)
        self.build_dir.mkdir(parents=True, exist_ok=True)
        
        cmd = ["cmake", "-B", str(self.build_dir), "-S", str(self.project_root)]
        
        if generator:
            cmd.extend(["-G", generator])
        
        cmd.append(f"-DCMAKE_BUILD_TYPE={build_type}")
        
        # Add Visual Studio architecture for VS generator
        if "Visual Studio" in generator:
            cmd.extend(["-A", "x64"])
        
        # Prepare environment for subprocess
        env = None
        
        # Set compiler toolchain based on selection (Windows only)
        if get_current_platform() == Platform.WINDOWS:
            if compiler == "Clang":
                # Use Clang with clang-cl for MSVC compatibility or pure Clang
                if "Visual Studio" in generator:
                    cmd.extend(["-T", "ClangCL"])
                else:
                    cmd.extend(["-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"])
            elif compiler == "GCC (MinGW)":
                cmd.extend(["-DCMAKE_C_COMPILER=gcc", "-DCMAKE_CXX_COMPILER=g++"])
            elif compiler == "MSVC" and "Visual Studio" not in generator:
                # For MSVC with non-VS generators (like Ninja), we need to set up
                # the Visual Studio environment so CMake can find cl.exe
                env = get_msvc_environment("x64")
                if env is None:
                    return ActionResult(
                        False, 
                        "", 
                        "Could not find Visual Studio installation.\n"
                        "Please ensure Visual Studio is installed with C++ development tools,\n"
                        "or run the launcher from a Visual Studio Developer Command Prompt."
                    )
        
        return self._run_streaming_command(cmd, env, on_output, timeout=300)
    
    def cmake_build(self, build_type: str = "Debug", compiler: str = "MSVC",
                    on_output: Optional[Callable[[str], None]] = None) -> ActionResult:
        """Run CMake build with parallel compilation and real-time output streaming."""
        self.set_compiler(compiler)
        self.set_build_type(build_type)
        if not self.build_dir.exists():
            return ActionResult(False, "", f"Build directory {self.build_dir} does not exist. Run CMake Configure first.")
        
        # Get number of CPU cores for parallel build
        cpu_count = os.cpu_count() or 4
        
        cmd = [
            "cmake", "--build", str(self.build_dir),
            "--config", build_type,
            "--parallel", str(cpu_count),  # Multi-threaded build
        ]
        
        # For MSVC with Ninja, we need the VS environment for the build step too
        env = None
        if get_current_platform() == Platform.WINDOWS and compiler == "MSVC":
            # Check if we're using a non-VS generator by looking for Ninja files
            ninja_build = self.build_dir / "build.ninja"
            if ninja_build.exists():
                env = get_msvc_environment("x64")
                if env is None:
                    return ActionResult(
                        False, 
                        "", 
                        "Could not find Visual Studio installation.\n"
                        "Please ensure Visual Studio is installed with C++ development tools."
                    )
            else:
                # For Visual Studio generator
                env = os.environ.copy()
        else:
            env = os.environ.copy()
        
        return self._run_streaming_command(cmd, env, on_output, timeout=600)
    
    def open_cursor(self) -> ActionResult:
        """Open project in Cursor IDE with workspace file.
        
        The workspace file (CI/BagiEngine.code-workspace) points to the project root,
        where .cursorrules and .cursor/rules/ are located. Cursor will automatically
        load these rules when opening the workspace.
        """
        platform = get_current_platform()
        
        # Ensure workspace file exists
        if not self.workspace_file.exists():
            return ActionResult(
                False, 
                "", 
                f"Workspace file not found: {self.workspace_file}"
            )
        
        try:
            if platform == Platform.WINDOWS:
                # Try common Cursor paths on Windows
                cursor_paths = [
                    Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "Cursor" / "Cursor.exe",
                    Path(os.environ.get("PROGRAMFILES", "")) / "Cursor" / "Cursor.exe",
                    Path(os.environ.get("PROGRAMFILES(X86)", "")) / "Cursor" / "Cursor.exe",
                ]
                
                cursor_exe = None
                for path in cursor_paths:
                    if path.exists():
                        cursor_exe = path
                        break
                
                if cursor_exe:
                    # Use absolute path to workspace file
                    # DETACHED_PROCESS makes the process independent of parent
                    # Redirect stdio to fully detach from parent process
                    subprocess.Popen(
                        [str(cursor_exe), str(self.workspace_file.resolve())],
                        creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP,
                        stdin=subprocess.DEVNULL,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL
                    )
                else:
                    # Try from PATH
                    subprocess.Popen(
                        ["cursor", str(self.workspace_file.resolve())], 
                        shell=True,
                        creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP,
                        stdin=subprocess.DEVNULL,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL
                    )
                    
            elif platform == Platform.MACOS:
                # Try command from PATH first (most reliable)
                try:
                    # Check if cursor command is available
                    result = subprocess.run(
                        ["which", "cursor"],
                        capture_output=True,
                        timeout=2
                    )
                    if result.returncode == 0:
                        subprocess.Popen(
                            ["cursor", str(self.workspace_file.resolve())],
                            start_new_session=True
                        )
                    else:
                        raise FileNotFoundError("cursor command not in PATH")
                except (FileNotFoundError, subprocess.TimeoutExpired):
                    # Fallback to direct app bundle path
                    cursor_app = Path("/Applications/Cursor.app")
                    if cursor_app.exists():
                        # Use the executable inside the app bundle
                        cursor_exe = cursor_app / "Contents" / "Resources" / "app" / "bin" / "cursor"
                        if cursor_exe.exists():
                            subprocess.Popen(
                                [str(cursor_exe), str(self.workspace_file.resolve())],
                                start_new_session=True
                            )
                        else:
                            # Fallback to open command
                            subprocess.Popen(
                                ["open", "-a", "Cursor", str(self.workspace_file.resolve())],
                                start_new_session=True
                            )
                    else:
                        return ActionResult(
                            False,
                            "",
                            "Cursor not found. Please install Cursor or add it to PATH."
                        )
            else:
                # Linux - try from PATH
                subprocess.Popen(
                    ["cursor", str(self.workspace_file.resolve())],
                    start_new_session=True
                )
            
            return ActionResult(
                True, 
                f"Cursor opened with workspace: {self.workspace_file.name}\n"
                f"Workspace points to project root where .cursorrules and .cursor/rules/ are located."
            )
        except Exception as e:
            return ActionResult(False, "", f"Failed to open Cursor: {e}")
    
    def open_visual_studio(self, vs_build_dirs: Optional[list[str]] = None) -> ActionResult:
        """Open project in Visual Studio.
        
        Args:
            vs_build_dirs: Optional list of build directory names for VS generator configs.
                          If not provided, uses the current build_dir.
        """
        if get_current_platform() != Platform.WINDOWS:
            return ActionResult(False, "", "Visual Studio is only available on Windows")
        
        # Determine which build directories to check
        if vs_build_dirs:
            build_dirs = [self.project_root / "build" / d for d in vs_build_dirs]
        else:
            build_dirs = [self.build_dir]
        
        opened_files = []
        errors = []
        
        for build_dir in build_dirs:
            sln_files = list(build_dir.glob("*.sln"))
            if not sln_files:
                errors.append(f"No .sln in build/{build_dir.name}")
                continue
            
            sln_file = sln_files[0]
            try:
                os.startfile(str(sln_file))
                opened_files.append(f"{build_dir.name}/{sln_file.name}")
            except Exception as e:
                errors.append(f"Failed to open {sln_file.name}: {e}")
        
        if opened_files:
            output = "Opened: " + ", ".join(opened_files)
            if errors:
                output += "\nWarnings: " + "; ".join(errors)
            return ActionResult(True, output)
        else:
            return ActionResult(
                False, 
                "", 
                "No .sln files found. Run CMake Configure with Visual Studio generator first.\n" + 
                "\n".join(errors)
            )
    
    def close_visual_studio(self) -> ActionResult:
        """Close Visual Studio instances and wait for processes to terminate."""
        if get_current_platform() != Platform.WINDOWS:
            return ActionResult(False, "", "Visual Studio is only available on Windows")
        
        try:
            # First, check if any Visual Studio processes are running
            check_result = subprocess.run(
                ["tasklist", "/FI", "IMAGENAME eq devenv.exe", "/FO", "CSV", "/NH"],
                capture_output=True,
                text=True,
                timeout=10
            )
            
            # If no processes found, nothing to close
            if check_result.returncode != 0 or not check_result.stdout.strip() or "devenv.exe" not in check_result.stdout:
                return ActionResult(True, "No Visual Studio instances were running")
            
            # Close all devenv.exe processes (Visual Studio)
            result = subprocess.run(
                ["taskkill", "/F", "/IM", "devenv.exe"],
                capture_output=True,
                text=True,
                timeout=30
            )
            
            if result.returncode == 0:
                # Wait for processes to actually terminate
                # Poll every 0.5 seconds for up to 10 seconds
                max_wait_time = 10.0
                poll_interval = 0.5
                elapsed = 0.0
                
                while elapsed < max_wait_time:
                    time.sleep(poll_interval)
                    elapsed += poll_interval
                    
                    # Check if processes are still running
                    check_result = subprocess.run(
                        ["tasklist", "/FI", "IMAGENAME eq devenv.exe", "/FO", "CSV", "/NH"],
                        capture_output=True,
                        text=True,
                        timeout=5
                    )
                    
                    # If no processes found or command failed, they're closed
                    if check_result.returncode != 0 or not check_result.stdout.strip() or "devenv.exe" not in check_result.stdout:
                        return ActionResult(True, "Visual Studio instances closed successfully")
                
                # Timeout reached but processes might still be closing
                # This is non-fatal - return success but with a warning
                return ActionResult(
                    True, 
                    "Visual Studio instances terminated (may still be closing background processes)"
                )
            elif "not found" in result.stderr.lower() or "not running" in result.stderr.lower():
                return ActionResult(True, "No Visual Studio instances were running")
            else:
                return ActionResult(False, result.stdout, result.stderr)
        except subprocess.TimeoutExpired:
            return ActionResult(False, "", "Close operation timed out")
        except Exception as e:
            return ActionResult(False, "", f"Failed to close Visual Studio: {e}")
    
    def open_xcode(self) -> ActionResult:
        """Open project in Xcode."""
        if get_current_platform() != Platform.MACOS:
            return ActionResult(False, "", "Xcode is only available on macOS")
        
        # Find .xcodeproj in build directory
        xcodeproj_dirs = list(self.build_dir.glob("*.xcodeproj"))
        if not xcodeproj_dirs:
            return ActionResult(False, "", "No .xcodeproj found. Run CMake Configure with Xcode generator first.")
        
        xcodeproj = xcodeproj_dirs[0]
        
        try:
            subprocess.Popen(["open", str(xcodeproj)])
            return ActionResult(True, f"Opened {xcodeproj.name}")
        except Exception as e:
            return ActionResult(False, "", f"Failed to open Xcode: {e}")
    
    def execute(self, action_id: str, on_output: Optional[Callable[[str], None]] = None, **kwargs) -> ActionResult:
        """Execute an action by ID.
        
        Args:
            action_id: ID of the action to execute
            on_output: Optional callback for real-time output streaming
            **kwargs: Additional arguments for the action
        """
        action_map = {
            "cmake_configure": lambda: self.cmake_configure(
                kwargs.get("build_type", "Debug"),
                kwargs.get("generator", "Ninja"),
                kwargs.get("compiler", "MSVC"),
                on_output=on_output
            ),
            "cmake_build": lambda: self.cmake_build(
                kwargs.get("build_type", "Debug"),
                kwargs.get("compiler", "MSVC"),
                on_output=on_output
            ),
            "close_vs": self.close_visual_studio,
            "clean_build": lambda: self.clean_build(kwargs.get("compiler", "MSVC"), kwargs.get("build_type", "Debug")),
            "clean_all_builds": self.clean_all_builds,
            "open_cursor": self.open_cursor,
            "open_vs": lambda: self.open_visual_studio(kwargs.get("vs_build_dirs")),
            "open_xcode": self.open_xcode,
            "open_ide": lambda: self._open_ide(kwargs.get("ide", "cursor")),
        }
        
        if action_id not in action_map:
            return ActionResult(False, "", f"Unknown action: {action_id}")
        
        return action_map[action_id]()
    
    def _open_ide(self, ide: str) -> ActionResult:
        """Open the specified IDE."""
        ide_map = {
            "cursor": self.open_cursor,
            "vs": self.open_visual_studio,
            "xcode": self.open_xcode,
        }
        
        if ide not in ide_map:
            return ActionResult(False, "", f"Unknown IDE: {ide}")
        
        return ide_map[ide]()


# Available actions registry
ACTIONS = {
    "cmake_configure": Action(
        id="cmake_configure",
        name="CMake Configure",
        description="Configure the project with CMake",
        platforms=[Platform.WINDOWS, Platform.MACOS, Platform.LINUX]
    ),
    "cmake_build": Action(
        id="cmake_build",
        name="CMake Build",
        description="Build the project with CMake",
        platforms=[Platform.WINDOWS, Platform.MACOS, Platform.LINUX]
    ),
    "clean_build": Action(
        id="clean_build",
        name="Clean Build",
        description="Clean current build directory (permanent deletion via git clean)",
        platforms=[Platform.WINDOWS, Platform.MACOS, Platform.LINUX]
    ),
    "clean_all_builds": Action(
        id="clean_all_builds",
        name="Clean All Builds",
        description="Clean all build directories (permanent deletion via git clean)",
        platforms=[Platform.WINDOWS, Platform.MACOS, Platform.LINUX]
    ),
    "open_cursor": Action(
        id="open_cursor",
        name="Open Cursor",
        description="Open project in Cursor IDE",
        platforms=[Platform.WINDOWS, Platform.MACOS, Platform.LINUX]
    ),
    "close_vs": Action(
        id="close_vs",
        name="Close Visual Studio",
        description="Close all Visual Studio instances",
        platforms=[Platform.WINDOWS]
    ),
    "open_vs": Action(
        id="open_vs",
        name="Open Visual Studio",
        description="Open project in Visual Studio",
        platforms=[Platform.WINDOWS]
    ),
    "open_xcode": Action(
        id="open_xcode",
        name="Open Xcode",
        description="Open project in Xcode",
        platforms=[Platform.MACOS]
    ),
}


def get_available_actions() -> dict[str, Action]:
    """Get actions available on current platform."""
    return {k: v for k, v in ACTIONS.items() if v.is_available()}
