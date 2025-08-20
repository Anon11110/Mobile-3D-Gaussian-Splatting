#!/usr/bin/env python3
"""
Cross-platform CMake configuration script for the project.
"""

import sys
import platform
import argparse
from pathlib import Path
from abc import ABC, abstractmethod
from typing import List

from utils import term
from utils.configure_utils import (
    # Error handling
    ConfigureError,
    ValidationError,
    BuildError,
    PlatformError,
    CMakeError,
    Result,
    handle_error,
    log_environment_info,
    # Types
    BuildType,
    Generator,
    GeneratorInfo,
    # CMake utilities
    run_cmake,
    is_project_configured,
    auto_configure,
    discover_build_targets,
    discover_cmake_targets,
    build_targets,
    run_executable,
    clean_build_dir,
    print_success_instructions,
)
from platform_config import get_platform_config


# ============================================================================
# COMMAND PATTERN ARCHITECTURE
# ============================================================================


class Command(ABC):
    """Base command interface."""

    @abstractmethod
    def execute(self) -> Result[int]:
        """Execute the command and return a result."""
        pass

    @abstractmethod
    def validate(self) -> Result[None]:
        """Validate command parameters."""
        pass


class ConfigureCommand(Command):
    """Command for configuring the project."""

    def __init__(self, args, source_dir: Path, build_dir: Path):
        self.args = args
        self.source_dir = source_dir
        self.build_dir = build_dir

    def validate(self) -> Result[None]:
        """Validate configure command parameters."""
        try:
            if not self.source_dir.exists():
                return Result.fail(
                    ValidationError(f"Source directory not found: {self.source_dir}")
                )

            cmake_file = self.source_dir / "CMakeLists.txt"
            if not cmake_file.exists():
                return Result.fail(
                    ValidationError(f"CMakeLists.txt not found in: {self.source_dir}")
                )

            # Validate build type
            if self.args.build_type not in [
                BuildType.DEBUG.value,
                BuildType.RELEASE.value,
            ]:
                return Result.fail(
                    ValidationError(f"Invalid build type: {self.args.build_type}")
                )

            return Result.ok(None)

        except Exception as e:
            return Result.fail(ValidationError(f"Validation failed: {e}"))

    def execute(self) -> Result[int]:
        """Execute configure command with error handling."""
        try:
            # Validate first
            validation_result = self.validate()
            if not validation_result.success:
                return Result.fail(validation_result.error)

            term.section("CMake Configuration Begins")
            term.kv("Platform", f"{platform.system()} {platform.machine()}")
            term.kv("Python", sys.version)
            term.kv("Source directory", str(self.source_dir))
            term.kv("Build directory", str(self.build_dir))
            term.kv("Build type", self.args.build_type)

            # Get platform configuration
            config = get_platform_config()
            if not config:
                return Result.fail(
                    PlatformError(f"Unsupported platform: {platform.system()}")
                )

            # Setup configuration
            config.setup_cmake_args(self.args.build_type, self.args.validation)

            # Platform-specific configuration
            if not config.configure():
                return Result.fail(PlatformError("Platform configuration failed"))

            # Override generator if specified
            if self.args.generator:
                self._override_generator(config)

            # Clean build directory if requested
            if self.args.clean:
                clean_build_dir(self.build_dir)
            else:
                self.build_dir.mkdir(exist_ok=True)

            # Run CMake
            if not run_cmake(
                config, self.source_dir, self.build_dir, self.args.verbose
            ):
                return Result.fail(CMakeError("CMake configuration failed"))

            # Print success instructions
            print_success_instructions(self.args, self.source_dir, self.build_dir)

            return Result.ok(0)

        except Exception as e:
            return Result.fail(
                ConfigureError(f"Configuration failed: {e}", details=str(e))
            )

    def _override_generator(self, config):
        """Override the CMake generator."""
        new_args = []
        skip_next = False
        for arg in config.cmake_args:
            if skip_next:
                skip_next = False
                continue
            if arg == "-G":
                skip_next = True
                continue
            new_args.append(arg)
        config.cmake_args = new_args
        config.cmake_args.extend(["-G", self.args.generator])


