#!/usr/bin/env python3
"""
Windows-specific platform configuration.
"""

from pathlib import Path
from typing import List

from utils.terminal import term
from .platformBase import PlatformConfig


class WindowsConfig(PlatformConfig):
    """Windows-specific configuration."""

    # Windows-specific CMake generators
    VISUAL_STUDIO_2022 = "Visual Studio 17 2022"
    NINJA = "Ninja"

    def get_default_generator(self) -> str:
        """Get the default CMake generator for Windows."""
        return self.VISUAL_STUDIO_2022

    def get_supported_generators(self) -> List[str]:
        """Get list of supported CMake generators for Windows."""
        return [self.VISUAL_STUDIO_2022, self.NINJA]

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
                self.get_default_generator(),  # Default to VS 2022, can be overridden
                "-A",
                "x64",
                "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL",
            ]
        )

        return True
