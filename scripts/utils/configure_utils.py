#!/usr/bin/env python3
"""
Utility functions for the configure.py script.
Consolidated error handling, types, and CMake utilities.
"""

import os
import sys
import platform
import subprocess
import shutil
import re
import time
from pathlib import Path
from dataclasses import dataclass
from enum import Enum
from typing import Optional, List, Dict, Union, Generic, TypeVar

from . import term


# ============================================================================
# OUTPUT CONFIGURATION SECTION
# ============================================================================


class OutputConfig:
    """Internal configuration for output filtering when verbose=False"""

    # Easy toggle - just change this value to control non-verbose output
    # Options: "errors", "warnings", "all", "custom"
    OUTPUT_LEVEL = "warnings"  # Default: only show errors (current behavior)

    # Pattern definitions for different output levels
    ERROR_PATTERNS = ["error", "failed", "fatal", "failure", "cannot", "abort"]
    WARNING_PATTERNS = ["warning", "warn", "deprecated", "notice", "caution"]
    INFO_PATTERNS = ["note", "info", "hint", "suggestion"]

    # Custom configuration (when OUTPUT_LEVEL = "custom")
    CUSTOM_PATTERNS = ["error", "warning", "failed", "fatal", "linking"]

    # Advanced options
    SHOW_STDERR = True  # Always show stderr regardless of level
    MAX_OUTPUT_LINES = 200  # Limit output to N lines to prevent overwhelming

    @classmethod
    def get_active_patterns(cls) -> List[str]:
        """Get the active filtering patterns based on OUTPUT_LEVEL."""
        if cls.OUTPUT_LEVEL == "errors":
            return cls.ERROR_PATTERNS
        elif cls.OUTPUT_LEVEL == "warnings":
            return cls.ERROR_PATTERNS + cls.WARNING_PATTERNS
        elif cls.OUTPUT_LEVEL == "all":
            return cls.ERROR_PATTERNS + cls.WARNING_PATTERNS + cls.INFO_PATTERNS
        elif cls.OUTPUT_LEVEL == "custom":
            return cls.CUSTOM_PATTERNS
        else:
            # Default fallback to errors only
            return cls.ERROR_PATTERNS


def filter_build_output(stdout: str, stderr: str, patterns: List[str]) -> tuple[str, str]:
    """Filter build output based on configured patterns.

    Args:
        stdout: Standard output from build process
        stderr: Standard error from build process
        patterns: List of patterns to match (case-insensitive)

    Returns:
        Tuple of (filtered_stdout, filtered_stderr)
    """
    filtered_stdout = ""
    filtered_stderr = stderr if OutputConfig.SHOW_STDERR else ""

    if stdout:
        stdout_lines = stdout.splitlines()
        matching_lines = []

        # Apply pattern filtering
        for line in stdout_lines:
            line_lower = line.lower()
            if any(pattern in line_lower for pattern in patterns):
                matching_lines.append(line)

        # Apply line limit to prevent overwhelming output
        if len(matching_lines) > OutputConfig.MAX_OUTPUT_LINES:
            matching_lines = matching_lines[:OutputConfig.MAX_OUTPUT_LINES]
            matching_lines.append(f"... (output truncated after {OutputConfig.MAX_OUTPUT_LINES} lines)")

        filtered_stdout = '\n'.join(matching_lines) if matching_lines else ""

    return filtered_stdout, filtered_stderr


# ============================================================================
# ERROR HANDLING SECTION
# ============================================================================


class ConfigureError(Exception):
    """Base class for configuration errors."""

    def __init__(self, message: str, exit_code: int = 1, details: Optional[str] = None):
        self.message = message
        self.exit_code = exit_code
        self.details = details
        super().__init__(message)


class PlatformError(ConfigureError):
    """Platform-specific configuration error."""

    pass


class CMakeError(ConfigureError):
    """CMake execution error."""

    pass


class BuildError(ConfigureError):
    """Build execution error."""

    pass


class ValidationError(ConfigureError):
    """Input validation error."""

    pass


T = TypeVar("T")


@dataclass
class Result(Generic[T]):
    """Result type for operations that can succeed or fail."""

    success: bool
    value: Optional[T] = None
    error: Optional[ConfigureError] = None

    @classmethod
    def ok(cls, value: T) -> "Result[T]":
        """Create a successful result."""
        return cls(success=True, value=value)

    @classmethod
    def fail(cls, error: ConfigureError) -> "Result[T]":
        """Create a failed result."""
        return cls(success=False, error=error)

    def unwrap(self) -> T:
        """Get the value or raise the error."""
        if self.success and self.value is not None:
            return self.value
        if self.error:
            raise self.error
        raise ConfigureError("Result has no value or error")


