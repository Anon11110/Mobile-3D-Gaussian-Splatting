#!/usr/bin/env python3
"""
Linux-specific platform configuration.
"""

import subprocess
from typing import List

from utils.terminal import term
from .platformBase import PlatformConfig


class LinuxConfig(PlatformConfig):
    """Linux-specific configuration."""

    # Linux-specific CMake generators
    UNIX_MAKEFILES = "Unix Makefiles"
    NINJA = "Ninja"

    def get_default_generator(self) -> str:
        """Get the default CMake generator for Linux."""
        return self.UNIX_MAKEFILES

    def get_supported_generators(self) -> List[str]:
        """Get list of supported CMake generators for Linux."""
        return [self.UNIX_MAKEFILES, self.NINJA]

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
                self.get_default_generator(),
            ]
        )

        return True
