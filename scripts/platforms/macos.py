#!/usr/bin/env python3
"""
macOS-specific platform configuration with MoltenVK support.
"""

import platform
from pathlib import Path
from typing import List

from utils.terminal import term
from .platformBase import PlatformConfig


class MacOSConfig(PlatformConfig):
    """macOS-specific configuration with MoltenVK support."""

    # macOS-specific CMake generators
    XCODE = "Xcode"
    UNIX_MAKEFILES = "Unix Makefiles"
    NINJA = "Ninja"

    def get_default_generator(self) -> str:
        """Get the default CMake generator for macOS."""
        return self.XCODE

    def get_supported_generators(self) -> List[str]:
        """Get list of supported CMake generators for macOS."""
        return [self.XCODE, self.UNIX_MAKEFILES, self.NINJA]

    def detect_moltenvk(self):
        """Detect MoltenVK installation paths."""
        possible_paths = [
            "/usr/local/share/vulkan/icd.d/MoltenVK_icd.json",
            "/opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json",
            Path.home()
            / "VulkanSDK"
            / "macOS"
            / "share"
            / "vulkan"
            / "icd.d"
            / "MoltenVK_icd.json",
        ]

        for path in possible_paths:
            if Path(path).exists():
                return Path(path)
        return None

    def configure(self):
        term.section("Configuring for macOS")

        # Detect Vulkan SDK
        vulkan_sdk = self.detect_vulkan_sdk()
        if vulkan_sdk:
            term.success(f"Found Vulkan SDK at: {vulkan_sdk}")
        else:
            term.warn("VULKAN_SDK not set. Checking common installation paths...")
            # Check common macOS Vulkan SDK paths
            common_paths = [
                Path.home() / "VulkanSDK" / "macOS",
                Path("/usr/local"),
                Path("/opt/homebrew"),
            ]

            for path in common_paths:
                if (path / "lib" / "libvulkan.dylib").exists():
                    vulkan_sdk = path
                    term.success(f"Found Vulkan at: {vulkan_sdk}")
                    break

            if not vulkan_sdk:
                term.error("Vulkan SDK not found. Please install via:")
                print("   - Download from https://vulkan.lunarg.com/")
                print("   - Or install via Homebrew: brew install vulkan-loader")
                return False

        # Detect MoltenVK
        moltenvk_icd = self.detect_moltenvk()
        if moltenvk_icd:
            term.success(f"Found MoltenVK ICD at: {moltenvk_icd}")
            self.env_vars["VK_ICD_FILENAMES"] = str(moltenvk_icd)
        else:
            term.warn("MoltenVK ICD not found. Vulkan may not work properly.")

        # macOS-specific CMake settings
        arch = platform.machine().lower()
        if arch in ("arm64", "aarch64"):
            osx_arch = "arm64"
        else:
            osx_arch = "x86_64"

        self.cmake_args.extend(
            [
                "-G",
                self.get_default_generator(),  # Use Xcode generator by default
                f"-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15",  # Minimum macOS version
                f"-DCMAKE_OSX_ARCHITECTURES={osx_arch}",  # Keep architecture stable across shells
            ]
        )

        # MoltenVK specific settings
        self.cmake_args.extend(["-DVK_USE_PLATFORM_MACOS_MVK=ON", "-DMOLTENVK=ON"])

        return True
