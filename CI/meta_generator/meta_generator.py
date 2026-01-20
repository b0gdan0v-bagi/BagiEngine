#!/usr/bin/env python3
"""
BagiEngine Meta-Generator CLI

Command-line tool for generating reflection code from C++ headers.
Called by CMake during the configure step.

Usage:
    python meta_generator.py --source-dir src/Modules/BECore/Logger \\
                             --output-dir build/Generated \\
                             --cache-dir build \\
                             --include-dir src/Modules

Features:
    - Incremental generation (only re-parses changed files via SHA-256 hash)
    - Libclang AST parsing (LLVM required)
    - Factory and enum generation for FACTORY_BASE classes
"""

import argparse
import sys
from pathlib import Path
from typing import List, Set

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from core.env_setup import initialize_clang, LLVMStatus
from core.cache import MetadataCache
from core.parser import create_parser
from core.generator import CodeGenerator
from core.models import ClassData


def scan_headers(source_dirs: List[Path], extensions: List[str] = None) -> List[Path]:
    """
    Scan directories for header files.
    
    Args:
        source_dirs: Directories to scan
        extensions: File extensions to include
        
    Returns:
        List of header file paths
    """
    if extensions is None:
        extensions = ['.h', '.hpp', '.hxx']
    
    headers: List[Path] = []
    
    for source_dir in source_dirs:
        if not source_dir.exists():
            continue
        for ext in extensions:
            headers.extend(source_dir.rglob(f'*{ext}'))
    
    return headers


