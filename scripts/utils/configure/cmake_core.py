#!/usr/bin/env python3
"""
CMake core utilities for the build system.

This module provides CMake-specific functionality including:
- Project configuration with CMake
- Build target discovery and execution
- Output filtering and progress indication
- Cross-platform build type detection
- Executable running with proper working directories

For complete module architecture and dependencies, see MODULE_ARCHITECTURE.md
"""

import os
import sys
import platform
import subprocess
import shutil
import re
import time
from pathlib import Path
from typing import Optional, List, Dict, Union

from ..terminal import term
from ..terminal.progress import create_progress
from .constants import BuildConstants, Messages
from .types import (
    Result,
    ConfigureError,
    BuildType,
    GeneratorInfo,
    handle_error,
)


# ============================================================================
# OUTPUT CONFIGURATION SECTION
# ============================================================================


# Legacy OutputConfig class - replaced by OutputStrategy pattern
# Kept for backward compatibility during migration
class OutputConfig:
    """DEPRECATED: Use OutputStrategy pattern instead.

    This class is kept for backward compatibility during migration to strategy patterns.
    """

    OUTPUT_LEVEL = "warnings"
    SHOW_STDERR = True
    MAX_OUTPUT_LINES = BuildConstants.MAX_OUTPUT_LINES

    @classmethod
    def get_active_patterns(cls) -> List[str]:
        """Get active patterns - delegates to OutputStrategy."""
        from .output_strategies import OutputStrategyFactory

        strategy = OutputStrategyFactory.create_strategy(cls.OUTPUT_LEVEL)
        if hasattr(strategy, "ERROR_PATTERNS"):
            if cls.OUTPUT_LEVEL == "errors":
                return strategy.ERROR_PATTERNS
            elif cls.OUTPUT_LEVEL == "warnings":
                return strategy.ERROR_PATTERNS + strategy.WARNING_PATTERNS

        # Fallback to legacy patterns
        return ["error", "failed", "fatal", "failure", "cannot", "abort"]


def filter_build_output(
    stdout: str, stderr: str, patterns: List[str] = None
) -> tuple[str, str]:
    """Filter build output using OutputStrategy pattern.

    Args:
        stdout: Standard output from build process
        stderr: Standard error from build process
        patterns: Legacy parameter for backward compatibility (ignored)

    Returns:
        Tuple of (filtered_stdout, filtered_stderr)
    """
    from .output_strategies import OutputStrategyFactory, OutputManager

    # Use new OutputStrategy pattern
    strategy = OutputStrategyFactory.create_strategy(OutputConfig.OUTPUT_LEVEL)
    manager = OutputManager(strategy)

    return manager.filter_output(stdout, stderr)


# ============================================================================
# ENVIRONMENT AND LOGGING SECTION
# ============================================================================


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
                [BuildConstants.CMAKE_EXECUTABLE, "--version"],
                capture_output=True,
                text=True,
                timeout=BuildConstants.CMAKE_TIMEOUT_SECONDS,
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
        print(Messages.CLEANING_BUILD_DIR.format(path=build_dir))
        shutil.rmtree(build_dir)
    build_dir.mkdir(exist_ok=True)


def run_cmake(config, source_dir: Path, build_dir: Path, verbose: bool = True) -> bool:
    """Run CMake configuration."""
    cmake_cmd = (
        [BuildConstants.CMAKE_EXECUTABLE] + config.cmake_args + [str(source_dir)]
    )

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
            # Capture output with progress indicator
            progress = create_progress("Configuring project", enabled=True)
            progress.start()
            
            try:
                result = subprocess.run(
                    cmake_cmd,
                    cwd=build_dir,
                    env=env,
                    capture_output=True,
                    text=True,
                    check=False,
                )
            finally:
                progress.stop()

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
    cmake_cache = build_dir / BuildConstants.CMAKE_CACHE_FILE
    return cmake_cache.exists()


def auto_configure(
    source_dir: Path,
    build_dir: Path,
    build_type: Union[BuildType, str] = BuildType.RELEASE,
    verbose: bool = True,
) -> bool:
    """Automatically configure the project with sensible defaults."""
    # Import here to avoid circular imports
    import sys
    from pathlib import Path
    # Add parent directory to path to import platforms module
    sys.path.insert(0, str(Path(__file__).parent.parent.parent))
    from platforms import get_platform_config

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

    term.success(Messages.AUTO_CONFIG_COMPLETE)
    return True


