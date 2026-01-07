#!/usr/bin/env python3
"""
Rename C++ classes to match MFCG naming conventions.
Based on the deobfuscated reference implementation mappings.
"""

import re
import os
import sys
from pathlib import Path

# C++ renames to match MFCG naming
# Format: "old_name" -> "new_name"
RENAMES = {
    # Class renames
    "Patch": "Cell",
    "Model": "City",

    # Member/variable renames
    "patches": "cells",
    "patchByVertex": "cellsByVertex",
    "patchByVertexPtr": "cellsByVertexPtr",
    "ownedPatches_": "ownedCells_",
    "nPatches_": "nCells_",
    "nPatches": "nCells",

    # Include file renames (handled separately for #include)
    # "Patch.h": "Cell.h",  # Handled by file rename
    # "Model.h": "City.h",  # Handled by file rename
}

# Files to rename (old -> new)
FILE_RENAMES = {
    "Patch.h": "Cell.h",
    "Patch.cpp": "Cell.cpp",
    "Model.h": "City.h",
    "Model.cpp": "City.cpp",
}

# Include path renames
INCLUDE_RENAMES = {
    "town_generator/building/Patch.h": "town_generator/building/Cell.h",
    "town_generator/building/Model.h": "town_generator/building/City.h",
}


def rename_in_file(filepath: Path, dry_run: bool = False) -> int:
    """
    Apply renames to a single file.
    Returns the number of replacements made.
    """
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    except UnicodeDecodeError:
        print(f"  Skipping binary file: {filepath}")
        return 0

    original = content
    total_replacements = 0

    # First handle include renames
    for old_include, new_include in INCLUDE_RENAMES.items():
        pattern = rf'#include\s+"({re.escape(old_include)})"'
        replacement = f'#include "{new_include}"'
        count = len(re.findall(pattern, content))
        if count > 0:
            content = re.sub(pattern, replacement, content)
            total_replacements += count
            print(f"  #include {old_include} -> {new_include}: {count}")

    # Sort by length (longest first) to avoid partial replacements
    sorted_renames = sorted(RENAMES.items(), key=lambda x: (-len(x[0]), x[0]))

    for old_name, new_name in sorted_renames:
        # Use word boundaries to avoid partial matches
        pattern = rf'\b{re.escape(old_name)}\b'

        count = len(re.findall(pattern, content))
        if count > 0:
            content = re.sub(pattern, new_name, content)
            total_replacements += count
            print(f"  {old_name} -> {new_name}: {count}")

    if content != original and not dry_run:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)

    return total_replacements


def rename_files(base_dir: Path, dry_run: bool = False) -> int:
    """Rename files according to FILE_RENAMES mapping."""
    count = 0
    for old_name, new_name in FILE_RENAMES.items():
        # Search for files with old name
        for old_path in base_dir.rglob(old_name):
            new_path = old_path.parent / new_name
            print(f"  Renaming: {old_path} -> {new_path}")
            if not dry_run:
                if new_path.exists():
                    print(f"    WARNING: {new_path} already exists, skipping")
                else:
                    old_path.rename(new_path)
                    count += 1
            else:
                count += 1
    return count


def main():
    dry_run = '--dry-run' in sys.argv

    base_dir = Path(__file__).parent

    # Directories to process
    src_dir = base_dir / "src"
    include_dir = base_dir / "include"
    tests_dir = base_dir / "tests"

    print("=" * 60)
    print("C++ MFCG Naming Rename Script")
    print("=" * 60)

    if dry_run:
        print("(DRY RUN - no files will be modified)")
    print()

    # Step 1: Rename content in all files
    print("Step 1: Renaming content in files...")
    print("-" * 40)

    targets = []
    for dir_path in [src_dir, include_dir, tests_dir]:
        if dir_path.exists():
            targets.extend(dir_path.rglob("*.cpp"))
            targets.extend(dir_path.rglob("*.h"))

    # Also process CMakeLists.txt
    cmake_file = base_dir / "CMakeLists.txt"
    if cmake_file.exists():
        targets.append(cmake_file)

    total_content_changes = 0
    for filepath in sorted(targets):
        print(f"\n{filepath.relative_to(base_dir)}:")
        count = rename_in_file(filepath, dry_run)
        total_content_changes += count
        if count == 0:
            print("  (no changes)")

    print()
    print("-" * 40)
    print(f"Content changes: {total_content_changes}")
    print()

    # Step 2: Rename files
    print("Step 2: Renaming files...")
    print("-" * 40)

    file_renames = rename_files(base_dir, dry_run)

    print()
    print("-" * 40)
    print(f"Files renamed: {file_renames}")
    print()

    print("=" * 60)
    print(f"Total: {total_content_changes} content changes, {file_renames} file renames")
    if dry_run:
        print("(DRY RUN - no changes were made)")
    print("=" * 60)


if __name__ == "__main__":
    main()
