#!/usr/bin/env python3
"""
Core types and error handling for the build system.
Extracted and organized build system types and error handling.
"""

import sys
import platform
from dataclasses import dataclass
from enum import Enum
from typing import Optional, Generic, TypeVar
from pathlib import Path

from ..terminal import term
from .constants import BuildConstants


# ============================================================================
# ERROR HANDLING SECTION
# ============================================================================


class ConfigureError(Exception):
    """Base class for configuration errors."""

    def __init__(self, message: str, exit_code: int = 1, details: Optional[str] = None):
        self.message = message
        self.exit_code = exit_code
        self.details = details
        super().__init__(message)


class PlatformError(ConfigureError):
    """Platform-specific configuration error."""

    pass


class CMakeError(ConfigureError):
    """CMake execution error."""

    pass


class BuildError(ConfigureError):
    """Build execution error."""

    pass


class ValidationError(ConfigureError):
    """Input validation error."""

    pass


T = TypeVar("T")


@dataclass
class Result(Generic[T]):
    """Result type for operations that can succeed or fail."""

    success: bool
    value: Optional[T] = None
    error: Optional[ConfigureError] = None

    @classmethod
    def ok(cls, value: T) -> "Result[T]":
        """Create a successful result."""
        return cls(success=True, value=value)

    @classmethod
    def fail(cls, error: ConfigureError) -> "Result[T]":
        """Create a failed result."""
        return cls(success=False, error=error)

    def unwrap(self) -> T:
        """Get the value or raise the error."""
        if self.success and self.value is not None:
            return self.value
        if self.error:
            raise self.error
        raise ConfigureError("Result has no value or error")


def handle_error(error: ConfigureError) -> int:
    """Centralized error handling with logging and recovery suggestions."""
    term.error(error.message)
    if error.details:
        term.info(f"Details: {error.details}")

    # Provide context-specific recovery suggestions
    if isinstance(error, PlatformError):
        term.info("Recovery suggestions:")
        if "Vulkan" in error.message:
            print("  • Install Vulkan SDK from https://vulkan.lunarg.com/")
            print("  • Set VULKAN_SDK environment variable")
        elif "MoltenVK" in error.message:
            print("  • Install MoltenVK via Vulkan SDK or Homebrew")
            print("  • Check VK_ICD_FILENAMES environment variable")

    elif isinstance(error, CMakeError):
        term.info("Recovery suggestions:")
        print("  • Check CMakeLists.txt for syntax errors")
        print("  • Verify all dependencies are installed")
        print("  • Try cleaning the build directory with --clean")
        print("  • Check CMake version compatibility")

    elif isinstance(error, BuildError):
        term.info("Recovery suggestions:")
        print("  • Configure the project first if not already done")
        print("  • Check for compilation errors in the output above")
        print("  • Verify all dependencies are available")
        print("  • Try building individual targets")

    elif isinstance(error, ValidationError):
        term.info("Recovery suggestions:")
        print("  • Check command syntax with --help")
        print("  • Verify file paths exist and are accessible")
        print("  • Ensure arguments are compatible")

    return error.exit_code


# ============================================================================
# BUILD TYPES SECTION
# ============================================================================


class BuildType(Enum):
    """Build type enumeration."""

    DEBUG = "Debug"
    RELEASE = "Release"
    RELWITHDEBINFO = "RelWithDebInfo"


@dataclass
class GeneratorInfo:
    """Information about the detected CMake generator."""

    name: Optional[str]
    is_multi_config: bool

    @classmethod
    def detect(cls, build_dir: Path) -> "GeneratorInfo":
        """Detect generator type from CMake cache."""
        generator = None
        try:
            cache_file = build_dir / BuildConstants.CMAKE_CACHE_FILE
            if cache_file.exists():
                content = cache_file.read_text()
                for line in content.splitlines():
                    if line.startswith(BuildConstants.CMAKE_GENERATOR_CACHE_PREFIX):
                        generator = line.split("=", 1)[1]
                        break
        except Exception:
            pass

        generator_lower = (generator or "").lower()
        is_multi_config = (
            ("visual studio" in generator_lower)
            or ("xcode" in generator_lower)
            or ("multi-config" in generator_lower)
        )

        return cls(name=generator, is_multi_config=is_multi_config)
