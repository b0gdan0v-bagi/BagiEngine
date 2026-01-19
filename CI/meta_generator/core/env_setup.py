"""
LLVM Discovery and Initialization Module

Provides multi-level search for LLVM/libclang installation:
1. User settings (meta_generator_settings.json)
2. Environment variable (LIBCLANG_PATH)
3. Known platform-specific paths
4. llvm-config command
5. Bundled libclang (optional)
"""

import os
import platform
import subprocess
import shutil
from pathlib import Path
from dataclasses import dataclass
from typing import Optional, List

# Try to import clang, but don't fail if not available
try:
    import clang.cindex
    CLANG_AVAILABLE = True
except ImportError:
    CLANG_AVAILABLE = False


@dataclass
class LLVMStatus:
    """Status of LLVM/libclang discovery."""
    found: bool
    path: Optional[str]
    version: Optional[str]
    source: str  # "env_var", "known_path", "llvm_config", "bundled", "settings", "none"
    error: Optional[str] = None


class LLVMDiscovery:
    """Multi-level search for LLVM/libclang in the system."""
    
    # Known paths for different platforms
    KNOWN_PATHS = {
        "Windows": [
            "{ProgramFiles}/LLVM/bin",
            "{ProgramFiles}/LLVM/lib",
            "{ProgramW6432}/LLVM/bin",
            "C:/Program Files/LLVM/bin",
            "C:/LLVM/bin",
        ],
        "Linux": [
            "/usr/lib/llvm-18/lib",
            "/usr/lib/llvm-17/lib",
            "/usr/lib/llvm-16/lib",
            "/usr/lib/llvm-15/lib",
            "/usr/lib/llvm-14/lib",
            "/usr/lib/x86_64-linux-gnu",
            "/usr/lib64",
        ],
        "Darwin": [  # macOS
            "/usr/local/opt/llvm/lib",
            "/opt/homebrew/opt/llvm/lib",
            "/Library/Developer/CommandLineTools/usr/lib",
        ]
    }
    
    # Library file names for different platforms
    LIB_NAMES = {
        "Windows": ["libclang.dll"],
        "Linux": ["libclang.so", "libclang.so.1"],
        "Darwin": ["libclang.dylib"],
    }
    
    def __init__(self, settings_path: Optional[Path] = None, bundled_path: Optional[Path] = None):
        """
        Initialize LLVM discovery.
        
        Args:
            settings_path: Path to settings JSON file (meta_generator_settings.json)
            bundled_path: Path to bundled libclang directory (optional)
        """
        self.settings_path = settings_path
        self.bundled_path = bundled_path
        self._system = platform.system()
    
    def discover(self) -> LLVMStatus:
        """
        Search for LLVM in order of priority.
        
        Returns:
            LLVMStatus with discovery results
        """
        # 1. Check from settings.json (user specified manually)
        if self.settings_path and self.settings_path.exists():
            result = self._check_from_settings()
            if result.found:
                return result
        
        # 2. Environment variable LIBCLANG_PATH
        result = self._check_env_var()
        if result.found:
            return result
        
        # 3. Known paths for platform
        result = self._check_known_paths()
        if result.found:
            return result
        
        # 4. llvm-config (if available)
        result = self._check_llvm_config()
        if result.found:
            return result
        
        # 5. Bundled (libclang in tools folder)
        if self.bundled_path:
            result = self._check_bundled()
            if result.found:
                return result
        
        return LLVMStatus(
            found=False, 
            path=None, 
            version=None, 
            source="none",
            error="LLVM/libclang not found. Install LLVM or set LIBCLANG_PATH."
        )
    
    def _check_env_var(self) -> LLVMStatus:
        """Check LIBCLANG_PATH environment variable."""
        path = os.getenv("LIBCLANG_PATH")
        if path and self._validate_path(path):
            return LLVMStatus(
                found=True, 
                path=path, 
                version=self._get_version(path), 
                source="env_var"
            )
        return LLVMStatus(found=False, path=None, version=None, source="env_var")
    
    def _check_known_paths(self) -> LLVMStatus:
        """Check known platform-specific paths."""
        paths = self.KNOWN_PATHS.get(self._system, [])
        
        for path_template in paths:
            path = path_template.format(
                ProgramFiles=os.getenv("ProgramFiles", ""),
                ProgramW6432=os.getenv("ProgramW6432", "")
            )
            if self._validate_path(path):
                return LLVMStatus(
                    found=True, 
                    path=path, 
                    version=self._get_version(path), 
                    source="known_path"
                )
        
        return LLVMStatus(found=False, path=None, version=None, source="known_path")
    
    def _check_llvm_config(self) -> LLVMStatus:
        """Check using llvm-config command."""
        if not shutil.which("llvm-config"):
            return LLVMStatus(found=False, path=None, version=None, source="llvm_config")
        
        try:
            result = subprocess.run(
                ["llvm-config", "--libdir"], 
                capture_output=True, 
                text=True,
                timeout=5
            )
            if result.returncode == 0:
                path = result.stdout.strip()
                if self._validate_path(path):
                    # Get version
                    version_result = subprocess.run(
                        ["llvm-config", "--version"], 
                        capture_output=True, 
                        text=True,
                        timeout=5
                    )
                    version = version_result.stdout.strip() if version_result.returncode == 0 else None
                    return LLVMStatus(
                        found=True, 
                        path=path, 
                        version=version, 
                        source="llvm_config"
                    )
        except (subprocess.TimeoutExpired, Exception):
            pass
        
        return LLVMStatus(found=False, path=None, version=None, source="llvm_config")
    
    def _check_bundled(self) -> LLVMStatus:
        """Check for bundled libclang."""
        if self.bundled_path and self._validate_path(str(self.bundled_path)):
            return LLVMStatus(
                found=True, 
                path=str(self.bundled_path), 
                version=None, 
                source="bundled"
            )
        return LLVMStatus(found=False, path=None, version=None, source="bundled")
    
    def _check_from_settings(self) -> LLVMStatus:
        """Check path from settings.json."""
        import json
        try:
            with open(self.settings_path, encoding='utf-8') as f:
                settings = json.load(f)
            path = settings.get("llvm_bin_path")
            if path and self._validate_path(path):
                return LLVMStatus(
                    found=True, 
                    path=path, 
                    version=self._get_version(path), 
                    source="settings"
                )
        except Exception:
            pass
        return LLVMStatus(found=False, path=None, version=None, source="settings")
    
    def _validate_path(self, path: str) -> bool:
        """
        Check if libclang exists in the specified directory.
        
        Args:
            path: Directory path to check
            
        Returns:
            True if libclang found
        """
        p = Path(path)
        if not p.exists():
            return False
        
        # Get library names for current platform
        lib_names = self.LIB_NAMES.get(self._system, ["libclang.dll", "libclang.so", "libclang.dylib"])
        
        for name in lib_names:
            if (p / name).exists():
                return True
            # Also check versioned files (libclang.so.16)
            for f in p.glob(f"{name}*"):
                if f.is_file():
                    return True
        
        return False
    
    def _get_version(self, path: str) -> Optional[str]:
        """
        Try to get libclang version by initializing it.
        
        Args:
            path: Path to libclang directory
            
        Returns:
            Version string or None
        """
        if not CLANG_AVAILABLE:
            return None
            
        try:
            clang.cindex.Config.set_library_path(path)
            # Try to create index to verify it works
            idx = clang.cindex.Index.create()
            return "OK"  # Actual version detection would require more complex parsing
        except Exception:
            return None


