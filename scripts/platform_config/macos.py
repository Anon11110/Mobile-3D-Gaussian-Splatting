#!/usr/bin/env python3
"""
macOS-specific platform configuration with MoltenVK support.
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


class MacOSConfig(PlatformConfig):
    """macOS-specific configuration with MoltenVK support."""

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
        self.cmake_args.extend(
            [
                "-G",
                Generator.XCODE.value,  # Use Xcode generator by default
                f"-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15",  # Minimum macOS version
            ]
        )

        # MoltenVK specific settings
        self.cmake_args.extend(["-DVK_USE_PLATFORM_MACOS_MVK=ON", "-DMOLTENVK=ON"])

        return True
