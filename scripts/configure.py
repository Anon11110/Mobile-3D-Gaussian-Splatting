#!/usr/bin/env python3
"""
Cross-platform CMake configuration script for the project.
"""

import sys
import platform
import argparse
from pathlib import Path
from abc import ABC, abstractmethod
from typing import List, Optional, Union
from argparse import Namespace

from utils.terminal import term
from utils.configure.types import (
    ConfigureError,
    ValidationError,
    BuildError,
    PlatformError,
    CMakeError,
    Result,
    handle_error,
    BuildType,
)
from utils.configure.cmake_core import (
    log_environment_info,
    run_cmake,
    is_project_configured,
    auto_configure,
    get_build_type_from_cache,
    discover_cmake_targets,
    build_targets,
    run_executable,
    clean_build_dir,
    print_success_instructions,
)
from utils.configure.constants import BuildConstants
from platforms import get_platform_config
from platforms.platformBase import compute_build_dir_name, find_existing_build_dirs

# Type aliases for better readability (defined after imports)
CommandArgs = Namespace
TargetList = List[str]
ValidationResult = Result[None]
ExecutionResult = Result[int]
BuildTypeInput = Union[BuildType, str]


# ============================================================================
# COMMAND PATTERN ARCHITECTURE
# ============================================================================


class Command(ABC):
    """Base command interface."""

    @abstractmethod
    def execute(self) -> ExecutionResult:
        """Execute the command and return a result."""
        pass

    @abstractmethod
    def validate(self) -> ValidationResult:
        """Validate command parameters."""
        pass


