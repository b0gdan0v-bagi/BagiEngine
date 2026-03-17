"""
LLVM Discovery stubs (kept for backwards compatibility with GUI imports).

LLVM/libclang is no longer required — the meta-generator uses a regex parser.
"""


class LLVMStatus:
    """Stub retained for GUI compatibility."""
    def __init__(self, found=False, path=None, version=None, source="none", error=None):
        self.found = found
        self.path = path
        self.version = version
        self.source = source
        self.error = error


class LLVMDiscovery:
    """Stub retained for GUI compatibility."""
    def __init__(self, **_kwargs):
        pass

    def discover(self) -> LLVMStatus:
        return LLVMStatus(found=False, source="none", error="LLVM is no longer required")

    def _validate_path(self, path: str) -> bool:
        return False


def initialize_clang(**_kwargs) -> LLVMStatus:
    return LLVMStatus(found=False, source="none", error="LLVM is no longer required")


def is_libclang_available() -> bool:
    return False
