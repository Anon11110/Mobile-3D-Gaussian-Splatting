#!/usr/bin/env python3
"""
Unit tests for configure.py refactored functions.
"""

import unittest
import tempfile
import argparse
import sys
import subprocess
import re
from pathlib import Path
from unittest.mock import patch, MagicMock, mock_open

# Add parent directory to path to import from scripts/
sys.path.append(str(Path(__file__).parent.parent))

from utils.terminal import term
from platforms.windows import WindowsConfig
from platforms.macos import MacOSConfig  
from platforms.linux import LinuxConfig
from utils.configure.types import (
    # Error handling and Result types
    ConfigureError,
    PlatformError,
    CMakeError,
    BuildError,
    ValidationError,
    Result,
    handle_error,
    # Types
    BuildType,
    GeneratorInfo,
)
from utils.configure.cmake_core import (
    # Target discovery functions
    discover_cmake_targets,
    discover_build_targets,
    # Output filtering
    OutputConfig,
    filter_build_output,
    # Environment
    log_environment_info,
)
from configure import (
    create_argument_parser,
    # Command classes
    Command,
    ConfigureCommand,
    BuildCommand,
    CommandFactory,
)


class TestBuildTypeEnum(unittest.TestCase):
    """Test BuildType enum functionality."""

    def test_debug_value(self):
        self.assertEqual(BuildType.DEBUG.value, "Debug")

    def test_release_value(self):
        self.assertEqual(BuildType.RELEASE.value, "Release")

    def test_enum_members(self):
        self.assertEqual(len(BuildType), 2)
        self.assertIn(BuildType.DEBUG, BuildType)
        self.assertIn(BuildType.RELEASE, BuildType)


class TestPlatformGenerators(unittest.TestCase):
    """Test platform-specific generator functionality."""

    def test_windows_generators(self):
        """Test Windows platform generator configuration."""
        config = WindowsConfig()
        self.assertEqual(config.get_default_generator(), "Visual Studio 17 2022")
        supported = config.get_supported_generators()
        self.assertIn("Visual Studio 17 2022", supported)
        self.assertIn("Ninja", supported)
        self.assertEqual(len(supported), 2)

    def test_macos_generators(self):
        """Test macOS platform generator configuration."""
        config = MacOSConfig()
        self.assertEqual(config.get_default_generator(), "Xcode")
        supported = config.get_supported_generators()
        self.assertIn("Xcode", supported)
        self.assertIn("Unix Makefiles", supported)
        self.assertIn("Ninja", supported)
        self.assertEqual(len(supported), 3)

    def test_linux_generators(self):
        """Test Linux platform generator configuration."""
        config = LinuxConfig()
        self.assertEqual(config.get_default_generator(), "Unix Makefiles")
        supported = config.get_supported_generators()
        self.assertIn("Unix Makefiles", supported)
        self.assertIn("Ninja", supported)
        self.assertEqual(len(supported), 2)

    def test_generator_validation(self):
        """Test generator validation across platforms."""
        windows_config = WindowsConfig()
        self.assertTrue(windows_config.validate_generator("Visual Studio 17 2022"))
        self.assertTrue(windows_config.validate_generator("Ninja"))
        self.assertFalse(windows_config.validate_generator("Xcode"))

        macos_config = MacOSConfig()
        self.assertTrue(macos_config.validate_generator("Xcode"))
        self.assertTrue(macos_config.validate_generator("Unix Makefiles"))
        self.assertFalse(macos_config.validate_generator("Visual Studio 17 2022"))

        linux_config = LinuxConfig()
        self.assertTrue(linux_config.validate_generator("Unix Makefiles"))
        self.assertTrue(linux_config.validate_generator("Ninja"))
        self.assertFalse(linux_config.validate_generator("Visual Studio 17 2022"))


class TestGeneratorInfo(unittest.TestCase):
    """Test GeneratorInfo dataclass and detection functionality."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.build_dir = Path(self.temp_dir)

    def tearDown(self):
        import shutil

        shutil.rmtree(self.temp_dir)

    def test_detect_visual_studio(self):
        """Test detection of Visual Studio generator."""
        cache_content = """
CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022
CMAKE_BUILD_TYPE:STRING=Release
"""
        cache_file = self.build_dir / "CMakeCache.txt"
        cache_file.write_text(cache_content)

        info = GeneratorInfo.detect(self.build_dir)
        self.assertEqual(info.name, "Visual Studio 17 2022")
        self.assertTrue(info.is_multi_config)

    def test_detect_xcode(self):
        """Test detection of Xcode generator."""
        cache_content = """