def initialize_clang(settings_path: Optional[Path] = None, project_root: Optional[Path] = None) -> LLVMStatus:
    """
    Initialize libclang with auto-discovery of LLVM.
    
    Args:
        settings_path: Path to settings file (defaults to project_root/meta_generator_settings.json)
        project_root: Project root directory
        
    Returns:
        LLVMStatus with initialization results
    """
    if not CLANG_AVAILABLE:
        return LLVMStatus(
            found=False,
            path=None,
            version=None,
            source="none",
            error="Python 'clang' package not installed. Run: pip install clang"
        )
    
    # Determine settings path
    if settings_path is None and project_root:
        settings_path = project_root / "meta_generator_settings.json"
    
    # Determine bundled path (CI/meta_generator/bin)
    bundled_path = Path(__file__).parent.parent / "bin"
    
    discovery = LLVMDiscovery(
        settings_path=settings_path,
        bundled_path=bundled_path if bundled_path.exists() else None
    )
    
    status = discovery.discover()
    
    if status.found and status.path:
        try:
            clang.cindex.Config.set_library_path(status.path)
        except Exception as e:
            status = LLVMStatus(
                found=False,
                path=status.path,
                version=None,
                source=status.source,
                error=str(e)
            )
    
    return status


def is_libclang_available() -> bool:
    """
    Quick check if libclang is available and configured.
    
    Returns:
        True if libclang can be used
    """
    if not CLANG_AVAILABLE:
        return False
    
    try:
        idx = clang.cindex.Index.create()
        return True
    except Exception:
        return False