def discover_cmake_targets(source_dir: Path, build_dir: Path) -> List[str]:
    """Discover available CMake targets by scanning CMakeLists.txt files and querying CMake."""
    targets = []

    # First try to get actual targets from the build system
    if build_dir.exists():
        # Check for Xcode project first (macOS)
        xcode_project = build_dir / "Mobile-3D-Gaussian-Splatting.xcodeproj"
        if xcode_project.exists():
            try:
                result = subprocess.run(
                    ["xcodebuild", "-list", "-project", str(xcode_project)],
                    capture_output=True,
                    text=True,
                    cwd=build_dir,
                )
                if result.returncode == 0:
                    # Parse xcodebuild output for targets
                    in_targets = False
                    for line in result.stdout.splitlines():
                        line = line.strip()
                        if line == "Targets:":
                            in_targets = True
                            continue
                        elif in_targets:
                            if line and not line.startswith("Build Configurations:"):
                                # Skip cmake internal targets and unwanted library/shader targets
                                if line not in ["ALL_BUILD", "ZERO_CHECK", "install", "uninstall", "update_mappings"]:
                                    # Filter out static libraries and shader compilation targets
                                    if not line.endswith("_shaders") and line not in ["core", "app", "engine", "glfw"]:
                                        targets.append(line)
                            else:
                                break
                    if targets:
                        return targets
            except (subprocess.CalledProcessError, FileNotFoundError):
                pass
        
        # Try standard cmake --build approach for other generators
        try:
            result = subprocess.run(
                [
                    BuildConstants.CMAKE_EXECUTABLE,
                    "--build",
                    str(build_dir),
                    "--target",
                    "help",
                ],
                capture_output=True,
                text=True,
                cwd=build_dir,
            )
            if result.returncode == 0:
                # Parse the output for actual targets
                for line in result.stdout.splitlines():
                    line = line.strip()
                    if line and not line.startswith("..."):
                        # Filter out static libraries and shader compilation targets
                        if not line.endswith("_shaders") and line not in ["core", "app", "engine", "glfw"]:
                            targets.append(line)
                if targets:
                    return targets
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass

    # Fallback: scan CMakeLists.txt files for add_executable and add_library
    discovered_targets = []
    try:
        # Find all CMakeLists.txt files, excluding third-party directories
        for cmake_file in source_dir.glob(BuildConstants.CMAKE_FILES_PATTERN):
            if BuildConstants.THIRD_PARTY_EXCLUDE in str(cmake_file):
                continue

            try:
                content = cmake_file.read_text()

                # Find add_executable and add_library calls
                for line in content.splitlines():
                    line = line.strip()
                    if line.startswith("add_executable("):
                        # Extract target name (first argument after opening parenthesis)
                        match = re.search(
                            r"add_executable\s*\(\s*([^\s)]+)", line
                        )
                        if match:
                            target_name = match.group(1)
                            if target_name not in discovered_targets:
                                discovered_targets.append(target_name)
                    # Special case: always include RHI even though it's a library
                    elif line.startswith("add_library(RHI"):
                        if "RHI" not in discovered_targets:
                            discovered_targets.append("RHI")
            except Exception:
                # Skip files that can't be read or parsed
                continue

        if discovered_targets:
            return discovered_targets
    except Exception:
        pass

    # Final fallback to known project targets if all else fails
    return BuildConstants.FALLBACK_TARGETS


def discover_build_targets(build_dir: Path) -> List[str]:
    """Legacy function for backward compatibility. Use discover_cmake_targets() instead."""
    # Try to infer source directory from build directory
    source_dir = (
        build_dir.parent
        if build_dir.name == BuildConstants.DEFAULT_BUILD_DIR
        else build_dir
    )
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
    """Build specified targets using cmake --build.

    Legacy wrapper around BuildOrchestrator for backward compatibility.
    """
    from .orchestrator import BuildConfiguration, BuildOrchestrator

    try:
        # Create build configuration
        build_config = BuildConfiguration.create(
            source_dir=source_dir,
            build_dir=build_dir,
            parallel_jobs=parallel_jobs,
            clean_first=clean_first,
            verbose=verbose,
            output_level="warnings",
        )

        # Create orchestrator and build targets
        orchestrator = BuildOrchestrator(build_config)
        result = orchestrator.build_targets(targets)

        return result.success

    except Exception as e:
        term.error(f"Build orchestration failed: {e}")
        return False


def get_build_type_from_cache(build_dir: Path) -> str:
    """Extract build type from CMakeCache.txt."""
    try:
        cache_file = build_dir / BuildConstants.CMAKE_CACHE_FILE
        if cache_file.exists():
            content = cache_file.read_text()
            for line in content.splitlines():
                # Handle both STRING and UNINITIALIZED types
                if (
                    line.startswith(BuildConstants.CMAKE_BUILD_TYPE_CACHE_PREFIX)
                    and "=" in line
                ):
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
            if (build_dir / BuildConstants.BIN_SUBDIR_DEBUG).exists():
                build_type = "Debug"

    # Determine executable path
    if build_type == "Debug":
        exe_dir = build_dir / BuildConstants.BIN_SUBDIR_DEBUG
    else:
        exe_dir = build_dir / BuildConstants.BIN_SUBDIR_RELEASE

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

    term.success(Messages.CONFIGURATION_COMPLETE)

    # Note about compile_commands.json for Debug builds
    if hasattr(args, "build_type") and args.build_type == "Debug":
        # Check which generator is being used
        generator_info = GeneratorInfo.detect(build_dir)
        generator_lower = (generator_info.name or "").lower()

        if "xcode" in generator_lower:
            term.info(
                'Note: Use --generator "Unix Makefiles" or "Ninja" to generate compile_commands.json'
            )
        else:
            compile_commands_path = build_dir / "compile_commands.json"
            term.info(
                f"Compile commands database will be at: {compile_commands_path.resolve()}"
            )

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
