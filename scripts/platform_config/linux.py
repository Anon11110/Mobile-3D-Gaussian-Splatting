#!/usr/bin/env python3
"""
Linux-specific platform configuration.
"""

import subprocess
from enum import Enum

from utils import term
from .platformBase import PlatformConfig


class Generator(Enum):
    """CMake generator enumeration."""

    VISUAL_STUDIO_2022 = "Visual Studio 17 2022"
    XCODE = "Xcode"
    UNIX_MAKEFILES = "Unix Makefiles"
    NINJA = "Ninja"


class LinuxConfig(PlatformConfig):
    """Linux-specific configuration."""

    def detect_vulkan_packages(self):
        """Detect Vulkan packages on Linux."""
        try:
            # Check for pkg-config
            result = subprocess.run(
                ["pkg-config", "--exists", "vulkan"], capture_output=True, text=True
            )
            return result.returncode == 0
        except FileNotFoundError:
            return False

    def configure(self):
        term.section("Configuring for Linux")

        # Check for Vulkan via pkg-config
        if self.detect_vulkan_packages():
            term.success("Found Vulkan via pkg-config")
        else:
            vulkan_sdk = self.detect_vulkan_sdk()
            if vulkan_sdk:
                term.success(f"Found Vulkan SDK at: {vulkan_sdk}")
                self.cmake_args.extend(
                    [
                        f"-DVULKAN_SDK={vulkan_sdk}",
                    ]
                )
            else:
                term.error("Vulkan not found. Please install via:")
                print("   - Ubuntu/Debian: sudo apt install libvulkan-dev vulkan-tools")
                print("   - Fedora: sudo dnf install vulkan-loader-devel vulkan-tools")
                print("   - Arch: sudo pacman -S vulkan-devel vulkan-tools")
                return False

        # Linux-specific CMake settings
        self.cmake_args.extend(
            [
                "-G",
                Generator.UNIX_MAKEFILES.value,
            ]
        )

        return True
