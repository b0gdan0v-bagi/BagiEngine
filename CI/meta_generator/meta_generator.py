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
import json
import os
import sys
import time
from pathlib import Path
from typing import List, Set

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from core.env_setup import initialize_clang, LLVMStatus
from core.cache import MetadataCache
from core.parser import create_parser
from core.generator import CodeGenerator
from core.models import ClassData, EnumData


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

  # Use config for target (CMake mode)
  python meta_generator.py --target BECoreModule --project-root . \\
                           --output-dir build/Generated --cache-dir build --settings meta_generator_settings.json
"""
    )
    
    parser.add_argument(
        '--target', '-t',
        help='Target name from reflection_targets.json (when set, source/scan/include dirs are loaded from config)'
    )
    
    parser.add_argument(
        '--project-root',
        help='Project root for resolving relative paths in reflection_targets.json (used with --target)'
    )
    
    parser.add_argument(
        '--source-dir', '-s',
        action='append',
        dest='source_dirs',
        default=[],
        help='Source directory to scan for headers (can be specified multiple times; not used with --target)'
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
    
    output_dir = Path(args.output_dir)
    cache_dir = Path(args.cache_dir)
    settings_path = Path(args.settings) if args.settings else None
    
    # Load dirs from config when --target is set
    if args.target:
        if args.source_dirs or args.scan_dirs or args.include_dirs:
            print("Warning: --source-dir/--scan-dir/--include-dir are ignored when --target is set", file=sys.stderr)
        project_root = Path(args.project_root).resolve() if args.project_root else Path.cwd()
        config_path = Path(__file__).parent / "reflection_targets.json"
        if not config_path.exists():
            print(f"ERROR: Reflection targets config not found: {config_path}", file=sys.stderr)
            return 1
        with open(config_path, encoding='utf-8') as f:
            targets_config = json.load(f)
        if args.target not in targets_config:
            print(f"ERROR: Unknown target '{args.target}' in reflection_targets.json. Known: {list(targets_config.keys())}", file=sys.stderr)
            return 1
        cfg = targets_config[args.target]
        source_dirs = [project_root / d for d in cfg.get("source_dirs", [])]
        scan_dirs = [project_root / d for d in cfg.get("scan_dirs", [])]
        include_dirs = [str(project_root / d) for d in cfg.get("include_dirs", [])]
    else:
        if not args.source_dirs:
            parser.error("Either --target or at least one --source-dir is required")
        source_dirs = [Path(d) for d in args.source_dirs]
        include_dirs = args.include_dirs
        scan_dirs = [Path(d) for d in args.scan_dirs] if args.scan_dirs else source_dirs
    
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
    t0_load = time.perf_counter()
    cache_loaded = cache.load()
    cache_load_elapsed = time.perf_counter() - t0_load
    if args.verbose and not args.quiet:
        log(f"Cache load: {cache_load_elapsed:.2f} s")
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
    
    # Pass 1: parse all files and update cache.
    # Parsing is done first for ALL files so that factory base information is
    # complete before any code generation begins. This avoids the ordering race
    # where LoggerManager.h could be generated before ILogSink.h is parsed,
    # resulting in missing FactoryTraits includes.
    processed_count = 0
    generated_count = 0
    error_count = 0

    files_to_generate: List[Path] = []

    for file_path in files_to_process:
        verbose_log(f"  Parsing: {file_path.name}")
        try:
            classes, enums = cpp_parser.parse_file(file_path)
            cache.update_file(file_path, classes, enums)
            processed_count += 1
            if classes or enums:
                files_to_generate.append(file_path)
                for cls in classes:
                    if cls.is_factory_base:
                        verbose_log(f"    Found factory base: {cls.name}")
        except Exception as e:
            error_count += 1
            if not args.quiet:
                print(f"Error processing {file_path}: {e}", file=sys.stderr)

    if scan_dirs:
        verbose_log("Scanning for derived classes...")
        scan_headers_list = scan_headers(scan_dirs)
        for file_path in scan_headers_list:
            if str(file_path.resolve()) not in cache.files:
                verbose_log(f"  Parsing: {file_path.name}")
                try:
                    classes, enums = cpp_parser.parse_file(file_path)
                    cache.update_file(file_path, classes, enums)
                    if classes or enums:
                        files_to_generate.append(file_path)
                except Exception as e:
                    error_count += 1
                    if not args.quiet:
                        print(f"Error processing {file_path}: {e}", file=sys.stderr)

    # After all parsing: build the complete factory-base picture once.
    all_classes = cache.get_all_classes()
    factory_bases = generator.build_factory_bases(all_classes, include_dirs)
    if factory_bases:
        verbose_log(f"Factory bases found: {[fb.name for fb in factory_bases]}")

    # Pass 2: generate code for all files that had reflected content.
    # At this point every factory base is known, so FactoryTraits can be
    # inlined correctly into whichever gen file needs them.
    for file_path in files_to_generate:
        classes, enums = cache.get_file_data(file_path)
        try:
            output_path = generator.generate_reflection(
                classes, enums, file_path, include_dirs, all_classes, factory_bases
            )
            if output_path:
                generated_count += 1
                verbose_log(f"    Generated: {output_path.name}")
        except Exception as e:
            error_count += 1
            if not args.quiet:
                print(f"Error generating {file_path}: {e}", file=sys.stderr)

    # Cleanup deleted files from cache
    # Only cleanup files from directories we're responsible for (not the entire cache)
    # This allows multiple modules to share the same cache without overwriting each other
    scanned_dirs = source_dirs + scan_dirs
    existing_files_in_scanned_dirs = set(scan_headers(scanned_dirs))
    
    # Find files in cache that were from our directories but no longer exist
    to_remove = []
    for cached_path in cache.files.keys():
        cached_path_obj = Path(cached_path)
        # Check if this file was from one of our directories
        for scan_dir in scanned_dirs:
            try:
                cached_path_obj.relative_to(scan_dir.resolve())
                # File was from our directory - check if it still exists
                if cached_path_obj not in existing_files_in_scanned_dirs and \
                   Path(cached_path) not in existing_files_in_scanned_dirs:
                    to_remove.append(cached_path)
                break
            except ValueError:
                # Not from this directory, check next
                continue
    
    for path in to_remove:
        cache.remove_file(Path(path))
    
    if to_remove:
        verbose_log(f"Removed {len(to_remove)} deleted files from cache")
    
    # Save cache
    t0_save = time.perf_counter()
    cache.save()
    cache_save_elapsed = time.perf_counter() - t0_save
    if args.verbose and not args.quiet:
        log(f"Cache save (serialization): {cache_save_elapsed:.2f} s")
    verbose_log(f"Cache saved to {cache_path}")
    
    # Summary
    if not args.quiet:
        stats = cache.get_statistics()
        log(f"Done: {processed_count} files processed, {generated_count} files generated")
        if error_count > 0:
            log(f"  Errors: {error_count}")
        verbose_log(f"  Cache: {stats['files']} files, {stats['classes']} classes, {stats['enums']} enums")
        log(f"  Cache load: {cache_load_elapsed:.2f} s | Cache save (serialization): {cache_save_elapsed:.2f} s")
    
    return 0 if error_count == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
