#!/usr/bin/env python3
"""
Platform configuration module.
"""

import platform
from typing import Optional

from .platformBase import PlatformConfig
from .windows import WindowsConfig
from .macos import MacOSConfig
from .linux import LinuxConfig


def get_platform_config() -> Optional[PlatformConfig]:
    """Factory function to get platform-specific configuration."""
    system = platform.system().lower()

    if system == "windows":
        return WindowsConfig()
    elif system == "darwin":
        return MacOSConfig()
    elif system == "linux":
        return LinuxConfig()
    else:
        print(f"❌ Unsupported platform: {system}")
        return None