def handle_error(error: ConfigureError) -> int:
    """Centralized error handling with logging and recovery suggestions."""
    term.error(error.message)
    if error.details:
        term.info(f"Details: {error.details}")

    # Provide context-specific recovery suggestions
    if isinstance(error, PlatformError):
        term.info("Recovery suggestions:")
        if "Vulkan" in error.message:
            print("  • Install Vulkan SDK from https://vulkan.lunarg.com/")
            print("  • Set VULKAN_SDK environment variable")
        elif "MoltenVK" in error.message:
            print("  • Install MoltenVK via Vulkan SDK or Homebrew")
            print("  • Check VK_ICD_FILENAMES environment variable")

    elif isinstance(error, CMakeError):
        term.info("Recovery suggestions:")
        print("  • Check CMakeLists.txt for syntax errors")
        print("  • Verify all dependencies are installed")
        print("  • Try cleaning the build directory with --clean")
        print("  • Check CMake version compatibility")

    elif isinstance(error, BuildError):
        term.info("Recovery suggestions:")
        print("  • Configure the project first if not already done")
        print("  • Check for compilation errors in the output above")
        print("  • Verify all dependencies are available")
        print("  • Try building individual targets")

    elif isinstance(error, ValidationError):
        term.info("Recovery suggestions:")
        print("  • Check command syntax with --help")
        print("  • Verify file paths exist and are accessible")
        print("  • Ensure arguments are compatible")

    return error.exit_code


def log_environment_info():
    """Log environment information for debugging."""
    try:
        import subprocess

        term.info("Environment Information:")
        print(
            f"  Platform: {platform.system()} {platform.release()} {platform.machine()}"
        )
        print(f"  Python: {sys.version}")

        # Check CMake version
        try:
            cmake_result = subprocess.run(
                ["cmake", "--version"], capture_output=True, text=True, timeout=10
            )
            if cmake_result.returncode == 0:
                cmake_version = cmake_result.stdout.split("\n")[0]
                print(f"  {cmake_version}")
        except (subprocess.TimeoutExpired, FileNotFoundError):
            print("  CMake: Not found or not accessible")

        # Check Vulkan SDK
        vulkan_sdk = os.environ.get("VULKAN_SDK")
        if vulkan_sdk:
            print(f"  Vulkan SDK: {vulkan_sdk}")
        else:
            print("  Vulkan SDK: Not set")

    except Exception as e:
        term.warn(f"Could not gather environment info: {e}")


# ============================================================================
# TYPES SECTION
# ============================================================================


class BuildType(Enum):
    """Build type enumeration."""

    DEBUG = "Debug"
    RELEASE = "Release"


class Generator(Enum):
    """CMake generator enumeration."""

    VISUAL_STUDIO_2022 = "Visual Studio 17 2022"
    XCODE = "Xcode"
    UNIX_MAKEFILES = "Unix Makefiles"
    NINJA = "Ninja"


@dataclass
class GeneratorInfo:
    """Information about the detected CMake generator."""

    name: Optional[str]
    is_multi_config: bool

    @classmethod
    def detect(cls, build_dir: Path) -> "GeneratorInfo":
        """Detect generator type from CMake cache."""
        generator = None
        try:
            cache_file = build_dir / "CMakeCache.txt"
            if cache_file.exists():
                content = cache_file.read_text()
                for line in content.splitlines():
                    if line.startswith("CMAKE_GENERATOR:INTERNAL="):
                        generator = line.split("=", 1)[1]
                        break
        except Exception:
            pass

        generator_lower = (generator or "").lower()
        is_multi_config = (
            ("visual studio" in generator_lower)
            or ("xcode" in generator_lower)
            or ("multi-config" in generator_lower)
        )

        return cls(name=generator, is_multi_config=is_multi_config)


# ============================================================================
# CMAKE UTILITIES SECTION
# ============================================================================


def get_project_name(source_dir: Path) -> str:
    """Extract project name from CMakeLists.txt, fallback to directory name."""
    cmake_file = source_dir / "CMakeLists.txt"

    if cmake_file.exists():
        try:
            content = cmake_file.read_text()
            # Look for project() declaration
            match = re.search(r"project\s*\(\s*([^\s)]+)", content, re.IGNORECASE)
            if match:
                return match.group(1)
        except Exception as e:
            term.warn(f"Could not parse CMakeLists.txt: {e}")

    # Fallback to directory name
    return source_dir.name