CMAKE_GENERATOR:INTERNAL=Xcode
CMAKE_BUILD_TYPE:STRING=Debug
"""
        cache_file = self.build_dir / "CMakeCache.txt"
        cache_file.write_text(cache_content)

        info = GeneratorInfo.detect(self.build_dir)
        self.assertEqual(info.name, "Xcode")
        self.assertTrue(info.is_multi_config)

    def test_detect_unix_makefiles(self):
        """Test detection of Unix Makefiles generator."""
        cache_content = """
CMAKE_GENERATOR:INTERNAL=Unix Makefiles
CMAKE_BUILD_TYPE:STRING=Release
"""
        cache_file = self.build_dir / "CMakeCache.txt"
        cache_file.write_text(cache_content)

        info = GeneratorInfo.detect(self.build_dir)
        self.assertEqual(info.name, "Unix Makefiles")
        self.assertFalse(info.is_multi_config)

    def test_detect_ninja(self):
        """Test detection of Ninja generator."""
        cache_content = """
CMAKE_GENERATOR:INTERNAL=Ninja
CMAKE_BUILD_TYPE:STRING=Debug
"""
        cache_file = self.build_dir / "CMakeCache.txt"
        cache_file.write_text(cache_content)

        info = GeneratorInfo.detect(self.build_dir)
        self.assertEqual(info.name, "Ninja")
        self.assertFalse(info.is_multi_config)

    def test_detect_no_cache_file(self):
        """Test detection when CMakeCache.txt doesn't exist."""
        info = GeneratorInfo.detect(self.build_dir)
        self.assertIsNone(info.name)
        self.assertFalse(info.is_multi_config)

    def test_detect_malformed_cache(self):
        """Test detection with malformed CMakeCache.txt."""
        cache_file = self.build_dir / "CMakeCache.txt"
        cache_file.write_text("malformed content")

        info = GeneratorInfo.detect(self.build_dir)
        self.assertIsNone(info.name)
        self.assertFalse(info.is_multi_config)

    def test_detect_multi_config_detection(self):
        """Test multi-config detection logic."""
        test_cases = [
            ("Visual Studio 16 2019", True),
            ("Visual Studio 17 2022", True),
            ("Xcode", True),
            ("Some Multi-Config Generator", True),
            ("Unix Makefiles", False),
            ("Ninja", False),
            ("", False),
        ]

        for generator_name, expected_multi_config in test_cases:
            with self.subTest(generator=generator_name):
                cache_content = f"CMAKE_GENERATOR:INTERNAL={generator_name}\n"
                cache_file = self.build_dir / "CMakeCache.txt"
                cache_file.write_text(cache_content)

                info = GeneratorInfo.detect(self.build_dir)
                self.assertEqual(info.is_multi_config, expected_multi_config)


class TestCreateArgumentParser(unittest.TestCase):
    """Test argument parser creation and parsing."""

    def setUp(self):
        self.parser = create_argument_parser()

    def test_parser_creation(self):
        """Test that parser is created successfully."""
        self.assertIsInstance(self.parser, argparse.ArgumentParser)

    def test_default_arguments(self):
        """Test default argument parsing."""
        args = self.parser.parse_args([])
        self.assertEqual(args.build_type, "Release")
        self.assertFalse(args.clean)
        self.assertFalse(args.validation)
        self.assertIsNone(args.generator)
        self.assertEqual(args.build_dir, "build")
        self.assertIsNone(args.command)

    def test_configure_arguments(self):
        """Test configure command arguments."""
        args = self.parser.parse_args(
            [
                "--build-type",
                "Debug",
                "--clean",
                "--validation",
                "--generator",
                "Ninja",
                "--build-dir",
                "custom_build",
            ]
        )

        self.assertEqual(args.build_type, "Debug")
        self.assertTrue(args.clean)
        self.assertTrue(args.validation)
        self.assertEqual(args.generator, "Ninja")
        self.assertEqual(args.build_dir, "custom_build")

    def test_build_command_basic(self):
        """Test basic build command parsing."""
        args = self.parser.parse_args(["build", "--target", "triangle"])
        self.assertEqual(args.command, "build")
        self.assertEqual(args.targets, ["triangle"])
        self.assertFalse(args.tests)
        self.assertFalse(args.run)

    def test_build_command_multiple_targets(self):
        """Test build command with multiple targets."""
        args = self.parser.parse_args(
            ["build", "--target", "triangle", "--target", "unit-tests"]
        )
        self.assertEqual(args.targets, ["triangle", "unit-tests"])

    def test_build_command_tests(self):
        """Test build command with tests flag."""
        args = self.parser.parse_args(["build", "--tests", "--run"])
        self.assertTrue(args.tests)
        self.assertTrue(args.run)

    def test_build_command_all(self):
        """Test build command with all target."""
        args = self.parser.parse_args(["build", "--target", "all", "--parallel", "8"])
        self.assertEqual(args.targets, ["all"])
        self.assertEqual(args.parallel, 8)

    def test_build_command_list_targets(self):
        """Test build command with list-targets flag."""
        args = self.parser.parse_args(["build", "--list-targets"])
        self.assertTrue(args.list_targets)

    def test_invalid_build_type(self):
        """Test invalid build type raises error."""
        with self.assertRaises(SystemExit):
            self.parser.parse_args(["--build-type", "Invalid"])


