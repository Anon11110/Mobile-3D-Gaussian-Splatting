#!/usr/bin/env python3
"""
Build system constants and configuration.
Centralized configuration for the build system.
"""

import os
import platform
import shutil
from pathlib import Path
from typing import List


def _resolve_cmake_executable() -> str:
    """Resolve a usable CMake executable for the current platform.

    On Apple Silicon, users can accidentally pick Intel Homebrew CMake
    (`/usr/local/bin/cmake`) from a Rosetta shell. Prefer native Homebrew CMake
    (`/opt/homebrew/bin/cmake`) when possible to avoid architecture mismatch.
    """

    # Allow explicit user override.
    cmake_override = os.environ.get("CMAKE_EXECUTABLE")
    if cmake_override:
        return cmake_override

    discovered = shutil.which("cmake")
    if not discovered:
        native_homebrew_cmake = Path("/opt/homebrew/bin/cmake")
        if native_homebrew_cmake.exists():
            return str(native_homebrew_cmake)
        return "cmake"

    if platform.system() != "Darwin":
        return discovered

    if platform.machine().lower() not in ("arm64", "aarch64"):
        return discovered

    native_homebrew_cmake = Path("/opt/homebrew/bin/cmake")
    if native_homebrew_cmake.exists() and Path(discovered) == Path("/usr/local/bin/cmake"):
        return str(native_homebrew_cmake)

    return discovered


class BuildConstants:
    """Central configuration for build system constants and settings."""

    # Test targets
    DEFAULT_TEST_TARGETS: List[str] = ["unit-tests", "perf-tests"]

    # Fallback targets when discovery fails
    FALLBACK_TARGETS: List[str] = [
        "3dgs-renderer",
        "unit-tests",
        "perf-tests",
        "core",
        "RHI",
    ]

    # Timeouts and limits
    CMAKE_TIMEOUT_SECONDS: int = 10
    MAX_OUTPUT_LINES: int = 200

    # File patterns
    CMAKE_FILES_PATTERN: str = "**/CMakeLists.txt"
    THIRD_PARTY_EXCLUDE: str = "third-party"

    # Executables
    CMAKE_EXECUTABLE: str = _resolve_cmake_executable()

    # Build directories
    BUILD_DIR_BASE: str = "build"
    DEFAULT_BUILD_DIR: str = "build"
    BIN_SUBDIR_DEBUG: str = "bin/Debug"
    BIN_SUBDIR_RELEASE: str = "bin/Release"
    BIN_SUBDIR_RELWITHDEBINFO: str = "bin/RelWithDebInfo"

    # Standard exit codes
    EXIT_SUCCESS: int = 0
    EXIT_SIGINT: int = 130  # Standard exit code for SIGINT

    # CMake cache file
    CMAKE_CACHE_FILE: str = "CMakeCache.txt"

    # CMake generator flags
    CMAKE_GENERATOR_FLAG: str = "-G"

    # Build configuration patterns
    CMAKE_GENERATOR_CACHE_PREFIX: str = "CMAKE_GENERATOR:INTERNAL="
    CMAKE_BUILD_TYPE_CACHE_PREFIX: str = "CMAKE_BUILD_TYPE:"


class Messages:
    """User-facing messages and templates."""

    # Error message templates
    SOURCE_DIR_NOT_FOUND = "Source directory not found: {path}. Ensure you're running from the project root."
    CMAKE_FILE_NOT_FOUND = "CMakeLists.txt not found in: {path}. This doesn't appear to be a CMake project."
    INVALID_BUILD_TYPE = "Invalid build type: {type}. Valid options: {valid_types}"
    PROJECT_NOT_CONFIGURED = "Project not configured in: {path}"

    # Validation messages
    CONFLICTING_TARGET_OPTIONS = "Only one target selection option can be used at a time. Choose either --target, --tests, or --list-targets."
    NO_TARGETS_SPECIFIED = "No targets specified. Use --target, --tests, or --list-targets. Run with --list-targets to see available options."
    RUN_SINGLE_TARGET_ONLY = "--run can only be used with a single target. Specify exactly one target with --target <name>."

    # Success messages
    CONFIGURATION_COMPLETE = "Configuration completed successfully"
    AUTO_CONFIG_COMPLETE = "Auto-configuration completed successfully"

    # Progress messages
    CLEANING_BUILD_DIR = "🧹 Cleaning build directory: {path}"
    BUILDING_TARGET = "🔨 Building target: {target}"
    BUILT_TARGET_SUCCESS = "✅ Built {target} in {time:.1f}s"

    # Recovery suggestions
    CONFIGURE_FIRST = "Run configuration first with: python scripts/configure.py"
    CHECK_SYNTAX_HELP = "Check command syntax with --help"
    VERIFY_PATHS = "Verify file paths exist and are accessible"
    ENSURE_ARGS_COMPATIBLE = "Ensure arguments are compatible"