def main() -> int:
    """Main entry point for CLI."""
    parser = argparse.ArgumentParser(
        description='BagiEngine Meta-Generator - Reflection code generator',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic usage
  python meta_generator.py --source-dir src/Modules/BECore/Logger \\
                           --output-dir build/Generated \\
                           --cache-dir build

  # Multiple source directories with scan dirs for factory generation
  python meta_generator.py --source-dir src/Modules/BECore/Logger \\
                           --source-dir src/Modules/BECore/Widgets \\
                           --scan-dir src/Widgets \\
                           --output-dir build/Generated \\
                           --cache-dir build \\
                           --include-dir src/Modules
"""
    )
    
    parser.add_argument(
        '--source-dir', '-s',
        action='append',
        dest='source_dirs',
        required=True,
        help='Source directory to scan for headers (can be specified multiple times)'
    )
    
    parser.add_argument(
        '--output-dir', '-o',
        required=True,
        help='Output directory for generated files'
    )
    
    parser.add_argument(
        '--cache-dir', '-c',
        required=True,
        help='Directory for metadata cache file'
    )
    
    parser.add_argument(
        '--settings',
        help='Path to settings JSON file (for LLVM path)'
    )
    
    parser.add_argument(
        '--include-dir', '-I',
        action='append',
        dest='include_dirs',
        default=[],
        help='Include directory for computing include paths (can be specified multiple times)'
    )
    
    parser.add_argument(
        '--scan-dir', '-S',
        action='append',
        dest='scan_dirs',
        default=[],
        help='Additional directory to scan for derived classes (factory generation)'
    )
    
    parser.add_argument(
        '--force', '-f',
        action='store_true',
        help='Force full rescan (ignore cache)'
    )
    
    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help='Enable verbose output'
    )
    
    parser.add_argument(
        '--quiet', '-q',
        action='store_true',
        help='Suppress all output except errors'
    )
    
    args = parser.parse_args()
    
    # Convert paths
    source_dirs = [Path(d) for d in args.source_dirs]
    output_dir = Path(args.output_dir)
    cache_dir = Path(args.cache_dir)
    include_dirs = args.include_dirs
    scan_dirs = [Path(d) for d in args.scan_dirs] if args.scan_dirs else source_dirs
    settings_path = Path(args.settings) if args.settings else None
    
    def log(msg: str):
        if not args.quiet:
            print(msg)
    
    def verbose_log(msg: str):
        if args.verbose and not args.quiet:
            print(msg)
    
    # Initialize libclang (required)
    verbose_log("Initializing libclang...")
    
    llvm_status = initialize_clang(settings_path=settings_path)
    
    if llvm_status.found:
        verbose_log(f"  libclang: OK (via {llvm_status.source})")
        if llvm_status.path:
            verbose_log(f"  Path: {llvm_status.path}")
    else:
        # LLVM is required - fail with helpful error message
        print("ERROR: LLVM/libclang is required but not available.", file=sys.stderr)
        if llvm_status.error:
            print(f"  Reason: {llvm_status.error}", file=sys.stderr)
        print("", file=sys.stderr)
        print("Set system environment variable:", file=sys.stderr)
        print("  Windows: set LIBCLANG_PATH=D:\\LLVM\\bin", file=sys.stderr)
        print("  Linux:   export LIBCLANG_PATH=/usr/lib/llvm-18/lib", file=sys.stderr)
        print("  macOS:   export LIBCLANG_PATH=/opt/homebrew/opt/llvm/lib", file=sys.stderr)
        print("", file=sys.stderr)
        print("Install LLVM from: https://github.com/llvm/llvm-project/releases", file=sys.stderr)
        return 1
    
    # Load cache
    cache_path = cache_dir / "metadata_cache.json"
    cache = MetadataCache(cache_path)
    cache_loaded = cache.load()
    
    if cache_loaded:
        stats = cache.get_statistics()
        verbose_log(f"Loaded cache: {stats['files']} files, {stats['classes']} classes")
    else:
        verbose_log("No cache found, starting fresh")
    
    # Create parser (libclang only)
    try:
        cpp_parser = create_parser(include_dirs)
        verbose_log("Using libclang parser")
    except RuntimeError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    
    # Create generator
    generator = CodeGenerator(output_dir=output_dir)
    
    # Get files to process
    all_headers = scan_headers(source_dirs)
    
    if args.force:
        files_to_process = all_headers
        log(f"Force mode: processing all {len(files_to_process)} files")
    else:
        files_to_process = cache.get_outdated_files(source_dirs)
        if files_to_process:
            log(f"Processing {len(files_to_process)} changed files")
        else:
            log("All files up to date")
    
    # Process files
    processed_count = 0
    generated_count = 0
    error_count = 0
    
    for file_path in files_to_process:
        verbose_log(f"  Parsing: {file_path.name}")
        
        try:
            classes, enums = cpp_parser.parse_file(file_path)
            
            # Update cache
            cache.update_file(file_path, classes, enums)
            processed_count += 1
            
            # Generate reflection code
            if classes or enums:
                output_path = generator.generate_reflection(
                    classes, enums, file_path.name
                )
                if output_path:
                    generated_count += 1
                    verbose_log(f"    Generated: {output_path.name}")
                    
                    for cls in classes:
                        if cls.is_factory_base:
                            verbose_log(f"    Found factory base: {cls.name}")
            
        except Exception as e:
            error_count += 1
            if not args.quiet:
                print(f"Error processing {file_path}: {e}", file=sys.stderr)
    
    # Generate factory code for factory bases
    # Scan additional directories for derived classes
    if scan_dirs:
        verbose_log("Scanning for derived classes...")
        scan_headers_list = scan_headers(scan_dirs)
        
        for file_path in scan_headers_list:
            if str(file_path.resolve()) not in cache.files:
                verbose_log(f"  Parsing: {file_path.name}")
                try:
                    classes, enums = cpp_parser.parse_file(file_path)
                    cache.update_file(file_path, classes, enums)
                    
                    # Generate reflection code for newly found classes
                    if classes or enums:
                        output_path = generator.generate_reflection(
                            classes, enums, file_path.name
                        )
                        if output_path:
                            generated_count += 1
                            verbose_log(f"    Generated: {output_path.name}")
                except Exception as e:
                    error_count += 1
                    if not args.quiet:
                        print(f"Error processing {file_path}: {e}", file=sys.stderr)
    
    # Build and generate factory bases
    all_classes = cache.get_all_classes()
    factory_bases = generator.build_factory_bases(all_classes, include_dirs)
    
    for factory_base in factory_bases:
        verbose_log(f"Generating factory for {factory_base.name}: {len(factory_base.derived)} derived classes")
        output_path = generator.generate_enum_factory(factory_base, f"{factory_base.name}.h")
        if output_path:
            verbose_log(f"    Generated: {output_path.name}")
    
    # Cleanup deleted files from cache
    existing_files = set(scan_headers(source_dirs + scan_dirs))
    removed = cache.cleanup_deleted_files(existing_files)
    if removed > 0:
        verbose_log(f"Removed {removed} deleted files from cache")
    
    # Save cache
    cache.save()
    verbose_log(f"Cache saved to {cache_path}")
    
    # Summary
    if not args.quiet:
        stats = cache.get_statistics()
        log(f"Done: {processed_count} files processed, {generated_count} files generated")
        if error_count > 0:
            log(f"  Errors: {error_count}")
        verbose_log(f"  Cache: {stats['files']} files, {stats['classes']} classes, {stats['enums']} enums")
    
    return 0 if error_count == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