# Command Pattern and Error Handling Tests
class TestErrorHandling(unittest.TestCase):
    """Test error handling and result types."""

    def test_configure_error_creation(self):
        """Test ConfigureError creation and properties."""
        error = ConfigureError("Test message", exit_code=2, details="Test details")
        self.assertEqual(error.message, "Test message")
        self.assertEqual(error.exit_code, 2)
        self.assertEqual(error.details, "Test details")

    def test_specialized_errors(self):
        """Test specialized error types."""
        platform_error = PlatformError("Platform issue")
        cmake_error = CMakeError("CMake issue")
        build_error = BuildError("Build issue")
        validation_error = ValidationError("Validation issue")

        self.assertIsInstance(platform_error, ConfigureError)
        self.assertIsInstance(cmake_error, ConfigureError)
        self.assertIsInstance(build_error, ConfigureError)
        self.assertIsInstance(validation_error, ConfigureError)

    def test_result_ok(self):
        """Test successful Result creation."""
        result = Result.ok("success_value")
        self.assertTrue(result.success)
        self.assertEqual(result.value, "success_value")
        self.assertIsNone(result.error)

    def test_result_fail(self):
        """Test failed Result creation."""
        error = ConfigureError("Test error")
        result = Result.fail(error)
        self.assertFalse(result.success)
        self.assertIsNone(result.value)
        self.assertEqual(result.error, error)

    def test_result_unwrap_success(self):
        """Test unwrapping successful result."""
        result = Result.ok(42)
        value = result.unwrap()
        self.assertEqual(value, 42)

    def test_result_unwrap_failure(self):
        """Test unwrapping failed result raises error."""
        error = ConfigureError("Test error")
        result = Result.fail(error)
        with self.assertRaises(ConfigureError):
            result.unwrap()

    @patch("utils.configure_utils.term")
    def test_handle_error_basic(self, mock_term):
        """Test basic error handling."""
        error = ConfigureError("Test error", exit_code=5)
        exit_code = handle_error(error)

        mock_term.error.assert_called_once_with("Test error")
        self.assertEqual(exit_code, 5)

    @patch("utils.configure_utils.term")
    def test_handle_error_with_details(self, mock_term):
        """Test error handling with details."""
        error = ConfigureError("Test error", details="Additional info")
        handle_error(error)

        mock_term.error.assert_called_once_with("Test error")
        mock_term.info.assert_called_with("Details: Additional info")