class BuildCommand(Command):
    """Command for building targets."""

    def __init__(self, args, source_dir: Path, build_dir: Path):
        self.args = args
        self.source_dir = source_dir
        self.build_dir = build_dir

    def validate(self) -> Result[None]:
        """Validate build command parameters."""
        try:
            if not self.source_dir.exists():
                return Result.fail(
                    ValidationError(f"Source directory not found: {self.source_dir}")
                )

            # Check for conflicting target selection options
            target_options = [
                bool(self.args.targets),
                self.args.tests,
                self.args.list_targets,
            ]

            if sum(target_options) > 1:
                return Result.fail(
                    ValidationError(
                        "Only one target selection option can be used at a time"
                    )
                )

            if not any(target_options):
                return Result.fail(
                    ValidationError(
                        "No targets specified. Use --target, --tests, --all, or --list-targets"
                    )
                )

            # Validate run option
            if self.args.run:
                if not self.args.targets or len(self.args.targets) != 1:
                    return Result.fail(
                        ValidationError("--run can only be used with a single target")
                    )

            return Result.ok(None)

        except Exception as e:
            return Result.fail(ValidationError(f"Validation failed: {e}"))

    def execute(self) -> Result[int]:
        """Execute build command with error handling."""
        try:
            # Auto-configure if project is not configured
            if not is_project_configured(self.build_dir):
                auto_build_type = BuildType.DEBUG
                verbose = getattr(
                    self.args, "verbose", True
                )  # Handle case where verbose might not exist
                if not auto_configure(
                    self.source_dir, self.build_dir, auto_build_type, verbose
                ):
                    return Result.fail(ConfigureError("Auto-configuration failed"))

            # Handle list targets first (no validation needed)
            if self.args.list_targets:
                return self._list_targets()

            # Validate after auto-configure
            validation_result = self.validate()
            if not validation_result.success:
                return Result.fail(validation_result.error)

            # Determine targets to build
            targets_result = self._determine_targets()
            if not targets_result.success:
                return Result.fail(targets_result.error)

            targets_to_build = targets_result.value

            # Build the targets
            verbose = getattr(
                self.args, "verbose", True
            )  # Handle case where verbose might not exist
            if not build_targets(
                None,
                self.source_dir,
                self.build_dir,
                targets_to_build,
                self.args.parallel,
                self.args.clean,
                verbose,
            ):
                return Result.fail(BuildError("Build failed"))

            # Run executable if requested
            if self.args.run:
                target = targets_to_build[0]
                if not run_executable(self.build_dir, target):
                    return Result.fail(BuildError(f"Failed to run {target}"))

            return Result.ok(0)

        except Exception as e:
            return Result.fail(BuildError(f"Build failed: {e}", details=str(e)))

    def _list_targets(self) -> Result[int]:
        """List available build targets."""
        try:
            term.section("Available Build Targets")
            targets = discover_cmake_targets(self.source_dir, self.build_dir)
            for target in targets:
                print(f"  • {target}")
            return Result.ok(0)
        except Exception as e:
            return Result.fail(BuildError(f"Failed to list targets: {e}"))

    def _determine_targets(self) -> Result[List[str]]:
        """Determine which targets to build."""
        try:
            if self.args.tests:
                return Result.ok(["unit-tests", "perf-tests"])
            elif self.args.targets:
                # Check if 'all' is in the targets list
                if "all" in self.args.targets:
                    if len(self.args.targets) > 1:
                        term.warn("When using 'all' target, other targets are ignored.")
                    return Result.ok(discover_cmake_targets(self.source_dir, self.build_dir))
                else:
                    return Result.ok(self.args.targets)
            else:
                return Result.fail(ValidationError("No targets specified. Use '--target <name>', '--target all', or '--tests'"))
        except Exception as e:
            return Result.fail(BuildError(f"Failed to determine targets: {e}"))


class CommandFactory:
    """Factory for creating command instances."""

    @staticmethod
    def create_command(args, source_dir: Path, build_dir: Path) -> Result[Command]:
        """Create appropriate command based on arguments."""
        try:
            if args.command == "build":
                return Result.ok(BuildCommand(args, source_dir, build_dir))
            else:
                # Default to configure command
                return Result.ok(ConfigureCommand(args, source_dir, build_dir))
        except Exception as e:
            return Result.fail(ConfigureError(f"Failed to create command: {e}"))


