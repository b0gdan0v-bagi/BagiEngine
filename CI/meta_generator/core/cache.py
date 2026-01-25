"""
Metadata Cache Module

Provides caching of parsed reflection metadata to avoid re-parsing unchanged files.
Uses SHA-256 hashing to detect file changes.
"""

import json
import hashlib
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional, Set

from .models import FileMetadata, ClassData, EnumData


class MetadataCache:
    """
    Cache for reflection metadata with file change detection.
    
    Stores parsed metadata in a JSON file and uses SHA-256 hashes
    to detect when files need to be re-parsed.
    """
    
    CACHE_VERSION = "1.1"  # Bumped for is_primitive field support
    
    def __init__(self, cache_path: Path):
        """
        Initialize metadata cache.
        
        Args:
            cache_path: Path to cache JSON file (e.g., build/metadata_cache.json)
        """
        self.cache_path = cache_path
        self.files: Dict[str, FileMetadata] = {}
        self._version = self.CACHE_VERSION
        self._generated_at: Optional[str] = None
    
    def load(self) -> bool:
        """
        Load cache from disk.
        
        Returns:
            True if cache was loaded successfully, False otherwise
        """
        if not self.cache_path.exists():
            return False
        
        try:
            with open(self.cache_path, 'r', encoding='utf-8') as f:
                data = json.load(f)
            
            # Check version compatibility
            if data.get("version") != self.CACHE_VERSION:
                # Version mismatch - invalidate cache
                self.files = {}
                return False
            
            self._version = data.get("version", self.CACHE_VERSION)
            self._generated_at = data.get("generated_at")
            
            # Load file metadata
            files_data = data.get("files", {})
            for path, file_data in files_data.items():
                self.files[path] = FileMetadata.from_dict(file_data)
            
            return True
            
        except (json.JSONDecodeError, KeyError, TypeError) as e:
            # Corrupted cache - start fresh
            self.files = {}
            return False
    
    def save(self) -> None:
        """Save cache to disk."""
        # Ensure parent directory exists
        self.cache_path.parent.mkdir(parents=True, exist_ok=True)
        
        data = {
            "version": self.CACHE_VERSION,
            "generated_at": datetime.now().isoformat(),
            "files": {path: meta.to_dict() for path, meta in self.files.items()}
        }
        
        with open(self.cache_path, 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
    
    @staticmethod
    def get_file_hash(path: Path) -> str:
        """
        Calculate SHA-256 hash of file contents.
        
        Args:
            path: Path to file
            
        Returns:
            Hex string of SHA-256 hash
        """
        hasher = hashlib.sha256()
        
        try:
            with open(path, 'rb') as f:
                # Read in chunks to handle large files
                for chunk in iter(lambda: f.read(8192), b''):
                    hasher.update(chunk)
            return hasher.hexdigest()
        except (IOError, OSError):
            return ""
    
    def is_file_changed(self, path: Path) -> bool:
        """
        Check if a file has changed since last scan.
        
        Args:
            path: Path to file
            
        Returns:
            True if file is new or has changed
        """
        path_str = str(path.resolve())
        
        # File not in cache - needs processing
        if path_str not in self.files:
            return True
        
        # Compare hashes
        current_hash = self.get_file_hash(path)
        cached_hash = self.files[path_str].content_hash
        
        return current_hash != cached_hash
    
    def update_file(self, path: Path, classes: List[ClassData], enums: List[EnumData]) -> None:
        """
        Update cache with new file metadata.
        
        Args:
            path: Path to source file
            classes: List of classes found in file
            enums: List of enums found in file
        """
        path_str = str(path.resolve())
        
        metadata = FileMetadata(
            path=path_str,
            content_hash=self.get_file_hash(path),
            last_scanned=datetime.now().isoformat(),
            classes=classes,
            enums=enums
        )
        
        self.files[path_str] = metadata
    
    def remove_file(self, path: Path) -> None:
        """
        Remove a file from cache.
        
        Args:
            path: Path to file to remove
        """
        path_str = str(path.resolve())
        if path_str in self.files:
            del self.files[path_str]
    
    def get_outdated_files(self, source_dirs: List[Path], extensions: List[str] = None) -> List[Path]:
        """
        Get list of files that need to be re-processed.
        
        Args:
            source_dirs: Directories to scan for source files
            extensions: File extensions to include (default: ['.h', '.hpp'])
            
        Returns:
            List of file paths that are new or changed
        """
        if extensions is None:
            extensions = ['.h', '.hpp', '.hxx']
        
        outdated: List[Path] = []
        
        for source_dir in source_dirs:
            if not source_dir.exists():
                continue
            
            for ext in extensions:
                for file_path in source_dir.rglob(f'*{ext}'):
                    if self.is_file_changed(file_path):
                        outdated.append(file_path)
        
        return outdated
    
    def get_all_files(self, source_dirs: List[Path], extensions: List[str] = None) -> List[Path]:
        """
        Get all source files from directories.
        
        Args:
            source_dirs: Directories to scan
            extensions: File extensions to include
            
        Returns:
            List of all matching file paths
        """
        if extensions is None:
            extensions = ['.h', '.hpp', '.hxx']
        
        all_files: List[Path] = []
        
        for source_dir in source_dirs:
            if not source_dir.exists():
                continue
            
            for ext in extensions:
                all_files.extend(source_dir.rglob(f'*{ext}'))
        
        return all_files
    
    def get_all_classes(self) -> List[ClassData]:
        """
        Get all classes from all cached files.
        
        Returns:
            List of all ClassData objects
        """
        classes: List[ClassData] = []
        for file_meta in self.files.values():
            classes.extend(file_meta.classes)
        return classes
    
    def get_all_enums(self) -> List[EnumData]:
        """
        Get all enums from all cached files.
        
        Returns:
            List of all EnumData objects
        """
        enums: List[EnumData] = []
        for file_meta in self.files.values():
            enums.extend(file_meta.enums)
        return enums
    
    def get_classes_by_file(self, path: Path) -> List[ClassData]:
        """
        Get classes from a specific file.
        
        Args:
            path: Path to source file
            
        Returns:
            List of ClassData objects from that file
        """
        path_str = str(path.resolve())
        if path_str in self.files:
            return self.files[path_str].classes
        return []
    
    def get_enums_by_file(self, path: Path) -> List[EnumData]:
        """
        Get enums from a specific file.
        
        Args:
            path: Path to source file
            
        Returns:
            List of EnumData objects from that file
        """
        path_str = str(path.resolve())
        if path_str in self.files:
            return self.files[path_str].enums
        return []
    
    def find_factory_bases(self) -> List[ClassData]:
        """
        Find all classes marked as factory bases.
        
        Returns:
            List of ClassData objects with is_factory_base=True
        """
        return [cls for cls in self.get_all_classes() if cls.is_factory_base]
    
    def find_derived_classes(self, base_class_name: str) -> List[ClassData]:
        """
        Find all classes that inherit from a specific base class.
        
        Args:
            base_class_name: Name of the base class
            
        Returns:
            List of ClassData objects that inherit from the base
        """
        return [cls for cls in self.get_all_classes() 
                if cls.parent_class == base_class_name]
    
    def cleanup_deleted_files(self, existing_files: Set[Path]) -> int:
        """
        Remove cache entries for files that no longer exist.
        
        Args:
            existing_files: Set of currently existing file paths
            
        Returns:
            Number of entries removed
        """
        existing_strs = {str(p.resolve()) for p in existing_files}
        to_remove = [path for path in self.files.keys() if path not in existing_strs]
        
        for path in to_remove:
            del self.files[path]
        
        return len(to_remove)
    
    def get_statistics(self) -> Dict[str, int]:
        """
        Get cache statistics.
        
        Returns:
            Dictionary with counts of files, classes, enums, etc.
        """
        total_classes = sum(len(f.classes) for f in self.files.values())
        total_enums = sum(len(f.enums) for f in self.files.values())
        total_fields = sum(
            len(c.fields) 
            for f in self.files.values() 
            for c in f.classes
        )
        total_methods = sum(
            len(c.methods) 
            for f in self.files.values() 
            for c in f.classes
        )
        factory_bases = sum(
            1 for f in self.files.values() 
            for c in f.classes 
            if c.is_factory_base
        )
        
        return {
            "files": len(self.files),
            "classes": total_classes,
            "enums": total_enums,
            "fields": total_fields,
            "methods": total_methods,
            "factory_bases": factory_bases,
        }