def clean_build_dir(build_dir: Path) -> None:
    """Clean the build directory."""
    if build_dir.exists():
        print(f"🧹 Cleaning build directory: {build_dir}")
        shutil.rmtree(build_dir)
    build_dir.mkdir(exist_ok=True)


def run_cmake(config, source_dir: Path, build_dir: Path, verbose: bool = True) -> bool:
    """Run CMake configuration."""
    cmake_cmd = ["cmake"] + config.cmake_args + [str(source_dir)]

    term.section("Running CMake configuration")
    if verbose:
        term.kv("Command", " ".join(cmake_cmd))
        term.kv("Working directory", str(build_dir))

    # Set environment variables
    env = os.environ.copy()
    env.update(config.env_vars)

    if verbose and config.env_vars:
        term.info("Environment variables:")
        for key, value in config.env_vars.items():
            print(f"  {key}={value}")

    try:
        if verbose:
            # Stream output in real-time (current behavior)
            result = subprocess.run(
                cmake_cmd, cwd=build_dir, env=env, check=True, text=True
            )
        else:
            # Capture output, show only on error
            term.info("Configuring project...")
            result = subprocess.run(
                cmake_cmd,
                cwd=build_dir,
                env=env,
                capture_output=True,
                text=True,
                check=False,
            )

            if result.returncode != 0:
                term.error("CMake configuration failed!")

                # Use new filtering system for non-verbose output
                active_patterns = OutputConfig.get_active_patterns()
                filtered_stdout, filtered_stderr = filter_build_output(
                    result.stdout or "", result.stderr or "", active_patterns
                )

                if filtered_stderr:
                    print(filtered_stderr)
                if filtered_stdout:
                    print(filtered_stdout)
                return False
            else:
                term.success("CMake configuration completed")

        return True
    except subprocess.CalledProcessError as e:
        term.error(f"CMake configuration failed: {e}")
        return False


def is_project_configured(build_dir: Path) -> bool:
    """Check if the project is properly configured."""
    if not build_dir.exists():
        return False

    # Check for CMakeCache.txt which indicates configuration has been run
    cmake_cache = build_dir / "CMakeCache.txt"
    return cmake_cache.exists()


def auto_configure(
    source_dir: Path,
    build_dir: Path,
    build_type: Union[BuildType, str] = BuildType.RELEASE,
    verbose: bool = True,
) -> bool:
    """Automatically configure the project with sensible defaults."""
    from platform_config import (
        get_platform_config,
    )  # Import here to avoid circular imports

    term.section("Auto-configuring project")
    term.info(f"Project not configured, running automatic configuration...")
    if verbose:
        term.kv("Build type", build_type)

    # Get platform configuration
    config = get_platform_config()
    if not config:
        return False

    # Setup configuration with defaults
    config.setup_cmake_args(build_type, enable_validation=False)

    # Platform-specific configuration
    if not config.configure():
        return False

    # Create build directory
    build_dir.mkdir(exist_ok=True)

    # Run CMake configuration
    if not run_cmake(config, source_dir, build_dir, verbose):
        return False

    term.success("Auto-configuration completed successfully")
    return True


def discover_cmake_targets(source_dir: Path, build_dir: Path) -> List[str]:
    """Discover available CMake targets by scanning CMakeLists.txt files and querying CMake."""
    targets = []

    # First try to get actual targets from CMake if available
    if build_dir.exists():
        try:
            result = subprocess.run(
                ["cmake", "--build", str(build_dir), "--target", "help"],
                capture_output=True,
                text=True,
                cwd=build_dir,
            )
            if result.returncode == 0:
                # Parse the output for actual targets
                for line in result.stdout.splitlines():
                    line = line.strip()
                    if line and not line.startswith("..."):
                        targets.append(line)
                if targets:
                    return targets
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass

    # Fallback: scan CMakeLists.txt files for add_executable and add_library
    discovered_targets = []
    try:
        # Find all CMakeLists.txt files, excluding third-party directories
        for cmake_file in source_dir.glob("**/CMakeLists.txt"):
            if "third-party" in str(cmake_file):
                continue
            
            try:
                content = cmake_file.read_text()
                
                # Find add_executable and add_library calls
                for line in content.splitlines():
                    line = line.strip()
                    if line.startswith(("add_executable(", "add_library(")):
                        # Extract target name (first argument after opening parenthesis)
                        match = re.search(r"add_(?:executable|library)\s*\(\s*([^\s)]+)", line)
                        if match:
                            target_name = match.group(1)
                            if target_name not in discovered_targets:
                                discovered_targets.append(target_name)
            except Exception:
                # Skip files that can't be read or parsed
                continue
                
        if discovered_targets:
            return discovered_targets
    except Exception:
        pass

    # Final fallback to known project targets if all else fails
    fallback_targets = [
        "triangle",
        "unit-tests", 
        "perf-tests",
        "core",
        "RHI",
    ]
    
    return fallback_targets