class TestCommandFactory(unittest.TestCase):
    """Test CommandFactory functionality."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.source_dir = Path(self.temp_dir) / "source"
        self.build_dir = Path(self.temp_dir) / "build"
        self.source_dir.mkdir()
        self.build_dir.mkdir()

    def tearDown(self):
        import shutil

        shutil.rmtree(self.temp_dir)

    def test_create_configure_command(self):
        """Test creating configure command."""
        args = MagicMock()
        args.command = None

        result = CommandFactory.create_command(args, self.source_dir, self.build_dir)

        self.assertTrue(result.success)
        self.assertIsInstance(result.value, ConfigureCommand)

    def test_create_build_command(self):
        """Test creating build command."""
        args = MagicMock()
        args.command = "build"

        result = CommandFactory.create_command(args, self.source_dir, self.build_dir)

        self.assertTrue(result.success)
        self.assertIsInstance(result.value, BuildCommand)


class TestConfigureCommand(unittest.TestCase):
    """Test ConfigureCommand functionality."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.source_dir = Path(self.temp_dir) / "source"
        self.build_dir = Path(self.temp_dir) / "build"
        self.source_dir.mkdir()
        self.build_dir.mkdir()

        # Create CMakeLists.txt
        (self.source_dir / "CMakeLists.txt").write_text("project(TestProject)")

        # Create mock args
        self.mock_args = MagicMock()
        self.mock_args.build_type = "Release"
        self.mock_args.validation = False
        self.mock_args.generator = None
        self.mock_args.clean = False

    def tearDown(self):
        import shutil

        shutil.rmtree(self.temp_dir)

    def test_validation_success(self):
        """Test successful validation."""
        command = ConfigureCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.validate()

        self.assertTrue(result.success)

    def test_validation_no_source_dir(self):
        """Test validation with missing source directory."""
        non_existent_dir = Path(self.temp_dir) / "nonexistent"
        command = ConfigureCommand(self.mock_args, non_existent_dir, self.build_dir)
        result = command.validate()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn("Source directory not found", result.error.message)

    def test_validation_no_cmake_file(self):
        """Test validation with missing CMakeLists.txt."""
        (self.source_dir / "CMakeLists.txt").unlink()
        command = ConfigureCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.validate()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn("CMakeLists.txt not found", result.error.message)

    def test_validation_invalid_build_type(self):
        """Test validation with invalid build type."""
        self.mock_args.build_type = "Invalid"
        command = ConfigureCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.validate()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn("Invalid build type", result.error.message)

    @patch("configure.get_platform_config")
    @patch("utils.configure_utils.run_cmake")
    @patch("utils.configure_utils.print_success_instructions")
    def test_execute_success(
        self, mock_print_success, mock_run_cmake, mock_get_platform_config
    ):
        """Test successful command execution."""
        mock_config = MagicMock()
        mock_config.configure.return_value = True
        mock_config.cmake_args = []
        mock_config.env_vars = {}
        mock_get_platform_config.return_value = mock_config
        mock_run_cmake.return_value = True

        command = ConfigureCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.execute()

        self.assertTrue(result.success)
        self.assertEqual(result.value, 0)


