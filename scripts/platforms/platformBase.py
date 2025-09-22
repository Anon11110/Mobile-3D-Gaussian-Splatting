#!/usr/bin/env python3
"""
Base platform configuration class.
"""

import os
import platform
from pathlib import Path
from typing import Union, List
from abc import ABC, abstractmethod

from utils.configure.types import BuildType


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

    def setup_cmake_args(
        self,
        build_type: Union[BuildType, str] = BuildType.RELEASE,
        enable_validation: bool = False,
        enable_rhi_tests: bool = False,
    ):
        """Setup basic CMake arguments."""
        build_type_str = (
            build_type.value if isinstance(build_type, BuildType) else build_type
        )
        self.cmake_args = [
            f"-DCMAKE_BUILD_TYPE={build_type_str}",
        ]

        # Enable compile_commands.json generation for Debug builds (IDE integration)
        if build_type_str == BuildType.DEBUG.value:
            self.cmake_args.append("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")

        if enable_validation:
            self.cmake_args.append("-DENABLE_VULKAN_VALIDATION=ON")

        if enable_rhi_tests:
            self.cmake_args.append("-DRHI_BUILD_TESTS=ON")

    def configure(self):
        """Platform-specific configuration. Override in subclasses."""
        pass
