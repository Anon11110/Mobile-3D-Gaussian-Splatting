#!/usr/bin/env python3
"""
Android platform configuration for Mobile 3D Gaussian Splatting.
"""

import os
import subprocess
import shutil
import urllib.request
from pathlib import Path
from typing import Optional, List

from .platformBase import PlatformConfig
from utils.terminal import term
from utils.configure.types import BuildType

GRADLE_WRAPPER_JAR_URL = "https://raw.githubusercontent.com/gradle/gradle/v8.7.0/gradle/wrapper/gradle-wrapper.jar"


class AndroidConfig(PlatformConfig):
    """Android platform configuration using Gradle + CMake."""

    def __init__(self, sdk_path: Optional[str] = None, jdk_path: Optional[str] = None):
        super().__init__()
        self.platform_name = "android"
        self.user_jdk_path = Path(jdk_path) if jdk_path else None
        self.android_home = Path(sdk_path) if sdk_path else self._find_android_sdk()
        self.gradle_cmd = self._find_gradle()

    def _find_android_sdk(self) -> Optional[Path]:
        """Find Android SDK location."""
        # Check environment variables
        for env_var in ["ANDROID_HOME", "ANDROID_SDK_ROOT"]:
            sdk_path = os.environ.get(env_var)
            if sdk_path:
                path = Path(sdk_path)
                if path.exists():
                    return path

        # Check common locations
        common_paths = []
        if os.name == "nt":  # Windows
            common_paths = [
                Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk",
                Path(os.environ.get("USERPROFILE", "")) / "AppData" / "Local" / "Android" / "Sdk",
            ]
        else:  # Linux/macOS
            home = Path.home()
            common_paths = [
                home / "Android" / "Sdk",
                home / "Library" / "Android" / "sdk",  # macOS
                Path("/opt/android-sdk"),
            ]

        for path in common_paths:
            if path.exists():
                return path

        return None

    def _find_gradle(self) -> str:
        """Find Gradle or Gradle wrapper."""
        # Check for gradlew in android directory
        gradlew = Path("android/gradlew")
        gradlew_bat = Path("android/gradlew.bat")

        if os.name == "nt" and gradlew_bat.exists():
            return str(gradlew_bat.absolute())
        elif gradlew.exists():
            return str(gradlew.absolute())

        return "gradle"

    def _ensure_gradle_wrapper(self) -> bool:
        """Ensure Gradle wrapper JAR exists, download if missing.

        Returns:
            True if wrapper is ready, False on failure
        """
        android_dir = Path("android")
        wrapper_jar = android_dir / "gradle" / "wrapper" / "gradle-wrapper.jar"

        if wrapper_jar.exists():
            return True

        wrapper_dir = wrapper_jar.parent
        wrapper_dir.mkdir(parents=True, exist_ok=True)

        term.info("Downloading Gradle wrapper JAR...")
        try:
            urllib.request.urlretrieve(GRADLE_WRAPPER_JAR_URL, wrapper_jar)
            term.success("Gradle wrapper downloaded successfully")
            return True
        except Exception as e:
            term.error(f"Failed to download Gradle wrapper: {e}")
            term.info("You can manually download from:")
            term.info(f"  {GRADLE_WRAPPER_JAR_URL}")
            term.info(f"  Place it at: {wrapper_jar}")
            return False

    def _find_java_home(self) -> Optional[Path]:
        """Find a valid JAVA_HOME path.

        Returns:
            Path to JAVA_HOME or None if not found
        """
        # Check user-provided path first
        if self.user_jdk_path:
            java_exe = self.user_jdk_path / "bin" / ("java.exe" if os.name == "nt" else "java")
            if java_exe.exists():
                return self.user_jdk_path
            else:
                term.warn(f"User-specified JDK path does not contain valid Java: {self.user_jdk_path}")
                # Fall through to auto-detection

        # Check JAVA_HOME environment variable
        java_home = os.environ.get("JAVA_HOME")
        if java_home:
            path = Path(java_home)
            java_exe = path / "bin" / ("java.exe" if os.name == "nt" else "java")
            if java_exe.exists():
                return path

        # Check common JDK locations
        common_paths = []
        if os.name == "nt":  # Windows
            # Android Studio bundled JDK
            program_files = [
                Path(os.environ.get("ProgramFiles", "C:\\Program Files")),
                Path(os.environ.get("ProgramFiles(x86)", "C:\\Program Files (x86)")),
                Path("D:\\Program Files"),
                Path("C:\\Program Files"),
                Path("C:\\Program Files (x86)"),
            ]
            for pf in program_files:
                common_paths.extend([
                    pf / "Android" / "Android Studio" / "jbr",
                    pf / "Android" / "Android Studio" / "jre",
                    pf / "Android" / "openjdk",  # Android SDK Manager installed JDK
                ])

            # Standalone JDKs
            common_paths.extend([
                Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "Eclipse Adoptium",
                Path("C:\\Program Files\\Java"),
                Path("C:\\Program Files\\Eclipse Adoptium"),
            ])
        else:  # Linux/macOS
            home = Path.home()
            common_paths.extend([
                # Android Studio bundled JDK
                home / "android-studio" / "jbr",
                Path("/opt/android-studio/jbr"),
                # macOS Android Studio
                Path("/Applications/Android Studio.app/Contents/jbr/Contents/Home"),
                # Common JDK locations
                Path("/usr/lib/jvm"),
                home / ".sdkman" / "candidates" / "java" / "current",
            ])

        for base_path in common_paths:
            if not base_path.exists():
                continue

            # Check if this is a JDK directly
            java_exe = base_path / "bin" / ("java.exe" if os.name == "nt" else "java")
            if java_exe.exists():
                return base_path

            # Check subdirectories (e.g., /usr/lib/jvm/java-17-openjdk)
            if base_path.is_dir():
                for subdir in base_path.iterdir():
                    if subdir.is_dir():
                        java_exe = subdir / "bin" / ("java.exe" if os.name == "nt" else "java")
                        if java_exe.exists():
                            return subdir

        return None

    def _check_java(self) -> bool:
        """Check if Java is available for Gradle.

        Returns:
            True if Java is available
        """
        # Check if java is already in PATH
        java_cmd = shutil.which("java")
        if java_cmd:
            return True

        # Try to find JAVA_HOME
        java_home = self._find_java_home()
        if java_home:
            # Set JAVA_HOME for this process and subprocesses
            os.environ["JAVA_HOME"] = str(java_home)
            term.info(f"Using JDK: {java_home}")
            return True

        term.error("Java not found!")
        term.info("Gradle requires Java to run. Please install JDK 17 or later:")
        term.info("  Option 1: Set JAVA_HOME to your JDK installation")
        term.info("  Option 2: Add java to your PATH")
        term.info("")
        term.info("JDK can be downloaded from:")
        term.info("  - https://adoptium.net/ (Eclipse Temurin)")
        term.info("  - https://www.oracle.com/java/technologies/downloads/")
        term.info("  - Or use the JDK bundled with Android Studio")
        return False

    def get_default_generator(self) -> str:
        """Android uses Ninja via Gradle."""
        return "Ninja"

    def get_supported_generators(self) -> List[str]:
        """Supported CMake generators for Android."""
        return ["Ninja", "Unix Makefiles"]

    def detect_android_sdk(self) -> bool:
        """Verify Android SDK is available."""
        if not self.android_home:
            term.error("Android SDK not found!")
            term.info("Please set ANDROID_HOME or ANDROID_SDK_ROOT environment variable")
            term.info("Or install Android SDK to a standard location")
            return False

        term.info(f"Found Android SDK at: {self.android_home}")

        # Check for NDK
        ndk_path = self.android_home / "ndk"
        if ndk_path.exists():
            ndks = list(ndk_path.iterdir())
            if ndks:
                term.info(f"Found NDK versions: {', '.join(d.name for d in ndks)}")
            else:
                term.warn("NDK directory exists but no versions found")
        else:
            term.warn("NDK not found in SDK directory")
            term.info("Install NDK via SDK Manager: sdkmanager 'ndk;25.2.9519653'")

        # Check for build-tools
        build_tools = self.android_home / "build-tools"
        if build_tools.exists():
            versions = list(build_tools.iterdir())
            if versions:
                term.info(f"Found build-tools: {', '.join(d.name for d in versions[:3])}")

        return True

    def configure(self) -> bool:
        """Configure for Android build."""
        if not self.detect_android_sdk():
            return False

        term.info("Android configuration uses Gradle + CMake")
        term.info("The native build will be handled by Gradle when building APK")
        return True

    def build_apk(
        self,
        build_type: str = "debug",
        verbose: bool = False
    ) -> bool:
        """Build Android APK using Gradle.

        Args:
            build_type: 'debug' or 'release'
            verbose: Show detailed build output

        Returns:
            True if build succeeded
        """
        android_dir = Path("android")
        if not android_dir.exists():
            term.error("Android project directory not found")
            term.info("Expected directory: ./android/")
            return False

        # Check Java is available
        if not self._check_java():
            return False

        # Ensure Gradle wrapper is available
        if not self._ensure_gradle_wrapper():
            return False

        # Re-find gradle command after ensuring wrapper exists
        self.gradle_cmd = self._find_gradle()

        # Set ANDROID_HOME for Gradle if we found it
        if self.android_home:
            os.environ["ANDROID_HOME"] = str(self.android_home)
            os.environ["ANDROID_SDK_ROOT"] = str(self.android_home)

        if build_type == "release":
            gradle_task = "assembleRelease"
            apk_subpath = "release/app-release.apk"
        else:
            gradle_task = "assembleDebug"
            apk_subpath = "debug/app-debug.apk"

        term.section(f"Building Android APK ({build_type})")

        # Build command
        cmd = [self.gradle_cmd, gradle_task]
        if verbose:
            cmd.append("--info")

        # Run Gradle
        original_dir = os.getcwd()
        try:
            os.chdir(android_dir)

            term.info(f"Running: {' '.join(cmd)}")
            result = subprocess.run(cmd, check=False)

            if result.returncode != 0:
                term.error("Gradle build failed")
                return False

            # Check for APK
            apk_path = Path("app/build/outputs/apk") / apk_subpath
            if not apk_path.exists():
                term.error(f"APK not found at expected path: {apk_path}")
                return False

        finally:
            os.chdir(original_dir)

        relative_apk_path = android_dir / "app/build/outputs/apk" / apk_subpath
        term.success(f"APK built successfully: {relative_apk_path}")

        return True

    def install_apk(self, apk_path: str) -> bool:
        """Install APK to connected device.

        Args:
            apk_path: Path to APK file

        Returns:
            True if installation succeeded
        """
        term.info("Installing APK to device...")

        adb = shutil.which("adb")
        if not adb:
            term.error("adb not found in PATH")
            return False

        result = subprocess.run(
            ["adb", "devices"],
            capture_output=True,
            text=True
        )
        lines = result.stdout.strip().split("\n")
        devices = [l for l in lines[1:] if l.strip() and "device" in l]

        if not devices:
            term.error("No Android devices connected")
            term.info("Connect a device or start an emulator with Vulkan support")
            return False

        term.info(f"Found {len(devices)} device(s)")

        cmd = ["adb", "install", "-r", apk_path]
        result = subprocess.run(cmd, check=False)

        if result.returncode != 0:
            term.error("APK installation failed")
            return False

        term.success("APK installed successfully")
        return True

    def run_app(self) -> bool:
        """Launch the app on connected device.

        Returns:
            True if launch succeeded
        """
        term.info("Launching app...")

        package = "com.msplat.gaussiansplatting"
        activity = "android.app.NativeActivity"

        cmd = [
            "adb", "shell", "am", "start",
            "-n", f"{package}/{activity}"
        ]

        result = subprocess.run(cmd, check=False)

        if result.returncode != 0:
            term.warn("App launch may have failed")
            return False

        term.success("App launched")
        return True

    def clean(self) -> bool:
        """Clean Android build artifacts.

        Returns:
            True if clean succeeded
        """
        android_dir = Path("android")
        if not android_dir.exists():
            term.warn("Android directory not found, nothing to clean")
            return True

        if not self._check_java():
            return False

        if self.android_home:
            os.environ["ANDROID_HOME"] = str(self.android_home)
            os.environ["ANDROID_SDK_ROOT"] = str(self.android_home)

        term.info("Cleaning Android build...")

        original_dir = os.getcwd()
        try:
            os.chdir(android_dir)
            result = subprocess.run([self.gradle_cmd, "clean"], check=False)
            return result.returncode == 0
        finally:
            os.chdir(original_dir)

    def list_devices(self) -> bool:
        """List connected Android devices.

        Returns:
            True if adb command succeeded
        """
        adb = shutil.which("adb")
        if not adb:
            term.error("adb not found in PATH")
            return False

        term.section("Connected Android Devices")
        subprocess.run(["adb", "devices", "-l"], check=False)
        return True