class TestBuildCommand(unittest.TestCase):
    """Test BuildCommand functionality."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.source_dir = Path(self.temp_dir) / "source"
        self.build_dir = Path(self.temp_dir) / "build"
        self.source_dir.mkdir()
        self.build_dir.mkdir()

        # Create mock args
        self.mock_args = MagicMock()
        self.mock_args.targets = None
        self.mock_args.tests = False
        self.mock_args.list_targets = False
        self.mock_args.run = False
        self.mock_args.parallel = None
        self.mock_args.clean = False

    def tearDown(self):
        import shutil

        shutil.rmtree(self.temp_dir)

    def test_validation_conflicting_options(self):
        """Test validation with conflicting target options."""
        self.mock_args.targets = ["triangle"]
        self.mock_args.tests = True

        command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.validate()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn("Only one target selection option", result.error.message)

    def test_validation_no_targets(self):
        """Test validation with no targets specified."""
        command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.validate()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn("No targets specified", result.error.message)

    def test_validation_run_multiple_targets(self):
        """Test validation with run flag and multiple targets."""
        self.mock_args.targets = ["triangle", "unit-tests"]
        self.mock_args.run = True

        command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.validate()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn(
            "--run can only be used with a single target", result.error.message
        )

    def test_determine_targets_all(self):
        """Test determining targets with 'all' target."""
        self.mock_args.targets = ["all"]

        with patch("configure.discover_cmake_targets") as mock_discover_targets:
            mock_discover_targets.return_value = [
                "triangle",
                "unit-tests",
                "perf-tests",
            ]

            command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
            result = command._determine_targets()

            self.assertTrue(result.success)
            # The result should match what discover_cmake_targets returns
            self.assertEqual(result.value, ["triangle", "unit-tests", "perf-tests"])
            mock_discover_targets.assert_called_once_with(
                self.source_dir, self.build_dir
            )

    def test_determine_targets_tests(self):
        """Test determining targets with --tests flag."""
        self.mock_args.tests = True

        command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command._determine_targets()

        self.assertTrue(result.success)
        self.assertEqual(result.value, ["unit-tests", "perf-tests"])

    def test_determine_targets_specific(self):
        """Test determining specific targets."""
        self.mock_args.targets = ["triangle", "vulkan_rhi"]

        command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command._determine_targets()

        self.assertTrue(result.success)
        self.assertEqual(result.value, ["triangle", "vulkan_rhi"])

    def test_determine_targets_all_with_warning(self):
        """Test determining targets with 'all' plus other targets (shows warning)."""
        self.mock_args.targets = ["all", "triangle", "other"]

        with patch("configure.discover_cmake_targets") as mock_discover_targets:
            mock_discover_targets.return_value = ["triangle", "unit-tests"]
            with patch("utils.term.warn") as mock_warn:
                command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
                result = command._determine_targets()

                self.assertTrue(result.success)
                self.assertEqual(result.value, ["triangle", "unit-tests"])
                mock_warn.assert_called_once_with(
                    "When using 'all' target, other targets are ignored."
                )

    def test_determine_targets_improved_error_message(self):
        """Test improved error message when no targets specified."""
        command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command._determine_targets()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn(
            "No targets specified. Use '--target <name>', '--target all', or '--tests'",
            result.error.message,
        )


# New tests for recent changes


class TestOutputFiltering(unittest.TestCase):
    """Test output filtering functionality."""

    def test_output_config_patterns(self):
        """Test OutputConfig pattern definitions."""
        self.assertIn("error", OutputConfig.ERROR_PATTERNS)
        self.assertIn("failed", OutputConfig.ERROR_PATTERNS)
        self.assertIn("warning", OutputConfig.WARNING_PATTERNS)
        self.assertIn("warn", OutputConfig.WARNING_PATTERNS)

    def test_get_active_patterns_errors(self):
        """Test getting active patterns for errors only."""
        original_level = OutputConfig.OUTPUT_LEVEL
        OutputConfig.OUTPUT_LEVEL = "errors"
        try:
            patterns = OutputConfig.get_active_patterns()
            self.assertEqual(patterns, OutputConfig.ERROR_PATTERNS)
        finally:
            OutputConfig.OUTPUT_LEVEL = original_level

    def test_get_active_patterns_warnings(self):
        """Test getting active patterns for warnings level."""
        original_level = OutputConfig.OUTPUT_LEVEL
        OutputConfig.OUTPUT_LEVEL = "warnings"
        try:
            patterns = OutputConfig.get_active_patterns()
            expected = OutputConfig.ERROR_PATTERNS + OutputConfig.WARNING_PATTERNS
            self.assertEqual(patterns, expected)
        finally:
            OutputConfig.OUTPUT_LEVEL = original_level

    def test_get_active_patterns_all(self):
        """Test getting active patterns for all level."""
        original_level = OutputConfig.OUTPUT_LEVEL
        OutputConfig.OUTPUT_LEVEL = "all"
        try:
            patterns = OutputConfig.get_active_patterns()
            expected = (
                OutputConfig.ERROR_PATTERNS
                + OutputConfig.WARNING_PATTERNS
                + OutputConfig.INFO_PATTERNS
            )
            self.assertEqual(patterns, expected)
        finally:
            OutputConfig.OUTPUT_LEVEL = original_level

    def test_filter_build_output_with_errors(self):
        """Test filtering build output with error patterns."""
        stdout = """