class ConfigureCommand(Command):
    """Command for configuring the project."""

    def __init__(self, args: CommandArgs, source_dir: Path, build_dir: Path):
        self.args = args
        self.source_dir = source_dir
        self.build_dir = build_dir

    def validate(self) -> ValidationResult:
        """Validate configure command parameters using ValidationFramework."""
        from utils.configure.validation import validate_configure_context

        return validate_configure_context(self.source_dir, self.build_dir, self.args)

    def execute(self) -> ExecutionResult:
        """Execute configure command with error handling."""
        validation_result = self._validate_configuration()
        if not validation_result.success:
            return validation_result

        config_result = self._setup_platform_configuration()
        if not config_result.success:
            return config_result

        preparation_result = self._prepare_build_environment(config_result.value)
        if not preparation_result.success:
            return preparation_result

        return self._execute_cmake_configuration(config_result.value)

    def _validate_configuration(self) -> ExecutionResult:
        """Validate configuration parameters and environment."""
        try:
            validation_result = self.validate()
            if not validation_result.success:
                return Result.fail(validation_result.error)

            self._display_configuration_info()
            return Result.ok(0)
        except Exception as e:
            return Result.fail(ValidationError(f"Configuration validation failed: {e}"))

    def _setup_platform_configuration(self) -> Result:
        """Setup platform-specific configuration."""
        try:
            config = get_platform_config()
            if not config:
                return Result.fail(
                    PlatformError(f"Unsupported platform: {platform.system()}")
                )

            # Enable RHI tests if --tests flag is used
            enable_rhi_tests = getattr(self.args, "tests", False)
            config.setup_cmake_args(
                self.args.build_type,
                self.args.validation,
                enable_rhi_tests,
                getattr(self.args, "backend", "vulkan"),
            )

            if not config.configure():
                return Result.fail(PlatformError("Platform configuration failed"))

            if self.args.generator:
                self._override_generator(config)

            return Result.ok(config)
        except Exception as e:
            return Result.fail(PlatformError(f"Platform configuration failed: {e}"))

    def _prepare_build_environment(self, config) -> ExecutionResult:
        """Prepare build environment and directories."""
        try:
            if self.args.clean:
                clean_build_dir(self.build_dir)
            else:
                self.build_dir.mkdir(parents=True, exist_ok=True)

            return Result.ok(0)
        except Exception as e:
            return Result.fail(
                ConfigureError(f"Build environment preparation failed: {e}")
            )

    def _execute_cmake_configuration(self, config) -> ExecutionResult:
        """Execute CMake configuration and display results."""
        try:
            if not run_cmake(
                config, self.source_dir, self.build_dir, self.args.verbose
            ):
                return Result.fail(CMakeError("CMake configuration failed"))

            print_success_instructions(self.args, self.source_dir, self.build_dir)
            return Result.ok(0)
        except Exception as e:
            return Result.fail(
                CMakeError(f"CMake execution failed: {e}", details=str(e))
            )

    def _display_configuration_info(self) -> None:
        """Display configuration information."""
        term.section("CMake Configuration Begins")
        term.kv("Platform", f"{platform.system()} {platform.machine()}")
        term.kv("Python", sys.version)
        term.kv("Source directory", str(self.source_dir))
        term.kv("Build directory", str(self.build_dir))
        term.kv("Build type", self.args.build_type)
        if hasattr(self.args, "backend"):
            term.kv("RHI backend", self.args.backend)

    def _override_generator(self, config):
        """Override the CMake generator with validation."""
        # Validate that the generator is supported on this platform
        if not config.validate_generator(self.args.generator):
            supported = ", ".join(config.get_supported_generators())
            raise ValidationError(
                f"Generator '{self.args.generator}' is not supported on {config.platform_name}. "
                f"Supported generators: {supported}"
            )

        # Remove existing generator arguments
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

    def __init__(self, args: CommandArgs, source_dir: Path, build_dir: Path):
        self.args = args
        self.source_dir = source_dir
        self.build_dir = build_dir

    def validate(self) -> ValidationResult:
        """Validate build command parameters using ValidationFramework."""
        from utils.configure.validation import validate_build_context

        return validate_build_context(self.source_dir, self.build_dir, self.args)

    def execute(self) -> ExecutionResult:
        """Execute build command with error handling."""
        auto_config_result = self._handle_auto_configuration()
        if not auto_config_result.success:
            return auto_config_result

        if self.args.list_targets:
            return self._list_targets()

        targets_result = self._validate_and_determine_targets()
        if not targets_result.success:
            return targets_result

        return self._build_and_run_targets(targets_result.value)

    def _handle_auto_configuration(self) -> ExecutionResult:
        """Handle automatic project configuration if needed."""
        try:
            if not is_project_configured(self.build_dir):
                auto_build_type = BuildType.DEBUG
                verbose = getattr(self.args, "verbose", True)
                if not auto_configure(
                    self.source_dir, self.build_dir, auto_build_type, verbose
                ):
                    return Result.fail(ConfigureError("Auto-configuration failed"))
            return Result.ok(0)
        except Exception as e:
            return Result.fail(ConfigureError(f"Auto-configuration failed: {e}"))

    def _validate_and_determine_targets(self) -> Result[TargetList]:
        """Validate command and determine targets to build."""
        try:
            # Validate after auto-configure
            validation_result = self.validate()
            if not validation_result.success:
                return Result.fail(validation_result.error)

            # Determine targets to build
            targets_result = self._determine_targets()
            if not targets_result.success:
                return Result.fail(targets_result.error)

            return targets_result
        except Exception as e:
            return Result.fail(ValidationError(f"Validation failed: {e}"))

    def _build_and_run_targets(self, targets: TargetList) -> ExecutionResult:
        """Build targets and optionally run executable."""
        try:
            # Build the targets
            verbose = getattr(self.args, "verbose", True)
            if not build_targets(
                None,
                self.source_dir,
                self.build_dir,
                targets,
                self.args.parallel,
                self.args.clean,
                verbose,
            ):
                return Result.fail(BuildError("Build failed"))

            # Run executable if requested
            if self.args.run:
                target = targets[0]
                run_build_type = getattr(self.args, "build_type", None)
                if not run_executable(self.build_dir, target, run_build_type):
                    return Result.fail(BuildError(f"Failed to run {target}"))

            return Result.ok(0)
        except Exception as e:
            return Result.fail(
                BuildError(f"Build execution failed: {e}", details=str(e))
            )

    def _list_targets(self) -> ExecutionResult:
        """List available build targets."""
        try:
            term.section("Available Build Targets")
            targets = discover_cmake_targets(self.source_dir, self.build_dir)
            for target in targets:
                print(f"  • {target}")
            return Result.ok(0)
        except Exception as e:
            return Result.fail(BuildError(f"Failed to list targets: {e}"))

    def _determine_targets(self) -> Result[TargetList]:
        """Determine which targets to build."""
        try:
            available_targets = discover_cmake_targets(self.source_dir, self.build_dir)

            if self.args.tests:
                # Filter test targets to only include those that exist
                test_targets = []
                for target in BuildConstants.DEFAULT_TEST_TARGETS:
                    if target in available_targets:
                        test_targets.append(target)
                    else:
                        term.warn(
                            f"Test target '{target}' not found in current build configuration"
                        )

                # Also check for rhi-tests if it exists (when RHI_BUILD_TESTS=ON)
                if "rhi-tests" in available_targets and "rhi-tests" not in test_targets:
                    test_targets.append("rhi-tests")

                if not test_targets:
                    return Result.fail(
                        ValidationError(
                            "No test targets available. You may need to reconfigure with --tests flag."
                        )
                    )
                return Result.ok(test_targets)
            elif self.args.targets:
                # Check if 'all' is in the targets list
                if "all" in self.args.targets:
                    if len(self.args.targets) > 1:
                        term.warn("When using 'all' target, other targets are ignored.")
                    return Result.ok(available_targets)
                else:
                    # Validate that requested targets exist
                    invalid_targets = []
                    for target in self.args.targets:
                        if target not in available_targets:
                            invalid_targets.append(target)

                    if invalid_targets:
                        target_list = ", ".join(available_targets[:10])
                        if len(available_targets) > 10:
                            target_list += "..."
                        return Result.fail(
                            ValidationError(
                                f"Target(s) not found: {', '.join(invalid_targets)}. "
                                f"Available targets: {target_list}"
                            )
                        )

                    return Result.ok(self.args.targets)
            else:
                # Show available targets in error message
                target_list = ", ".join(available_targets[:5])
                if len(available_targets) > 5:
                    target_list += "..."
                return Result.fail(
                    ValidationError(
                        f"No targets specified. Use '--target <name>', '--target all', or '--tests'. "
                        f"Available targets: {target_list}"
                    )
                )
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
        description="Cross-platform CMake configuration and build tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python scripts/configure.py                    # Configure with defaults
  python scripts/configure.py --clean --debug   # Clean configure for Debug
  python scripts/configure.py --backend metal3  # Configure with Metal3 backend
  python scripts/configure.py build --target 3dgs-renderer  # Build 3DGS renderer
  python scripts/configure.py build --target all       # Build all targets
  python scripts/configure.py build --tests --run      # Build and run tests
  python scripts/configure.py build --list-targets     # Show available targets
        """,
    )

    # Create subparsers for different commands
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # Configure command (default behavior, so these are also top-level args)
    parser.add_argument(
        "--backend",
        choices=["vulkan", "metal3"],
        default="vulkan",
        help="RHI backend to configure (default: vulkan)",
    )
    parser.add_argument(
        "--build-type",
        choices=[BuildType.DEBUG.value, BuildType.RELEASE.value, BuildType.RELWITHDEBINFO.value],
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
    parser.add_argument(
        "--tests",
        action="store_true",
        help="Enable building of all test targets including RHI tests",
    )
    parser.add_argument("--generator", help="Override CMake generator")
    parser.add_argument(
        "--build-dir",
        default=None,
        help="Build directory path (default: auto-computed from platform/generator/build-type)",
    )
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
        "--build-dir",
        default=None,
        help="Build directory path (default: auto-detected from existing build dirs)",
    )
    build_parser.add_argument(
        "--build-type",
        choices=[BuildType.DEBUG.value, BuildType.RELEASE.value, BuildType.RELWITHDEBINFO.value],
        default=None,
        help="Build type to select when multiple build dirs exist",
    )
    build_parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show detailed output during build operations",
    )

    # Android subcommand
    android_parser = subparsers.add_parser("android", help="Build for Android")
    android_parser.add_argument(
        "--build-type",
        choices=["debug", "release"],
        default="debug",
        help="Android build type (default: debug)",
    )
    android_parser.add_argument(
        "--sdk-path",
        type=str,
        help="Path to Android SDK (overrides ANDROID_HOME environment variable)",
    )
    android_parser.add_argument(
        "--jdk-path",
        type=str,
        help="Path to JDK (overrides JAVA_HOME and auto-detection)",
    )
    android_parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean before building",
    )
    android_parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show detailed build output",
    )

    return parser


def _resolve_build_dir_for_build(root_dir: Path, args: CommandArgs) -> Path:
    """Resolve the build directory for the build subcommand.

    When --build-dir is not given:
      - If --build-type is specified, compute the exact dir name
      - If exactly one matching subdir exists, use it
      - If multiple exist, error with a list
      - If none exist, compute a default dir (auto-configure will create it)
    """
    config = get_platform_config()
    generator = config.get_default_generator()

    def describe_build_dir(path: Path) -> str:
        try:
            rel = path.relative_to(root_dir)
        except ValueError:
            rel = path
        suffix = " (legacy)" if path.name == BuildConstants.DEFAULT_BUILD_DIR else ""
        return f"{rel}{suffix}"

    def collect_existing_build_dirs() -> List[Path]:
        existing = list(find_existing_build_dirs(root_dir))
        legacy = root_dir / BuildConstants.DEFAULT_BUILD_DIR
        if legacy.is_dir() and is_project_configured(legacy):
            existing.append(legacy)
        return sorted(existing)

    def matches_build_type(path: Path, build_type: str) -> bool:
        if path.name == BuildConstants.DEFAULT_BUILD_DIR:
            return get_build_type_from_cache(path) == build_type
        return path.name.split("-")[-1] == build_type

    build_type = getattr(args, "build_type", None)
    if build_type is not None:
        existing = collect_existing_build_dirs()
        if existing:
            matching = [d for d in existing if matches_build_type(d, build_type)]
            if len(matching) == 1:
                return matching[0].resolve()
            elif len(matching) > 1:
                names = "\n  ".join(describe_build_dir(d) for d in matching)
                term.error(
                    f"Multiple build directories match build type '{build_type}':\n  {names}\n"
                    "Specify one explicitly with --build-dir to select a generator."
                )
                sys.exit(1)
        # No matching existing build dirs — compute default path
        subdir = compute_build_dir_name(generator, build_type)
        return (root_dir / BuildConstants.BUILD_DIR_BASE / subdir).resolve()

    # Scan for existing build dirs matching this platform
    existing = collect_existing_build_dirs()

    if len(existing) == 1:
        return existing[0].resolve()
    elif len(existing) > 1:
        names = "\n  ".join(describe_build_dir(d) for d in existing)
        term.error(
            f"Multiple build directories found:\n  {names}\n"
            "Specify one with --build-type or --build-dir."
        )
        sys.exit(1)
    else:
        # None exist — compute default (auto-configure will create it)
        subdir = compute_build_dir_name(generator, BuildType.DEBUG.value)
        return (root_dir / BuildConstants.BUILD_DIR_BASE / subdir).resolve()


def main() -> int:
    """Main entry point using command pattern with comprehensive error handling."""
    try:
        # Parse arguments
        parser = create_argument_parser()
        args = parser.parse_args()

        # Handle Android command separately
        if args.command == "android":
            return handle_android_command(args)

        # Enable debug logging if requested
        if hasattr(args, "debug_mode") and args.debug_mode:
            term.section("Debug Mode Enabled")
            log_environment_info()
            term.sep()

        # Resolve project root: scripts/ is one level below project root
        script_dir = Path(__file__).resolve().parent
        root_dir = script_dir.parent.resolve()
        source_dir = root_dir

        # Resolve build directory
        if args.build_dir is not None:
            # Explicit override — use as-is
            build_dir = (root_dir / args.build_dir).resolve()
        elif args.command == "build":
            # Build subcommand: resolve from existing build dirs
            build_dir = _resolve_build_dir_for_build(root_dir, args)
        else:
            # Configure command: compute from platform/generator/build-type
            config = get_platform_config()
            generator = args.generator or config.get_default_generator()
            build_type = getattr(args, "build_type", BuildType.RELEASE.value)
            subdir = compute_build_dir_name(generator, build_type)
            build_dir = (root_dir / BuildConstants.BUILD_DIR_BASE / subdir).resolve()

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
        return BuildConstants.EXIT_SIGINT
    except Exception as e:
        error = ConfigureError(f"Unexpected error: {e}", details=str(e))
        try:
            if hasattr(args, "debug_mode") and args.debug_mode:
                log_environment_info()
        except:
            pass  # Don't let debug logging cause additional errors
        return handle_error(error)


def handle_android_command(args) -> int:
    """Handle Android-specific commands."""
    from platforms.android import AndroidConfig

    sdk_path = getattr(args, "sdk_path", None)
    jdk_path = getattr(args, "jdk_path", None)
    config = AndroidConfig(sdk_path=sdk_path, jdk_path=jdk_path)

    # Clean if requested
    if args.clean:
        if not config.clean():
            return 1

    # Check SDK
    if not config.configure():
        return 1

    # Build APK
    verbose = getattr(args, "verbose", False)
    build_type = getattr(args, "build_type", "debug")

    if config.build_apk(build_type, verbose):
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
