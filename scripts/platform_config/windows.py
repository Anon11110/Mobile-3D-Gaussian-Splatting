#!/usr/bin/env python3
"""
Windows-specific platform configuration.
"""

from pathlib import Path

from enum import Enum

from utils import term
from .platformBase import PlatformConfig


class Generator(Enum):
    """CMake generator enumeration."""
    VISUAL_STUDIO_2022 = "Visual Studio 17 2022"
    XCODE = "Xcode"
    UNIX_MAKEFILES = "Unix Makefiles"
    NINJA = "Ninja"


class WindowsConfig(PlatformConfig):
    """Windows-specific configuration."""

    def configure(self):
        term.section("Configuring for Windows")

        # Detect Vulkan SDK
        vulkan_sdk = self.detect_vulkan_sdk()
        if vulkan_sdk:
            term.success(f"Found Vulkan SDK at: {vulkan_sdk}")
            self.cmake_args.extend(
                [
                    f"-DVULKAN_SDK={vulkan_sdk}",
                    f"-DVulkan_INCLUDE_DIRS={vulkan_sdk / 'Include'}",
                    f"-DVulkan_LIBRARIES={vulkan_sdk / 'Lib' / 'vulkan-1.lib'}",
                ]
            )
        else:
            term.warn(
                "Vulkan SDK not found. Please install Vulkan SDK and set VULKAN_SDK environment variable."
            )
            return False

        # Windows-specific CMake settings
        self.cmake_args.extend(
            [
                "-G",
                Generator.VISUAL_STUDIO_2022.value,  # Default to VS 2022, can be overridden
                "-A",
                "x64",
                "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL",
            ]
        )

        return True