Building target triangle
This is an error message
This is a regular line
Another failed operation occurred
Warning: deprecated function
This is another regular line
"""
        stderr = "Critical stderr message"
        patterns = ["error", "failed", "warning"]

        filtered_stdout, filtered_stderr = filter_build_output(stdout, stderr, patterns)

        self.assertIn("error message", filtered_stdout)
        self.assertIn("failed operation", filtered_stdout)
        self.assertIn("Warning: deprecated", filtered_stdout)
        self.assertNotIn("regular line", filtered_stdout)
        self.assertEqual(filtered_stderr, stderr)  # stderr should be preserved

    def test_filter_build_output_empty(self):
        """Test filtering with empty input."""
        filtered_stdout, filtered_stderr = filter_build_output("", "", ["error"])
        self.assertEqual(filtered_stdout, "")
        self.assertEqual(filtered_stderr, "")

    def test_filter_build_output_no_matches(self):
        """Test filtering with no matching patterns."""
        stdout = "Just some regular build output\nNothing special here"
        stderr = "Some stderr"
        patterns = ["error", "failed"]

        filtered_stdout, filtered_stderr = filter_build_output(stdout, stderr, patterns)

        self.assertEqual(filtered_stdout, "")
        self.assertEqual(filtered_stderr, stderr)

    def test_filter_build_output_line_limit(self):
        """Test output line limiting."""
        # Create output with more lines than the limit
        original_limit = OutputConfig.MAX_OUTPUT_LINES
        OutputConfig.MAX_OUTPUT_LINES = 3
        try:
            stdout = "\n".join([f"error line {i}" for i in range(10)])
            patterns = ["error"]

            filtered_stdout, _ = filter_build_output(stdout, "", patterns)

            lines = filtered_stdout.splitlines()
            self.assertEqual(len(lines), 4)  # 3 lines + truncation message
            self.assertIn("output truncated", lines[-1])
        finally:
            OutputConfig.MAX_OUTPUT_LINES = original_limit


class TestTargetDiscovery(unittest.TestCase):
    """Test target discovery functionality."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.source_dir = Path(self.temp_dir) / "source"
        self.build_dir = Path(self.temp_dir) / "build"
        self.source_dir.mkdir()
        self.build_dir.mkdir()

    def tearDown(self):
        import shutil

        shutil.rmtree(self.temp_dir)

    @patch("subprocess.run")
    def test_discover_cmake_targets_with_cmake_help(self, mock_run):
        """Test target discovery using cmake --build --target help."""
        # Mock successful cmake help output
        mock_result = MagicMock()
        mock_result.returncode = 0
        mock_result.stdout = "triangle\nunit-tests\nperf-tests\nmsplat_core"
        mock_run.return_value = mock_result

        targets = discover_cmake_targets(self.source_dir, self.build_dir)

        self.assertEqual(
            targets, ["triangle", "unit-tests", "perf-tests", "msplat_core"]
        )
        mock_run.assert_called_once()

    @patch("subprocess.run")
    def test_discover_cmake_targets_cmake_fails(self, mock_run):
        """Test target discovery fallback when cmake fails."""
        # Mock cmake failure
        mock_run.side_effect = subprocess.CalledProcessError(1, "cmake")

        # Create a CMakeLists.txt with targets
        cmake_file = self.source_dir / "CMakeLists.txt"
        cmake_file.write_text(
            """
project(TestProject)
add_executable(triangle main.cpp)
add_library(core src/core.cpp)
"""
        )

        targets = discover_cmake_targets(self.source_dir, self.build_dir)

        self.assertIn("triangle", targets)
        self.assertIn("core", targets)

    def test_discover_cmake_targets_no_build_dir(self):
        """Test target discovery when build directory doesn't exist."""
        non_existent_build = Path(self.temp_dir) / "nonexistent"

        # Create a CMakeLists.txt with targets
        cmake_file = self.source_dir / "CMakeLists.txt"
        cmake_file.write_text(
            """
project(TestProject)
add_executable(my_app main.cpp)
add_library(my_lib STATIC src/lib.cpp)
"""
        )

        targets = discover_cmake_targets(self.source_dir, non_existent_build)

        self.assertIn("my_app", targets)
        self.assertIn("my_lib", targets)

    def test_discover_cmake_targets_fallback_to_defaults(self):
        """Test target discovery fallback to default targets."""
        # No CMakeLists.txt files, no cmake help - should use fallback
        targets = discover_cmake_targets(self.source_dir, self.build_dir)

        # Should contain fallback targets
        expected_fallbacks = ["triangle", "unit-tests", "perf-tests"]
        for target in expected_fallbacks:
            self.assertIn(target, targets)

    def test_discover_cmake_targets_skips_third_party(self):
        """Test that third-party directories are skipped."""
        # Create third-party directory with CMakeLists.txt
        third_party_dir = self.source_dir / "third-party" / "some-lib"
        third_party_dir.mkdir(parents=True)
        third_party_cmake = third_party_dir / "CMakeLists.txt"
        third_party_cmake.write_text("add_executable(third_party_target main.cpp)")

        # Create main CMakeLists.txt
        main_cmake = self.source_dir / "CMakeLists.txt"
        main_cmake.write_text("add_executable(main_target main.cpp)")

        targets = discover_cmake_targets(self.source_dir, self.build_dir)

        self.assertIn("main_target", targets)
        self.assertNotIn("third_party_target", targets)

    def test_discover_build_targets_backward_compatibility(self):
        """Test that discover_build_targets still works for backward compatibility."""
        # This should call discover_cmake_targets internally
        with patch("utils.configure_utils.discover_cmake_targets") as mock_discover:
            mock_discover.return_value = ["test_target"]

            targets = discover_build_targets(self.build_dir)

            self.assertEqual(targets, ["test_target"])
            # Should infer source directory from build directory
            expected_source = (
                self.build_dir.parent
                if self.build_dir.name == "build"
                else self.build_dir
            )
            mock_discover.assert_called_once_with(expected_source, self.build_dir)


# Command pattern architecture successfully replaced old handle functions


if __name__ == "__main__":
    unittest.main()
