"""Action definitions for the launcher."""

import subprocess
import sys
import os
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


def get_build_dir_name(compiler: str, platform: Optional[Platform] = None) -> str:
    """Get build directory name based on compiler and platform.
    
    Args:
        compiler: Compiler name (MSVC, Clang, GCC)
        platform: Target platform (defaults to current platform)
        
    Returns:
        Build directory name like "MSVCWin", "ClangMac", etc.
    """
    if platform is None:
        platform = get_current_platform()
    
    platform_suffix = {
        Platform.WINDOWS: "Win",
        Platform.MACOS: "Mac",
        Platform.LINUX: "Linux"
    }
    
    # Normalize compiler name (remove spaces and special chars)
    compiler_name = compiler.replace(" ", "").replace("(", "").replace(")", "")
    # Handle GCC (MinGW) -> GCC
    if "MinGW" in compiler:
        compiler_name = "GCC"
    
    return f"{compiler_name}{platform_suffix[platform]}"


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
    
    def __init__(self, project_root: Path, compiler: str = "MSVC"):
        self.project_root = project_root
        self._compiler = compiler
        self.workspace_file = project_root / "CI" / "BagiEngine.code-workspace"
    
    @property
    def build_dir(self) -> Path:
        """Get build directory based on current compiler."""
        build_dir_name = get_build_dir_name(self._compiler)
        return self.project_root / "build" / build_dir_name
    
    def set_compiler(self, compiler: str) -> None:
        """Set the current compiler for build directory resolution."""
        self._compiler = compiler
    
    def clean_build(self, compiler: str = "MSVC") -> ActionResult:
        """Clean build directory using git clean (permanent deletion, no recycle bin).
        
        Uses 'git clean -fdx' to forcefully remove all untracked files and directories
        in the build folder. This is a permanent deletion.
        """
        self.set_compiler(compiler)
        
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
            
            if result.returncode == 0:
                cleaned_files = result.stdout.strip()
                if cleaned_files:
                    return ActionResult(
                        True, 
                        f"Cleaned build directory: build/{get_build_dir_name(compiler)}\n{cleaned_files}"
                    )
                else:
                    return ActionResult(True, f"Build directory already clean: build/{get_build_dir_name(compiler)}")
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
                        compiler: str = "MSVC") -> ActionResult:
        """Run CMake configuration."""
        self.set_compiler(compiler)
        self.build_dir.mkdir(parents=True, exist_ok=True)
        
        cmd = ["cmake", "-B", str(self.build_dir), "-S", str(self.project_root)]
        
        if generator:
            cmd.extend(["-G", generator])
        
        cmd.append(f"-DCMAKE_BUILD_TYPE={build_type}")
        
        # Add Visual Studio architecture for VS generator
        if "Visual Studio" in generator:
            cmd.extend(["-A", "x64"])
        
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
        
        try:
            result = subprocess.run(
                cmd,
                cwd=self.project_root,
                capture_output=True,
                text=True,
                timeout=300
            )
            return ActionResult(
                success=result.returncode == 0,
                output=result.stdout,
                error=result.stderr
            )
        except subprocess.TimeoutExpired:
            return ActionResult(False, "", "CMake configuration timed out")
        except Exception as e:
            return ActionResult(False, "", str(e))
    
    def cmake_build(self, build_type: str = "Debug", compiler: str = "MSVC") -> ActionResult:
        """Run CMake build."""
        self.set_compiler(compiler)
        if not self.build_dir.exists():
            return ActionResult(False, "", f"Build directory {self.build_dir} does not exist. Run CMake Configure first.")
        
        cmd = ["cmake", "--build", str(self.build_dir), "--config", build_type]
        
        try:
            result = subprocess.run(
                cmd,
                cwd=self.project_root,
                capture_output=True,
                text=True,
                timeout=600
            )
            return ActionResult(
                success=result.returncode == 0,
                output=result.stdout,
                error=result.stderr
            )
        except subprocess.TimeoutExpired:
            return ActionResult(False, "", "CMake build timed out")
        except Exception as e:
            return ActionResult(False, "", str(e))
    
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
                    subprocess.Popen(
                        [str(cursor_exe), str(self.workspace_file.resolve())]
                    )
                else:
                    # Try from PATH
                    subprocess.Popen(
                        ["cursor", str(self.workspace_file.resolve())], 
                        shell=True
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
                        subprocess.Popen(["cursor", str(self.workspace_file.resolve())])
                    else:
                        raise FileNotFoundError("cursor command not in PATH")
                except (FileNotFoundError, subprocess.TimeoutExpired):
                    # Fallback to direct app bundle path
                    cursor_app = Path("/Applications/Cursor.app")
                    if cursor_app.exists():
                        # Use the executable inside the app bundle
                        cursor_exe = cursor_app / "Contents" / "Resources" / "app" / "bin" / "cursor"
                        if cursor_exe.exists():
                            subprocess.Popen([str(cursor_exe), str(self.workspace_file.resolve())])
                        else:
                            # Fallback to open command
                            subprocess.Popen(["open", "-a", "Cursor", str(self.workspace_file.resolve())])
                    else:
                        return ActionResult(
                            False,
                            "",
                            "Cursor not found. Please install Cursor or add it to PATH."
                        )
            else:
                # Linux - try from PATH
                subprocess.Popen(["cursor", str(self.workspace_file.resolve())])
            
            return ActionResult(
                True, 
                f"Cursor opened with workspace: {self.workspace_file.name}\n"
                f"Workspace points to project root where .cursorrules and .cursor/rules/ are located."
            )
        except Exception as e:
            return ActionResult(False, "", f"Failed to open Cursor: {e}")
    
    def open_visual_studio(self) -> ActionResult:
        """Open project in Visual Studio."""
        if get_current_platform() != Platform.WINDOWS:
            return ActionResult(False, "", "Visual Studio is only available on Windows")
        
        # Find .sln file in build directory
        sln_files = list(self.build_dir.glob("*.sln"))
        if not sln_files:
            return ActionResult(False, "", "No .sln file found. Run CMake Configure with Visual Studio generator first.")
        
        sln_file = sln_files[0]
        
        try:
            os.startfile(str(sln_file))
            return ActionResult(True, f"Opened {sln_file.name}")
        except Exception as e:
            return ActionResult(False, "", f"Failed to open Visual Studio: {e}")
    
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
    
    def execute(self, action_id: str, **kwargs) -> ActionResult:
        """Execute an action by ID."""
        action_map = {
            "cmake_configure": lambda: self.cmake_configure(
                kwargs.get("build_type", "Debug"),
                kwargs.get("generator", "Ninja"),
                kwargs.get("compiler", "MSVC")
            ),
            "cmake_build": lambda: self.cmake_build(
                kwargs.get("build_type", "Debug"),
                kwargs.get("compiler", "MSVC")
            ),
            "clean_build": lambda: self.clean_build(kwargs.get("compiler", "MSVC")),
            "clean_all_builds": self.clean_all_builds,
            "open_cursor": self.open_cursor,
            "open_vs": self.open_visual_studio,
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
