#!/usr/bin/env python3
"""
Base platform configuration class.
"""

import os
import platform
import re
from pathlib import Path
from typing import Union, List
from abc import ABC, abstractmethod

from utils.configure.types import BuildType


# ============================================================================
# BUILD DIRECTORY NAMING UTILITIES
# ============================================================================


def normalize_os_name() -> str:
    """Return normalized OS name: 'macos', 'windows', or 'linux'."""
    system = platform.system().lower()
    if system == "darwin":
        return "macos"
    return system


def normalize_arch() -> str:
    """Return normalized architecture: 'arm64' or 'x64'."""
    machine = platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        return "arm64"
    return "x64"


def normalize_generator(generator: str) -> str:
    """Normalize CMake generator to a short lowercase name.

    'Xcode' -> 'xcode'
    'Ninja' -> 'ninja'
    'Unix Makefiles' -> 'makefiles'
    'Visual Studio 17 2022' -> 'vs2022'
    'Visual Studio 18 2026' -> 'vs2026'
    """
    g = generator.strip()
    match = re.match(r"Visual Studio \d+ (\d{4})", g)
    if match:
        return f"vs{match.group(1)}"
    mapping = {
        "xcode": "xcode",
        "ninja": "ninja",
        "unix makefiles": "makefiles",
    }
    return mapping.get(g.lower(), g.lower().replace(" ", "-"))


def compute_build_dir_name(generator: str, build_type: str) -> str:
    """Compute the build subdirectory name.

    Returns e.g. 'macos-arm64-xcode-RelWithDebInfo'.
    """
    return f"{normalize_os_name()}-{normalize_arch()}-{normalize_generator(generator)}-{build_type}"


def find_existing_build_dirs(root_dir: Path) -> List[Path]:
    """Find existing build subdirs matching the current platform pattern."""
    prefix = f"{normalize_os_name()}-{normalize_arch()}-"
    build_base = root_dir / "build"
    if not build_base.exists():
        return []
    return sorted(
        d for d in build_base.iterdir() if d.is_dir() and d.name.startswith(prefix)
    )


class PlatformConfig(ABC):
    """Base class for platform-specific configurations."""

    def __init__(self):
        self.platform_name = platform.system().lower()
        self.build_dir = Path("build")
        self.cmake_args = []
        self.env_vars = {}

    @abstractmethod
    def get_default_generator(self) -> str:
        """Get the default CMake generator for this platform."""
        pass

    @abstractmethod
    def get_supported_generators(self) -> List[str]:
        """Get list of supported CMake generators for this platform."""
        pass

    def validate_generator(self, generator: str) -> bool:
        """Validate if a generator is supported on this platform."""
        return generator in self.get_supported_generators()

    def detect_vulkan_sdk(self):
        """Detect Vulkan SDK installation."""
        vulkan_sdk = os.environ.get("VULKAN_SDK")
        if vulkan_sdk:
            return Path(vulkan_sdk)
        return None

    def get_build_dir_name(self, build_type: str) -> str:
        """Return the build subdirectory name for this platform config.

        Uses the instance's default generator and the given build type.
        """
        return compute_build_dir_name(self.get_default_generator(), build_type)

    def setup_cmake_args(
        self,
        build_type: Union[BuildType, str] = BuildType.RELEASE,
        enable_validation: bool = False,
        enable_rhi_tests: bool = False,
        backend: str = "vulkan",
    ):
        """Setup basic CMake arguments."""
        build_type_str = (
            build_type.value if isinstance(build_type, BuildType) else build_type
        )
        self.cmake_args = [
            f"-DCMAKE_BUILD_TYPE={build_type_str}",
            f"-DRHI_BACKEND={backend.upper()}",
        ]

        # Enable compile_commands.json generation for Debug/RelWithDebInfo builds (IDE integration)
        if build_type_str in (BuildType.DEBUG.value, BuildType.RELWITHDEBINFO.value):
            self.cmake_args.append("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")

        if enable_validation:
            self.cmake_args.append("-DENABLE_VULKAN_VALIDATION=ON")

        if enable_rhi_tests:
            self.cmake_args.append("-DRHI_BUILD_TESTS=ON")

    def configure(self):
        """Platform-specific configuration. Override in subclasses."""
        pass
