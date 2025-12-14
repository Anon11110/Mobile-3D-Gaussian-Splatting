#!/usr/bin/env python3

import argparse
import os
import sys
from pathlib import Path
from shutil import which
from subprocess import CalledProcessError, check_output
from utils.terminal import term


ALLOWED_EXTENSIONS = {
    # C++
    ".h",
    ".hpp",
    ".cpp",
    # Shaders
    ".vert",
    ".frag",
    ".comp",
}

# Hardcoded directories to skip (takes priority over FORMAT_DIRS)
SKIPPED_DIRS = {
    "third-party",
    "build",
    ".git",
    ".vscode",
    "CMakeFiles",
}

# Hardcoded files to skip (takes priority over other filters)
SKIPPED_FILES = {
    ".clang-format",
    "CMakeLists.txt",
    "unordered_dense.h",  # MIT licensed header
}

# Hardcoded whitelist of directories to format (relative to repo root).
# Edit this list to add/remove directories.
FORMAT_DIRS = [
    "rhi/src/",
    "rhi/include/",
    "examples/triangle/",
    "examples/unit-tests/",
    "examples/perf-tests/",
    "examples/particles/",
    "examples/splat-loader/",
    "examples/naive-splat-cpu/",
    "examples/gpu-sorting-renderer/",
    "examples/hybrid-splat-renderer/",
    "include/msplat/app",
    "include/msplat/core",
    "include/msplat/engine",
    "src/app",
    "src/core",
    "src/engine",
    #"shaders",
]


def get_ext(file_path: str) -> str:
    file_name = os.path.basename(file_path)
    _, file_ext = os.path.splitext(file_name)
    return file_ext


def is_allowed_source(path: Path) -> bool:
    # Check file extension
    if get_ext(str(path)) not in ALLOWED_EXTENSIONS:
        return False

    # Check if file name is in skip list (takes priority)
    if path.name in SKIPPED_FILES:
        return False

    # Check if any directory in path is in skip list (takes priority)
    parts = set(path.parts)
    if any(part in SKIPPED_DIRS for part in parts):
        return False

    return True


def is_in_format_dirs(path: Path, root: Path) -> bool:
    # If FORMAT_DIRS is empty, treat as allow-all (skip lists still apply)
    if not FORMAT_DIRS:
        return True
    for rel_dir in FORMAT_DIRS:
        d = (root / rel_dir).resolve()
        try:
            path.resolve().relative_to(d)
            return True
        except Exception:
            continue
    return False


def git_dirty_files() -> list[str]:
    # Modified tracked files
    out_m = check_output(["git", "ls-files", "-m"]).decode("utf-8")
    # Staged changes
    out_c = check_output(["git", "diff", "--name-only", "--cached"]).decode("utf-8")
    # Untracked files (respect .gitignore)
    out_u = check_output(["git", "ls-files", "-o", "--exclude-standard"]).decode(
        "utf-8"
    )
    files = set(
        filter(None, out_m.splitlines() + out_c.splitlines() + out_u.splitlines())
    )
    return sorted(files)


def git_branch_diff_files() -> list[str]:
    # Determine upstream of current branch
    try:
        upstream = (
            check_output(
                ["git", "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"]
            )
            .decode("utf-8")
            .strip()
        )
    except CalledProcessError:
        upstream = None
    merge_base = None
    if upstream:
        try:
            merge_base = (
                check_output(["git", "merge-base", "HEAD", upstream])
                .decode("utf-8")
                .strip()
            )
        except CalledProcessError:
            merge_base = None
    if not merge_base:
        # Fallback to origin/HEAD
        try:
            origin_head = (
                check_output(["git", "symbolic-ref", "refs/remotes/origin/HEAD"])
                .decode("utf-8")
                .strip()
            )
            merge_base = (
                check_output(["git", "merge-base", "HEAD", origin_head])
                .decode("utf-8")
                .strip()
            )
        except CalledProcessError:
            merge_base = None
    if not merge_base:
        # Last resort: compare against initial commit
        try:
            merge_base = (
                check_output(["git", "rev-list", "--max-parents=0", "HEAD"])
                .decode("utf-8")
                .splitlines()[0]
            )
        except CalledProcessError:
            return []
    try:
        out = check_output(
            ["git", "diff", "--name-only", f"{merge_base}..HEAD"]
        ).decode("utf-8")
    except CalledProcessError:
        return []
    return [f for f in out.splitlines() if f]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Format C/C++ files using clang-format"
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "-i", "--input", nargs="+", help="Specific files or directories to format"
    )
    group.add_argument(
        "-m",
        "--modified",
        action="store_true",
        help="Format only dirty (modified/staged/untracked) files",
    )
    args = parser.parse_args()

    term.section("Code Formatter")
    term.kv("Format dirs", ", ".join(FORMAT_DIRS) if FORMAT_DIRS else "<all>")

    if not which("git"):
        term.error("Missing git")
        return 1

    if not which("clang-format"):
        term.error("Missing clang-format")
        return 1

    root = Path(__file__).resolve().parent.parent

    # Gather candidate files
    if args.input:
        candidates = []
        for item in args.input:
            path = Path(item)
            if path.is_file():
                candidates.append(str(path))
            elif path.is_dir():
                # Recursively find all files in directory
                for file_path in path.rglob("*"):
                    if file_path.is_file():
                        candidates.append(str(file_path))
        term.kv("Mode", "explicit files/directories")
    elif args.modified:
        term.kv("Mode", "git dirty files")
        candidates = git_dirty_files()
    else:
        term.kv("Mode", "current branch changes")
        candidates = git_branch_diff_files()

    # Normalize and filter
    paths = [root / f for f in candidates]

    # Apply filtering logic: skip lists take priority over allow lists
    # First, apply hardcoded format dir whitelist (if specified)
    if FORMAT_DIRS:
        paths = [p for p in paths if is_in_format_dirs(p, root)]

    # Keep only allowed extensions and existing files, applying skip lists
    files = [str(p) for p in paths if p.is_file() and is_allowed_source(p)]

    term.kv("Files detected", str(len(files)))
    term.sep()

    if files:
        term.info("Formatting files:")
        for f in files:
            print(f"  {os.path.relpath(f, start=os.getcwd())}")

        failures = []
        for f in files:
            try:
                check_output(
                    [
                        "clang-format",
                        "-i",
                        f,
                        "-style",
                        "file",
                        "-fallback-style",
                        "none",
                    ],
                    cwd=str(root),
                )
            except CalledProcessError as e:
                failures.append((f, str(e)))

        term.sep()
        if failures:
            term.warn("Some files failed to format:")
            for f, reason in failures:
                print(f"  {f}: {reason}")
            return 2
        else:
            term.success("Formatting completed")
            return 0
    else:
        term.info("No files to format")
        return 0


if __name__ == "__main__":
    sys.exit(main())
