#!/usr/bin/env python3
"""
Base platform configuration class.
"""

import os
import platform
from pathlib import Path
from typing import Union
from enum import Enum


class BuildType(Enum):
    """Build type enumeration."""
    DEBUG = "Debug"
    RELEASE = "Release"


class PlatformConfig:
    """Base class for platform-specific configurations."""

    def __init__(self):
        self.platform_name = platform.system().lower()
        self.build_dir = Path("build")
        self.cmake_args = []
        self.env_vars = {}

    def detect_vulkan_sdk(self):
        """Detect Vulkan SDK installation."""
        vulkan_sdk = os.environ.get("VULKAN_SDK")
        if vulkan_sdk:
            return Path(vulkan_sdk)
        return None

    def setup_cmake_args(self, build_type: Union[BuildType, str] = BuildType.RELEASE, enable_validation: bool = False):
        """Setup basic CMake arguments."""
        build_type_str = build_type.value if isinstance(build_type, BuildType) else build_type
        self.cmake_args = [
            f"-DCMAKE_BUILD_TYPE={build_type_str}",
        ]

        if enable_validation:
            self.cmake_args.append("-DENABLE_VULKAN_VALIDATION=ON")

    def configure(self):
        """Platform-specific configuration. Override in subclasses."""
        pass