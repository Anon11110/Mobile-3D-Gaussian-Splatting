#!/usr/bin/env python3
"""
Windows-specific platform configuration.
"""

import json
import os
import subprocess
from pathlib import Path
from typing import List, Tuple

from utils.terminal import term
from .platformBase import PlatformConfig


# Visual Studio version mapping: major version -> release year
# Pattern: VS releases every ~4 years (2019, 2022, 2026...)
VS_VERSIONS = {
    17: 2022,
    18: 2026,
}

# Minimum supported Visual Studio major version (VS 2022)
MINIMUM_VS_MAJOR = 17


def detect_visual_studio_installations() -> List[Tuple[int, int]]:
    """Detect installed Visual Studio versions using vswhere.exe.

    Returns list of (major_version, year) tuples sorted by version descending,
    e.g., [(18, 2026), (17, 2022)]. Only returns versions >= MINIMUM_VS_MAJOR.
    """
    # vswhere is typically at:
    # C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe
    program_files_x86 = os.environ.get("ProgramFiles(x86)", "")
    if not program_files_x86:
        return []

    vswhere_path = (
        Path(program_files_x86)
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )

    if not vswhere_path.exists():
        return []

    try:
        # Run vswhere to get installed versions
        result = subprocess.run(
            [str(vswhere_path), "-all", "-format", "json", "-products", "*"],
            capture_output=True,
            text=True,
            timeout=10,
        )

        if result.returncode != 0:
            return []

        # Parse JSON output
        installations = json.loads(result.stdout)
        versions: List[Tuple[int, int]] = []
        seen_majors: set[int] = set()

        for install in installations:
            # installationVersion is like "17.9.34728.136"
            version_str = install.get("installationVersion", "")
            if not version_str:
                continue

            try:
                major = int(version_str.split(".")[0])
            except (ValueError, IndexError):
                continue

            # Skip versions below minimum and duplicates
            if major < MINIMUM_VS_MAJOR or major in seen_majors:
                continue

            seen_majors.add(major)

            # Get year from mapping, or extrapolate for future versions
            if major in VS_VERSIONS:
                year = VS_VERSIONS[major]
            else:
                # Extrapolate: VS 17 = 2022, VS 18 = 2026, VS 19 = 2030, etc.
                year = 2022 + (major - 17) * 4

            versions.append((major, year))

        # Sort by major version descending (newest first)
        return sorted(versions, key=lambda x: x[0], reverse=True)

    except (subprocess.TimeoutExpired, json.JSONDecodeError, OSError):
        return []


class WindowsConfig(PlatformConfig):
    """Windows-specific configuration."""

    # Windows-specific CMake generators
    VISUAL_STUDIO_2022 = "Visual Studio 17 2022"
    NINJA = "Ninja"

    def __init__(self):
        super().__init__()
        self._detected_installations: List[Tuple[int, int]] | None = None

    def _get_detected_installations(self) -> List[Tuple[int, int]]:
        """Get cached Visual Studio installations."""
        if self._detected_installations is None:
            self._detected_installations = detect_visual_studio_installations()
        return self._detected_installations

    def get_default_generator(self) -> str:
        """Get the default CMake generator for Windows.

        Auto-detects the newest installed Visual Studio >= 2022.
        Falls back to VS 2022 if detection fails.
        """
        installations = self._get_detected_installations()
        if installations:
            # Use newest version (first in list since sorted descending)
            major, year = installations[0]
            return f"Visual Studio {major} {year}"

        # Fallback to VS 2022
        return self.VISUAL_STUDIO_2022

    def get_supported_generators(self) -> List[str]:
        """Get list of supported CMake generators for Windows."""
        generators: List[str] = []
        installations = self._get_detected_installations()

        for major, year in installations:
            generators.append(f"Visual Studio {major} {year}")

        # Always include VS 2022 as minimum supported (if not already present)
        if self.VISUAL_STUDIO_2022 not in generators:
            generators.append(self.VISUAL_STUDIO_2022)

        generators.append(self.NINJA)
        return generators

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