# ============================================================================
# ARGUMENT PARSING AND MAIN ENTRY POINT
# ============================================================================


def create_argument_parser() -> argparse.ArgumentParser:
    """Create and configure the argument parser."""
    parser = argparse.ArgumentParser(
        description="Cross-platform CMake configuration and build tool for Mobile 3D Gaussian Splatting",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python scripts/configure.py                    # Configure with defaults
  python scripts/configure.py --clean --debug   # Clean configure for Debug
  python scripts/configure.py build --target triangle  # Build triangle target
  python scripts/configure.py build --target all       # Build all targets
  python scripts/configure.py build --tests --run      # Build and run tests
  python scripts/configure.py build --list-targets     # Show available targets
        """,
    )

    # Create subparsers for different commands
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # Configure command (default behavior, so these are also top-level args)
    parser.add_argument(
        "--build-type",
        choices=[BuildType.DEBUG.value, BuildType.RELEASE.value],
        default=BuildType.RELEASE.value,
        help="CMake build type",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean build directory before configuration",
    )
    parser.add_argument(
        "--validation",
        action="store_true",
        help="Enable Vulkan validation layers (Debug builds)",
    )
    parser.add_argument("--generator", help="Override CMake generator")
    parser.add_argument("--build-dir", default="build", help="Build directory path")
    parser.add_argument(
        "--debug-mode",
        action="store_true",
        help="Enable debug mode with detailed logging",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show detailed output during build/configure operations",
    )

    # Build subcommand
    build_parser = subparsers.add_parser("build", help="Build targets")
    build_parser.add_argument(
        "--target",
        action="append",
        dest="targets",
        help="Build specific target (can be used multiple times). Use 'all' to build all targets.",
    )
    build_parser.add_argument(
        "--list-targets", action="store_true", help="List available build targets"
    )
    build_parser.add_argument(
        "--tests",
        action="store_true",
        help="Build all test targets (unit-tests, perf-tests)",
    )
    build_parser.add_argument(
        "--run",
        action="store_true",
        help="Run executable after building (for single target)",
    )
    build_parser.add_argument(
        "--clean", action="store_true", help="Clean before building"
    )
    build_parser.add_argument(
        "--parallel", type=int, help="Number of parallel build jobs"
    )
    build_parser.add_argument(
        "--build-dir", default="build", help="Build directory path"
    )
    build_parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show detailed output during build operations",
    )

    return parser


def main() -> int:
    """Main entry point using command pattern with comprehensive error handling."""
    try:
        # Parse arguments
        parser = create_argument_parser()
        args = parser.parse_args()

        # Enable debug logging if requested
        if hasattr(args, "debug_mode") and args.debug_mode:
            term.section("Debug Mode Enabled")
            log_environment_info()
            term.sep()

        # Resolve project root: scripts/ is one level below project root
        script_dir = Path(__file__).resolve().parent
        root_dir = script_dir.parent.resolve()
        source_dir = root_dir
        build_dir = (root_dir / args.build_dir).resolve()

        # Create and execute command
        command_result = CommandFactory.create_command(args, source_dir, build_dir)
        if not command_result.success:
            if hasattr(args, "debug_mode") and args.debug_mode:
                log_environment_info()
            return handle_error(command_result.error)

        command = command_result.value
        execution_result = command.execute()

        if execution_result.success:
            return execution_result.value
        else:
            if hasattr(args, "debug_mode") and args.debug_mode:
                log_environment_info()
            return handle_error(execution_result.error)

    except KeyboardInterrupt:
        term.warn("Operation cancelled by user")
        return 130  # Standard exit code for SIGINT
    except Exception as e:
        error = ConfigureError(f"Unexpected error: {e}", details=str(e))
        try:
            if hasattr(args, "debug_mode") and args.debug_mode:
                log_environment_info()
        except:
            pass  # Don't let debug logging cause additional errors
        return handle_error(error)


if __name__ == "__main__":
    sys.exit(main())