def discover_build_targets(build_dir: Path) -> List[str]:
    """Legacy function for backward compatibility. Use discover_cmake_targets() instead."""
    # Try to infer source directory from build directory
    source_dir = build_dir.parent if build_dir.name == "build" else build_dir
    return discover_cmake_targets(source_dir, build_dir)


def build_targets(
    config: Optional,
    source_dir: Path,
    build_dir: Path,
    targets: List[str],
    parallel_jobs: Optional[int] = None,
    clean_first: bool = False,
    verbose: bool = True,
) -> bool:
    """Build specified targets using cmake --build."""

    if not is_project_configured(build_dir):
        term.error(f"Project not configured in: {build_dir}")
        term.info("Run configuration first with: python scripts/configure.py")
        return False

    if clean_first:
        term.section("Cleaning build directory")
        try:
            result = subprocess.run(
                ["cmake", "--build", str(build_dir), "--target", "clean"],
                cwd=build_dir,
                check=True,
            )
        except subprocess.CalledProcessError as e:
            term.warn(f"Clean failed, continuing anyway: {e}")

    # Determine if this is a multi-config generator
    generator_info = GeneratorInfo.detect(build_dir)

    # Get build type from cache
    # Even for multi-config generators, CMAKE_BUILD_TYPE can be set during configuration
    build_type = get_build_type_from_cache(build_dir)

    # For multi-config generators, if we couldn't get it from cache or it's empty,
    # fall back to directory existence check
    if generator_info.is_multi_config and build_type == "Release":
        if (build_dir / "bin" / "Debug").exists():
            build_type = "Debug"

    term.section(f"Building targets: {', '.join(targets)}")
    if verbose:
        term.kv("Build directory", str(build_dir))
        term.kv("Build type", build_type)
        term.kv("Generator", generator_info.name or "Unknown")

    # Build each target
    success_count = 0
    for target in targets:
        if verbose:
            term.info(f"Building target: {target}")
        else:
            term.info(f"🔨 Building target: {target}")

        cmd = ["cmake", "--build", str(build_dir)]
        if generator_info.is_multi_config:
            cmd.extend(["--config", build_type])
        cmd.extend(["--target", target])
        if parallel_jobs:
            cmd.extend(["--parallel", str(parallel_jobs)])
        else:
            cmd.append("--parallel")

        if verbose:
            term.kv("Command", " ".join(cmd))

        try:
            start_time = time.time()

            if verbose:
                # Stream output in real-time (current behavior)
                result = subprocess.run(cmd, cwd=build_dir, check=True)
            else:
                # Capture output, show only on error
                result = subprocess.run(
                    cmd, cwd=build_dir, capture_output=True, text=True, check=False
                )

                if result.returncode != 0:
                    term.error(f"Failed to build target '{target}'!")

                    # Use new filtering system for non-verbose output
                    active_patterns = OutputConfig.get_active_patterns()
                    filtered_stdout, filtered_stderr = filter_build_output(
                        result.stdout or "", result.stderr or "", active_patterns
                    )

                    if filtered_stderr:
                        print(filtered_stderr)
                    if filtered_stdout:
                        print(filtered_stdout)
                    return False

            end_time = time.time()
            build_time = end_time - start_time

            if verbose:
                term.success(f"Built {target} in {build_time:.1f}s")
            else:
                term.success(f"✅ Built {target} in {build_time:.1f}s")
            success_count += 1

        except subprocess.CalledProcessError as e:
            term.error(f"Failed to build target '{target}': {e}")
            return False

    term.success(f"Successfully built {success_count}/{len(targets)} targets")
    return True


def get_build_type_from_cache(build_dir: Path) -> str:
    """Extract build type from CMakeCache.txt."""
    try:
        cache_file = build_dir / "CMakeCache.txt"
        if cache_file.exists():
            content = cache_file.read_text()
            for line in content.splitlines():
                # Handle both STRING and UNINITIALIZED types
                if line.startswith("CMAKE_BUILD_TYPE:") and "=" in line:
                    build_type = line.split("=", 1)[1]
                    if build_type:  # Only use if not empty
                        return build_type
    except Exception:
        pass
    return "Release"  # Default fallback


def run_executable(
    build_dir: Path, target: str, build_type: Optional[str] = None
) -> bool:
    """Run an executable target from the proper working directory."""
    # Auto-detect build type if not provided
    if build_type is None:
        # Determine if this is a multi-config generator
        generator_info = GeneratorInfo.detect(build_dir)

        # Read from cache first (works for both single and multi-config)
        build_type = get_build_type_from_cache(build_dir)

        # For multi-config generators, if we couldn't get it from cache or it's empty,
        # fall back to directory existence check
        if generator_info.is_multi_config and build_type == "Release":
            if (build_dir / "bin" / "Debug").exists():
                build_type = "Debug"

    # Determine executable path
    if build_type == "Debug":
        exe_dir = build_dir / "bin" / "Debug"
    else:
        exe_dir = build_dir / "bin" / "Release"

    exe_path = exe_dir / target
    if platform.system().lower() == "windows":
        exe_path = exe_path.with_suffix(".exe")

    if not exe_path.exists():
        term.error(f"Executable not found: {exe_path}")
        return False

    term.section(f"Running {target}")
    term.kv("Executable", str(exe_path))
    term.kv("Working directory", str(exe_dir))

    try:
        # Run from the executable's directory (important for asset loading)
        result = subprocess.run([f"./{target}"], cwd=exe_dir, check=False)

        if result.returncode == 0:
            term.success(f"{target} completed successfully")
        else:
            term.warn(f"{target} exited with code {result.returncode}")

        return result.returncode == 0

    except subprocess.CalledProcessError as e:
        term.error(f"Failed to run {target}: {e}")
        return False
    except FileNotFoundError:
        term.error(f"Executable not found or not executable: {exe_path}")
        return False


def print_success_instructions(args, source_dir: Path, build_dir: Path) -> None:
    """Print success instructions after configuration."""
    # Get project name
    project_name = get_project_name(source_dir)

    term.success("Configuration completed successfully")

    # Note about compile_commands.json for Debug builds
    if hasattr(args, 'build_type') and args.build_type == "Debug":
        # Check which generator is being used
        generator_info = GeneratorInfo.detect(build_dir)
        generator_lower = (generator_info.name or "").lower()

        if "xcode" in generator_lower:
            term.info("Note: Use --generator \"Unix Makefiles\" or \"Ninja\" to generate compile_commands.json")
        else:
            compile_commands_path = build_dir / "compile_commands.json"
            term.info(f"Compile commands database will be at: {compile_commands_path.resolve()}")

    term.sep()
    term.info("Next steps:")

    # Print build command suggestions
    term.info("Build with the new build command:")
    print(f"  python scripts/configure.py build --target triangle")
    print(f"  python scripts/configure.py build --tests --run")
    print(f"  python scripts/configure.py build --target all")
    print(f"  python scripts/configure.py build --list-targets")

    term.info("Or use traditional cmake commands:")

    # Print a single copy-pasteable command for building
    generator_info = GeneratorInfo.detect(build_dir)

    abs_build_dir = str(build_dir.resolve())

    if generator_info.is_multi_config:
        print(f"  cmake --build {abs_build_dir} --config {args.build_type} --parallel")
        generator_lower = (generator_info.name or "").lower()
        if "visual studio" in generator_lower or platform.system().lower() == "windows":
            sln_path = build_dir / f"{project_name}.sln"
            term.info(f"Or open the generated .sln in Visual Studio:")
            print(f'  start "{sln_path}"')
        if "xcode" in generator_lower or platform.system().lower() == "darwin":
            xcodeproj_path = build_dir / f"{project_name}.xcodeproj"
            term.info(f"Or open the generated .xcodeproj in Xcode:")
            print(f'  open "{xcodeproj_path}"')
    else:
        # Single-config generators (e.g., Ninja, Unix Makefiles) use CMAKE_BUILD_TYPE set at configure time
        print(f"  cmake --build {abs_build_dir} --parallel")
